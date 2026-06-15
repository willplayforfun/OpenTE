"""Extracts `flor.{}` ground-decoration sprites (B11).

`flor.{}` has one top-level directory per culture set (`chin`, `foly`,
`indi`, `pers`), each holding a per-culture `ap01` palette plus a mix of
sprite leaves. All four culture sets tag their decoration leaves with raw
little-endian int32 indices (`0`, `1`, `2`, ...) matching `elem.flop.spec`
1:1 -- confirmed by cross-checking all 74 map files:

  - `chin`: indices 0-6  (7 leaves)   used by ep01 China + Tutorial maps
  - `indi`: indices 0-21 (22 leaves)  used by ep02-ep05 Indian/Indus maps
  - `pers`: indices 0-21 (22 leaves)  used by ep02-ep04 Persian/Assyrian maps
  - `foly`: indices 0-19 (20 leaves)  not used by any current map, extracted
                                       for completeness

`foly` also has `rk00`-`rk12` ASCII-tagged rock leaves; their int32
representation falls well above `_MAX_INDEX` so they are filtered out
by the existing guard. No map currently uses rock decorations.

The last child in each culture dir is the container format's "+1 spillover"
entry (CLAUDE.md gotcha 1) whose tag is the *next* sibling's culture name;
its decoded int32 is also > `_MAX_INDEX` and is filtered safely.
"""
from __future__ import annotations

import struct
from pathlib import Path

from ..containers.container import DirNode, find_child
from ..manifest import SpriteEntry
from .sprite import decode_sprite, write_png_rgba

# All four culture sets use raw-int-tagged leaves indexed by `elem.flop.spec`.
_INDEXED_CULTURES = ("chin", "foly", "indi", "pers")

# Raw int32 tags outside this range are ASCII tags (e.g. the table's "+1
# spillover" trailing directory header, tagged with the *next* table's name)
# rather than decoration indices.
_MAX_INDEX = 99


def extract_decoration_sprites(flor_data: bytes, flor_root: DirNode, output_dir: Path) -> list[SpriteEntry]:
    sprites_dir = output_dir / "sprites" / "flor"
    sprites_dir.mkdir(parents=True, exist_ok=True)

    entries: list[SpriteEntry] = []
    for culture in _INDEXED_CULTURES:
        culture_entry = find_child(flor_root, culture)
        if culture_entry is None or culture_entry.dir is None:
            continue

        for child in culture_entry.dir.children:
            if child.kind != "leaf":
                continue
            tag_bytes = child.tag.encode("latin1", "replace")
            if len(tag_bytes) != 4:
                continue
            index = struct.unpack("<i", tag_bytes[::-1])[0]
            if not (0 <= index <= _MAX_INDEX):
                continue

            sprite = decode_sprite(flor_data, child.abs_off, child.size)
            if sprite is None:
                continue

            sprite_id = f"flor.{culture}.{index}"
            relative_path = Path("sprites") / "flor" / f"{sprite_id}.png"
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
            entries.append(SpriteEntry(id=sprite_id, file=str(relative_path).replace("\\", "/"),
                                         width=sprite.width, height=sprite.height,
                                         anchor_x=sprite.anchor_x, anchor_y=sprite.anchor_y))

    return entries
