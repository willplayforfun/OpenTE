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
from .maps.region import extract_map
from .sprites.buildings import find_building_sprite_path
from .sprites.decorations import extract_decoration_sprites
from .sprites.sprite import decode_sprite, find_leaf, write_png_rgba
from .sprites.terrain import extract_terrain_textures
from .sprites.ui import extract_ui_sprites
from .tables.abilities import extract_abilities
from .tables.bandits import extract_bandits
from .tables.buildings import extract_buildings
from .tables.commodities import extract_commodities
from .tables.episodes import extract_episodes
from .tables.events import extract_events
from .tables.guards import extract_guards
from .tables.json_io import write_json
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

# Maps to extract for Stage 1 -- (map id, episode id, "Maps/<file>.{}" stem).
_MAPS: list[tuple[str, str, str]] = [
    ("ep01_china", "ep01", "ep01 China"),
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


def _extract_tables(data: bytes, root: DirNode, output_dir: Path) -> tuple[list[TableEntry], dict[str, Any]]:
    tables_dir = output_dir / "tables"
    tables_dir.mkdir(parents=True, exist_ok=True)

    entries = []
    results: dict[str, Any] = {}
    for table_id, extractor in _TABLE_EXTRACTORS:
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


def _extract_maps(game_dir: GameDirectory, data_data: bytes, data_root: DirNode,
                   bldg_data: bytes, bldg_root: DirNode, buildings: dict[str, dict[str, Any]],
                   episodes: dict[str, dict[str, Any]], output_dir: Path) -> tuple[list[MapEntry], list[SpriteEntry]]:
    maps_dir = output_dir / "maps"
    maps_dir.mkdir(parents=True, exist_ok=True)

    map_entries = []
    sprite_entries: dict[str, SpriteEntry] = {}
    for map_id, episode, file_stem in _MAPS:
        map_data, map_footer = load(game_dir.maps_dir / f"{file_stem}.{{}}")
        map_root = parse_tree(map_data, map_footer)

        episode_regions = episodes.get(episode, {}).get("regions", [])
        result = extract_map(map_data, map_root, map_id=map_id, episode=episode,
                              episode_regions=episode_regions, data_data=data_data, data_root=data_root)

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

    table_entries, tables = _extract_tables(data_data, data_root, output_dir)
    for entry in table_entries:
        print(f"  wrote table '{entry.id}' -> {entry.file}")

    bldg_data, bldg_footer = load(game_dir.data_dir / "bldg.{}")
    bldg_root = parse_tree(bldg_data, bldg_footer)

    map_entries, sprite_entries = _extract_maps(game_dir, data_data, data_root, bldg_data, bldg_root,
                                                  tables["buildings"], tables["episodes"], output_dir)

    m_ui_data, m_ui_footer = load(game_dir.data_dir / "m_ui,u.{}")
    m_ui_root = parse_tree(m_ui_data, m_ui_footer)
    sprite_entries += extract_terrain_textures(m_ui_data, m_ui_root, output_dir)
    table_entries.append(TableEntry(id="terrain_textures", file="tables/terrain_textures.json"))

    flor_data, flor_footer = load(game_dir.data_dir / "flor.{}")
    flor_root = parse_tree(flor_data, flor_footer)
    sprite_entries += extract_decoration_sprites(flor_data, flor_root, output_dir)

    # a_ui,6.{}: main game UI sprites in RGB565 (181 leaves across 26 dialog groups).
    # a_ui,5.{} is byte-for-byte identical art in RGB555 -- skipped.
    a_ui_data, a_ui_footer = load(game_dir.data_dir / "a_ui,6.{}")
    a_ui_root = parse_tree(a_ui_data, a_ui_footer)
    a_ui_entries = extract_ui_sprites("a_ui", a_ui_data, a_ui_root, output_dir)
    print(f"  extracted {len(a_ui_entries)} a_ui sprites")
    sprite_entries += a_ui_entries

    # d_ui,5.{}: debug/dev UI sprites in ARGB4444 with per-pixel alpha (379 leaves).
    # d_ui,6.{} is byte-identical to d_ui,5 -- skipped.
    d_ui_data, d_ui_footer = load(game_dir.data_dir / "d_ui,5.{}")
    d_ui_root = parse_tree(d_ui_data, d_ui_footer)
    d_ui_entries = extract_ui_sprites("d_ui", d_ui_data, d_ui_root, output_dir)
    print(f"  extracted {len(d_ui_entries)} d_ui sprites")
    sprite_entries += d_ui_entries

    for entry in map_entries:
        print(f"  wrote map '{entry.id}' -> {entry.file}")
    for entry in sprite_entries:
        print(f"  wrote sprite '{entry.id}' ({entry.width}x{entry.height}) -> {entry.file}")

    manifest = Manifest(sprites=sprite_entries, tables=table_entries, maps=map_entries)
    manifest_path = write_manifest(output_dir, manifest)
    print(f"  wrote manifest -> {manifest_path.name}")

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
