#!/usr/bin/env python3
"""OpenTE extractor CLI.

Reads data and sprites from a user's own copy of Trade Empires and writes
OpenTE's `game_data/` directory (JSON tables + PNG sprites + maps +
manifest), per `OpenTE/spec/data-model.md`.

Usage::

    python -m extractor.main [--game-dir PATH] [--output PATH]

If `--game-dir` is omitted (or doesn't point at a valid installation), the
user is prompted interactively. If `--output` is omitted, output is written
to `./game_data` -- run this next to (or pass `--output` pointing at) the
OpenTE game executable so it can find its data automatically (see
`core::find_game_data_dir` in `game/src/core/paths.cpp`).
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path
from typing import Any, Callable

from .containers.container import DirNode, load, parse_tree
from .game_directory import GameDirectory, find_game_directory
from .manifest import Manifest, MapEntry, SpriteEntry, TableEntry, write_manifest
from .maps.region import extract_map, get_map_culture
from .sprites.buildings import find_building_sprite_path
from .sprites.decorations import extract_decoration_sprites
from .sprites.font import extract_fonts
from .sprites.sprite import decode_sprite, find_leaf, write_png_rgba
from .sprites.terrain import extract_bridge_sprites, extract_terrain_textures_all_cultures
from .sprites.ui import extract_ui_sprites, extract_main_menu_sprites
from .tables.abilities import extract_abilities
from .tables.bandits import extract_bandits
from .tables.buildings import extract_buildings
from .tables.commodities import extract_commodities
from .tables.episodes import extract_episodes
from .tables.events import extract_events
from .tables.guards import extract_guards
from .tables.json_io import write_json
from .tables.strings import extract_strings
from .tables.technologies import extract_technologies
from .tables.transporters import extract_transporters

# Each entry: (table id, extractor function). The extractor receives
# `data.{}`'s bytes + parsed root and returns a JSON-serializable object.
_TABLE_EXTRACTORS: list[tuple[str, Callable[[bytes, DirNode], Any]]] = [
    ("commodities", extract_commodities),
    ("buildings", extract_buildings),
    ("transporters", extract_transporters),
    ("bandits", extract_bandits),
    ("guards", extract_guards),
    ("abilities", extract_abilities),
    ("technologies", extract_technologies),
    ("episodes", extract_episodes),
    ("events", extract_events),
]

# Building id placed at every region's headquarters marker (per
# `documentation/scripts/te_map.py`'s `elem.regi.spec` field, always 'head').
_HQ_BUILDING_ID = "head"


def _prompt_for_game_directory() -> GameDirectory:
    while True:
        raw = input("Path to your Trade Empires installation directory: ").strip().strip('"')
        if not raw:
            continue
        game_dir = find_game_directory(Path(raw))
        if game_dir is not None:
            return game_dir
        print(f"  '{raw}' doesn't look like a Trade Empires installation "
              "(expected to find 'Trade Empires.exe' and 'Data\\data.{}').")


def _resolve_game_directory(arg: str | None) -> GameDirectory:
    if arg is not None:
        game_dir = find_game_directory(Path(arg))
        if game_dir is None:
            print(f"error: '{arg}' doesn't look like a Trade Empires installation "
                  "(expected to find 'Trade Empires.exe' and 'Data\\data.{}').",
                  file=sys.stderr)
            sys.exit(1)
        return game_dir
    return _prompt_for_game_directory()


def _extract_tables(data: bytes, root: DirNode, output_dir: Path,
                    bldg_data: bytes | None = None,
                    bldg_root: DirNode | None = None) -> tuple[list[TableEntry], dict[str, Any]]:
    tables_dir = output_dir / "tables"
    tables_dir.mkdir(parents=True, exist_ok=True)

    entries = []
    results: dict[str, Any] = {}
    for table_id, extractor in _TABLE_EXTRACTORS:
        if table_id == "buildings" and bldg_data is not None and bldg_root is not None:
            table = extractor(data, root, bldg_data, bldg_root)
        else:
            table = extractor(data, root)
        results[table_id] = table
        relative_path = Path("tables") / f"{table_id}.json"
        write_json(output_dir / relative_path, table)
        entries.append(TableEntry(id=table_id, file=str(relative_path).replace("\\", "/")))
    return entries, results


def _extract_building_sprite(bldg_data: bytes, bldg_root: DirNode, buildings: dict[str, dict[str, Any]],
                               culture: str, building_id: str, output_dir: Path) -> SpriteEntry | None:
    building = buildings.get(f"{culture}.{building_id}")
    if building is None:
        return None

    path = find_building_sprite_path(bldg_root, culture, building_id, building.get("look"))
    if path is None:
        return None

    leaf = find_leaf(bldg_root, path)
    if leaf is None:
        return None

    sprite = decode_sprite(bldg_data, leaf.abs_off, leaf.size)
    if sprite is None:
        return None

    sprite_id = f"bldg.{culture}.{building_id}"
    sprites_dir = output_dir / "sprites"
    sprites_dir.mkdir(parents=True, exist_ok=True)
    relative_path = Path("sprites") / f"{sprite_id}.png"
    write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)

    return SpriteEntry(id=sprite_id, file=str(relative_path).replace("\\", "/"),
                        width=sprite.width, height=sprite.height,
                        anchor_x=sprite.anchor_x, anchor_y=sprite.anchor_y)


def _discover_maps(
    episodes: dict[str, dict[str, Any]],
    maps_dir: Path,
) -> list[tuple[str, str, str, dict[str, Any]]]:
    """Discovers extractable maps from the episodes table and Maps/ directory.

    Iterates every episode's regions in order, looks for a corresponding
    `<file_stem>.{}` file, and returns one entry per found file:
      (map_id, episode_id, file_stem, episode_region_dict)

    `map_id` is `<ep_id>_<region_id>` (e.g. `ep01_chin`, `ep02_meso`).
    `episode_region_dict` is the matching entry from `episodes[ep]["regions"]`
    (used to supply the region's canonical id/name to `extract_map`).

    Sorted deterministically by episode then region order; skips any region
    whose map file does not exist on disk.
    """
    result: list[tuple[str, str, str, dict[str, Any]]] = []
    for ep_id, ep_data in sorted(episodes.items()):
        for region in ep_data.get("regions", []):
            file_stem = region.get("file", "")
            region_id = region.get("id", "")
            if not file_stem or not region_id:
                continue
            map_file = maps_dir / f"{file_stem}.{{}}"
            if not map_file.is_file():
                continue
            map_id = f"{ep_id}_{region_id}"
            result.append((map_id, ep_id, file_stem, region))
    return result


def _extract_maps(
    game_dir: GameDirectory,
    data_data: bytes,
    data_root: DirNode,
    bldg_data: bytes,
    bldg_root: DirNode,
    buildings: dict[str, dict[str, Any]],
    episodes: dict[str, dict[str, Any]],
    output_dir: Path,
) -> tuple[list[MapEntry], list[SpriteEntry]]:
    maps_dir = output_dir / "maps"
    maps_dir.mkdir(parents=True, exist_ok=True)

    discovered = _discover_maps(episodes, game_dir.maps_dir)
    if not discovered:
        print("  warning: no map files found in Maps/ directory")

    # Pre-scan: find the first non-empty regi.cult in each episode so that
    # "secondary" regions (no regi element → no player HQ) can inherit the
    # episode's cultural palette instead of falling back to "".
    ep_home_culture: dict[str, str] = {}
    for _mid, ep_id, fstem, _reg in discovered:
        if ep_id in ep_home_culture:
            continue
        try:
            mdata, mfoot = load(game_dir.maps_dir / f"{fstem}.{{}}")
            mroot = parse_tree(mdata, mfoot)
            cult = get_map_culture(mdata, mroot)
            if cult:
                ep_home_culture[ep_id] = cult
        except Exception:
            pass

    map_entries = []
    sprite_entries: dict[str, SpriteEntry] = {}

    for map_id, episode, file_stem, ep_region in discovered:
        map_data, map_footer = load(game_dir.maps_dir / f"{file_stem}.{{}}")
        map_root = parse_tree(map_data, map_footer)

        # Pass only the single matching region so extract_map picks the right
        # id/name for episode_regions[0] without being confused by sibling regions.
        result = extract_map(map_data, map_root, map_id=map_id, episode=episode,
                              episode_regions=[ep_region], data_data=data_data, data_root=data_root,
                              fallback_culture=ep_home_culture.get(episode, ""))

        relative_path = Path("maps") / f"{map_id}.json"
        write_json(output_dir / relative_path, result)
        map_entries.append(MapEntry(id=map_id, file=str(relative_path).replace("\\", "/")))

        for region in result["regions"]:
            culture = region["culture_set"]
            sprite_id = f"bldg.{culture}.{_HQ_BUILDING_ID}"
            if sprite_id in sprite_entries:
                continue
            sprite_entry = _extract_building_sprite(bldg_data, bldg_root, buildings, culture,
                                                       _HQ_BUILDING_ID, output_dir)
            if sprite_entry is not None:
                sprite_entries[sprite_id] = sprite_entry

    return map_entries, list(sprite_entries.values())


def run(game_dir: GameDirectory, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Extracting from: {game_dir.path}")
    print(f"Writing output to: {output_dir}")

    data_data, data_footer = load(game_dir.data_dir / "data.{}")
    data_root = parse_tree(data_data, data_footer)

    # Load bldg.{} before tables so extract_buildings can read futx/futy footprints.
    bldg_data, bldg_footer = load(game_dir.data_dir / "bldg.{}")
    bldg_root = parse_tree(bldg_data, bldg_footer)

    table_entries, tables = _extract_tables(data_data, data_root, output_dir,
                                             bldg_data=bldg_data, bldg_root=bldg_root)
    for entry in table_entries:
        print(f"  wrote table '{entry.id}' -> {entry.file}")

    print("Discovering and extracting maps...")

    map_entries, sprite_entries = _extract_maps(game_dir, data_data, data_root, bldg_data, bldg_root,
                                                  tables["buildings"], tables["episodes"], output_dir)
    print(f"  {len(map_entries)} maps extracted")
    for entry in map_entries:
        print(f"    {entry.id} -> {entry.file}")

    print("Extracting terrain textures (all cultures)...")
    m_ui_data, m_ui_footer = load(game_dir.data_dir / "m_ui,u.{}")
    m_ui_root = parse_tree(m_ui_data, m_ui_footer)
    terrain_entries = extract_terrain_textures_all_cultures(m_ui_data, m_ui_root, output_dir)
    sprite_entries += terrain_entries
    table_entries.append(TableEntry(id="terrain_textures", file="tables/terrain_textures.json"))
    print(f"  {len(terrain_entries)} terrain sprites written")

    print("Extracting bridge sprites...")
    bridge_entries = extract_bridge_sprites(m_ui_data, m_ui_root, output_dir)
    sprite_entries += bridge_entries
    table_entries.append(TableEntry(id="bridges", file="tables/bridges.json"))
    print(f"  {len(bridge_entries)} bridge sprites written")

    print("Extracting decoration sprites...")
    flor_data, flor_footer = load(game_dir.data_dir / "flor.{}")
    flor_root = parse_tree(flor_data, flor_footer)
    flor_entries = extract_decoration_sprites(flor_data, flor_root, output_dir)
    sprite_entries += flor_entries
    print(f"  {len(flor_entries)} decoration sprites written")

    # a_ui,6.{}: main game UI sprites in RGB565 (181 leaves across 26 dialog groups).
    # a_ui,5.{} holds the same dialog-group art in RGB555 PLUS the exclusive
    # 'main/' group (main-menu background, button highlights, logos).
    print("Extracting a_ui sprites (this takes a while)...")
    a_ui_data, a_ui_footer = load(game_dir.data_dir / "a_ui,6.{}")
    a_ui_root = parse_tree(a_ui_data, a_ui_footer)
    a_ui_entries = extract_ui_sprites("a_ui", a_ui_data, a_ui_root, output_dir)
    sprite_entries += a_ui_entries

    print("Extracting main menu sprites (a_ui,5.{}/main/)...")
    a_ui5_data, a_ui5_footer = load(game_dir.data_dir / "a_ui,5.{}")
    a_ui5_root = parse_tree(a_ui5_data, a_ui5_footer)
    main_entries = extract_main_menu_sprites(a_ui5_data, a_ui5_root, output_dir)
    sprite_entries += main_entries

    # d_ui,5.{}: debug/dev UI sprites in ARGB4444 with per-pixel alpha (379 leaves).
    # d_ui,6.{} is byte-identical to d_ui,5 -- skipped.
    print("Extracting d_ui sprites (this takes a while)...")
    d_ui_data, d_ui_footer = load(game_dir.data_dir / "d_ui,5.{}")
    d_ui_root = parse_tree(d_ui_data, d_ui_footer)
    d_ui_entries = extract_ui_sprites("d_ui", d_ui_data, d_ui_root, output_dir)
    sprite_entries += d_ui_entries

    # font.{}: glyph atlases and metrics for all four faces.
    print("Extracting fonts...")
    font_data, font_footer = load(game_dir.data_dir / "font.{}")
    font_root = parse_tree(font_data, font_footer)
    font_stems = extract_fonts(font_data, font_root, output_dir)
    print(f"  {len(font_stems)} font face/size pairs written")

    # text.{}: UI / localization string table (dotted-key -> string), e.g.
    # "cons.labl.titl" -> "C O N S T R U C T I O N". Loaded as the `strings`
    # table; UI code looks strings up by key.
    print("Extracting UI strings...")
    text_data, text_footer = load(game_dir.data_dir / "text.{}")
    text_root = parse_tree(text_data, text_footer)
    strings = extract_strings(text_data, text_root)
    write_json(output_dir / "tables" / "strings.json", strings)
    table_entries.append(TableEntry(id="strings", file="tables/strings.json"))
    print(f"  {len(strings)} strings written")

    print("Writing manifest...")
    manifest = Manifest(sprites=sprite_entries, tables=table_entries, maps=map_entries)
    manifest_path = write_manifest(output_dir, manifest)
    print(f"  {manifest_path.name} ({len(sprite_entries)} sprites, {len(table_entries)} tables, {len(map_entries)} maps)")

    print("Done.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--game-dir", help="Path to your Trade Empires installation directory")
    parser.add_argument("--output", default="game_data",
                         help="Output directory for extracted data (default: ./game_data)")
    args = parser.parse_args(argv)

    game_dir = _resolve_game_directory(args.game_dir)
    output_dir = Path(args.output)

    try:
        run(game_dir, output_dir)
    except (ValueError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
