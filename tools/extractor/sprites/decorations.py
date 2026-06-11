"""Extracts `flor.{}` ground-decoration sprites (B11).

`flor.{}` has one top-level directory per culture set (`chin`, `foly`,
`indi`, `pers`), each holding a per-culture `ap01` palette plus a mix of
sprite leaves. For `chin` (the only culture used by the `ep01_china` map),
the 7 sprite leaves are tagged with raw little-endian int32 indices `0`-`6`
(see CLAUDE.md gotcha 8) that match `elem.flop.spec` 1:1 -- confirmed by
cross-checking `ep01 China.{}`'s `elem.flop` records (488 records, all
`cult=chin`, `spec` in `0..6`, exactly matching `chin`'s 7 leaves).

The other cultures' decoration sets (`foly`/`indi`/`pers`, including the
`rkNN` "rock" leaves under `foly`) use a different convention that hasn't
been confirmed yet and aren't extracted here.
"""
from __future__ import annotations

import struct
from pathlib import Path

from ..containers.container import DirNode, find_child
from ..manifest import SpriteEntry
from .sprite import decode_sprite, write_png_rgba

# Cultures whose decoration leaves are raw-int-tagged and directly indexed
# by `elem.flop.spec` (see module docstring).
_INDEXED_CULTURES = ("chin",)

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
