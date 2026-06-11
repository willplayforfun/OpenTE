#!/usr/bin/env python3
"""OpenTE extractor CLI.

Reads data and sprites from a user's own copy of Trade Empires and writes
OpenTE's `game_data/` directory (JSON tables + PNG sprites + manifest).

Usage::

    python -m extractor.main [--game-dir PATH] [--output PATH]

If `--game-dir` is omitted (or doesn't point at a valid installation), the
user is prompted interactively. If `--output` is omitted, output is written
to `./game_data` -- run this next to (or pass `--output` pointing at) the
OpenTE game executable so it can find its data automatically (see
`core::find_game_data_dir` in `game/src/core/paths.cpp`).

This is the toolchain-spike version: it extracts one building sprite and the
`comm` (commodity) table. As more of `OpenTE/spec/data-model.md`'s tables and
asset types are needed, add more `tables/*.py` / sprite exports here.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from .containers.container import find_child, load, parse_tree
from .game_directory import GameDirectory, find_game_directory
from .manifest import Manifest, SpriteEntry, TableEntry, write_manifest
from .sprites.sprite import decode_sprite, find_leaf, write_png_rgba
from .tables.commodities import extract_commodities
from .tables.json_io import write_json

# A representative building sprite used to validate the toolchain end-to-end:
# the Chinese Bazaar's "terrain" (ground-level) sprite.
_SPIKE_SPRITE_PATH = ["cher", "cbaz", "rot0", "terr", "pla0"]
_SPIKE_SPRITE_ID = "bldg.cher.cbaz"


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


def _extract_spike_sprite(data: bytes, root, output_dir: Path) -> SpriteEntry:
    leaf = find_leaf(root, _SPIKE_SPRITE_PATH)
    if leaf is None:
        raise ValueError(f"sprite {'/'.join(_SPIKE_SPRITE_PATH)} not found in bldg.{{}}")

    sprite = decode_sprite(data, leaf.abs_off, leaf.size)
    if sprite is None:
        raise ValueError(f"leaf at {'/'.join(_SPIKE_SPRITE_PATH)} is not a recognized sprite")

    sprites_dir = output_dir / "sprites"
    sprites_dir.mkdir(parents=True, exist_ok=True)
    relative_path = Path("sprites") / f"{_SPIKE_SPRITE_ID}.png"
    write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)

    return SpriteEntry(id=_SPIKE_SPRITE_ID, file=str(relative_path).replace("\\", "/"),
                        width=sprite.width, height=sprite.height)


def _extract_commodities(data: bytes, root, output_dir: Path) -> TableEntry:
    commodities = extract_commodities(data, root)

    tables_dir = output_dir / "tables"
    tables_dir.mkdir(parents=True, exist_ok=True)
    relative_path = Path("tables") / "commodities.json"
    write_json(output_dir / relative_path, commodities)

    return TableEntry(id="commodities", file=str(relative_path).replace("\\", "/"))


def run(game_dir: GameDirectory, output_dir: Path) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)

    print(f"Extracting from: {game_dir.path}")
    print(f"Writing output to: {output_dir}")

    bldg_data, bldg_footer = load(game_dir.data_dir / "bldg.{}")
    bldg_root = parse_tree(bldg_data, bldg_footer)
    sprite_entry = _extract_spike_sprite(bldg_data, bldg_root, output_dir)
    print(f"  wrote sprite '{sprite_entry.id}' "
          f"({sprite_entry.width}x{sprite_entry.height}) -> {sprite_entry.file}")

    data_data, data_footer = load(game_dir.data_dir / "data.{}")
    data_root = parse_tree(data_data, data_footer)
    table_entry = _extract_commodities(data_data, data_root, output_dir)
    print(f"  wrote table '{table_entry.id}' -> {table_entry.file}")

    manifest = Manifest(sprites=[sprite_entry], tables=[table_entry])
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
