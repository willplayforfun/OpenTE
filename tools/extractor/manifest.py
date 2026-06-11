"""Writes `game_data/manifest.json`, the contract between the extractor and
the game executable.

`format_version` lets the game detect a stale or incompatible `game_data/`
directory (e.g. produced by an older extractor) and prompt the user to
re-run extraction. Bump it whenever the layout or schema of `game_data/`
changes in a way the game needs to know about.
"""
from __future__ import annotations

import json
from dataclasses import asdict, dataclass, field
from pathlib import Path

FORMAT_VERSION = 1


@dataclass
class SpriteEntry:
    """One sprite asset, referenced by `id` (its source path in the
    original container, dotted, e.g. "bldg.cher.cbaz") and `file` (its PNG
    path relative to `game_data/`).

    `anchor_x`/`anchor_y` are the sprite leaf's anchor offsets (pixels from
    the sprite's top-left corner to its placement point, e.g. the tile
    origin), as decoded by `sprites.sprite.decode_sprite`. Default to 0 for
    assets (terrain tiles) that don't have a meaningful anchor."""

    id: str
    file: str
    width: int
    height: int
    anchor_x: int = 0
    anchor_y: int = 0


@dataclass
class TableEntry:
    """One data table, exported to a JSON file relative to `game_data/`."""

    id: str
    file: str


@dataclass
class MapEntry:
    """One map, exported to a JSON file relative to `game_data/`."""

    id: str
    file: str


@dataclass
class Manifest:
    sprites: list[SpriteEntry] = field(default_factory=list)
    tables: list[TableEntry] = field(default_factory=list)
    maps: list[MapEntry] = field(default_factory=list)
    format_version: int = FORMAT_VERSION


def write_manifest(output_dir: Path, manifest: Manifest) -> Path:
    """Writes `manifest` to `<output_dir>/manifest.json` and returns its path."""
    path = output_dir / "manifest.json"
    path.write_text(json.dumps(asdict(manifest), indent=2), encoding="utf-8")
    return path
