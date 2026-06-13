"""Extracts terrain texture-page, shore-overlay, and map-edge textures from
`m_ui,u.{}` (B7/B15), plus the `terrain_textures.json` palette table.

`m_ui,u.{}`'s `terr/` directory has 93 leaves, each a 256x256 RGBA8888
`bg6a` sprite (e.g. `terr/deep`, `terr/ts11`, `terr/edge`, `terr/coa0`).

Per `OpenTE/implementation/terrain-blending-plan.md` Stage A, the original
picks a per-tile texture from a 13-slot "texture page" table built from one
`terr/sets/<N>` palette record (`documentation/extracted/
data_catalog_terr_sets.txt`, B15 Round 36). `_TEXTURE_PAGE_SOURCES` below is
that 13-slot table resolved for palette `N=16` ("chi1" -- matches
`ep01_china`'s culture set), per Stage A.3/A.4 as corrected by the full
disassembly of `fcn.0x466790` (`documentation/extracted/
exe_b15_round36_init.txt` lines 170-470):

```
index:  1     2     3     4     5     6     7     8     9     10    11    12    13
field:  deep  seas  alps  bld0  bld1  bld2  hill  mntn  undr  soil  ?     dsr0  dsr1
chi1:   deep  seas  snow  ts17  ts16  ts15  chhi  chmo  ts14  ts18  ?     sand  sand
```

Two corrections to the original Stage A.3 table:

- **Index 1 is `deep`, not `hidd`.** `hidd` is written to a *scratch local*
  at the top of `fcn.0x466790`, then passed to the per-field copy helper
  alongside each `terr/sets/<N>` lookup -- it's a fallback substituted when a
  field resolves to the `none` sentinel, not a table slot. `terr/hidd` is a
  starfield; per the user (2026-06-12), it's a tiling texture drawn *behind*
  the terrain (visible past the map edges), unrelated to the 13-slot
  per-tile table. There is no "fog of war"/"unexplored" concept here.
- **Index 11 has no source in `fcn.0x466790` at all** -- the 28-byte-stride
  table write sequence goes `...soil (slot 10) -> [slot 11 skipped] -> dsr0
  (slot 12) -> dsr1 (slot 13)`, with no write to the slot-11 offset
  (`esi+0x134`). `tran` (chi1 -> `trch`) is a *separate* single-texture slot
  (`esi+0x1b3c`, grouped with `coa0`/`coa1`/`edge`/per-culture decoration
  refs), not index 11's source -- matches `trch`'s visual content (a 4x4
  grid of distinct sparkle/particle frames, not a tileable ground texture;
  likely a transport-route decoration atlas). **Index 11 is left unmapped
  below** (empty sprite id, renderer falls back to flat shading for any tile
  with `texture_index == 11`) -- TODO: dig further into whether/how slot 11
  is ever actually selected by real `mapp.terr` data before assuming it's
  truly dead.

Each texture page is extracted at **full native resolution (256x256)**, no
downsample, no diamond mask. Per B15 Round 34, the renderer's continuous
`uv = (col/8, row/16)` scheme means each page tiles seamlessly across an
8x16-tile region of the map (32x16px per tile, 2x-magnified onto the 64x32
screen diamond) -- the texture must stay at its native tileable resolution
for that math to land on a sensible sample size (a 64x32-downsampled page
would only yield an 8x2px sample per tile, magnified 8x).

`terr/coa0`/`terr/coa1` (Stage C.1, shore-overlay atlases) and `terr/edge`
are extracted at full native resolution (256x256), unscaled/unmasked.
"""
from __future__ import annotations

from pathlib import Path

from ..containers.container import DirNode, find_child
from ..manifest import SpriteEntry
from ..tables.json_io import write_json
from .sprite import decode_sprite, find_leaf, write_png_rgba

# 13-slot texture-page table (index 1-13 -> `terr/<tag>`), resolved for the
# "chi1" palette (`terr/sets/16`) -- see module docstring. Index 11 has no
# confirmed source (see docstring) and is left unmapped.
_TEXTURE_PAGE_SOURCES: dict[int, str] = {
    1: "deep",
    2: "seas",
    3: "snow",
    4: "ts17",
    5: "ts16",
    6: "ts15",
    7: "chhi",
    8: "chmo",
    9: "ts14",
    10: "ts18",
    12: "sand",
    13: "sand",
}

_EDGE_TEXTURE_TAG = "edge"
_EDGE_SPRITE_ID = "terrain.edge"

# Edge-blend atlas (Stage B, B15 Round 40): a 4x4-cell dithered-dissolve
# atlas, one per culture palette -- "trch" is `terr/sets/16`'s ("chi1")
# `tran` field (`data_catalog_terr_sets.txt`).
_TRAN_TEXTURE_TAG = "trch"
_TRAN_SPRITE_ID = "terrain.tran"

# `terr/trch` decodes fully opaque (alpha==255 everywhere) with near-black
# (RGB <= ~0x1F) "dither dot" pixels forming the dissolve pattern -- there is
# no usable alpha gradient to blend with. Recut it as a hard alpha mask
# (dot pixels opaque, near-black background fully transparent) so the
# edge-blend pass (drawn with SDL_BLENDMODE_BLEND) shows only the dissolve
# dots, letting the base terrain pass show through everywhere else.
_TRAN_DISSOLVE_THRESHOLD = 8


def _apply_dissolve_mask(rgba: bytes, threshold: int = _TRAN_DISSOLVE_THRESHOLD) -> bytes:
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        out[i + 3] = 255 if max(out[i], out[i + 1], out[i + 2]) > threshold else 0
    return bytes(out)

# Shore-overlay atlases (Stage C.1).
_SHORE_ATLAS_TAGS = {
    "coa0": "terrain.coa0",
    "coa1": "terrain.coa1",
}

_TERRAIN_TEXTURES_TABLE_PATH = Path("tables") / "terrain_textures.json"


def extract_terrain_textures(m_ui_data: bytes, m_ui_root: DirNode,
                              output_dir: Path) -> list[SpriteEntry]:
    """Returns `SpriteEntry` list for the 13 terrain texture pages, the
    shore-overlay atlases, and the map-edge texture; writes their PNGs under
    `<output_dir>/sprites/terrain/` and the resolved palette table to
    `<output_dir>/tables/terrain_textures.json`."""
    terr_entry = find_child(m_ui_root, "terr")
    if terr_entry is None or terr_entry.dir is None:
        return []
    terr_root = terr_entry.dir

    sprites_dir = output_dir / "sprites" / "terrain"
    sprites_dir.mkdir(parents=True, exist_ok=True)

    entries: list[SpriteEntry] = []
    page_sprite_ids: list[str] = []

    for index in range(1, 14):
        tag = _TEXTURE_PAGE_SOURCES.get(index)
        leaf = find_leaf(terr_root, [tag]) if tag is not None else None
        if leaf is None:
            page_sprite_ids.append("")
            continue
        sprite = decode_sprite(m_ui_data, leaf.abs_off, leaf.size)
        if sprite is None:
            page_sprite_ids.append("")
            continue
        sprite_id = f"terrain.page{index:02d}"
        relative_path = Path("sprites") / "terrain" / f"{sprite_id}.png"
        write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
        entries.append(SpriteEntry(id=sprite_id, file=str(relative_path).replace("\\", "/"),
                                     width=sprite.width, height=sprite.height))
        page_sprite_ids.append(sprite_id)

    for tag, sprite_id in _SHORE_ATLAS_TAGS.items():
        leaf = find_leaf(terr_root, [tag])
        if leaf is None:
            continue
        sprite = decode_sprite(m_ui_data, leaf.abs_off, leaf.size)
        if sprite is None:
            continue
        relative_path = Path("sprites") / "terrain" / f"{sprite_id}.png"
        write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
        entries.append(SpriteEntry(id=sprite_id, file=str(relative_path).replace("\\", "/"),
                                     width=sprite.width, height=sprite.height))

    edge_leaf = find_leaf(terr_root, [_EDGE_TEXTURE_TAG])
    if edge_leaf is not None:
        sprite = decode_sprite(m_ui_data, edge_leaf.abs_off, edge_leaf.size)
        if sprite is not None:
            relative_path = Path("sprites") / "terrain" / f"{_EDGE_SPRITE_ID}.png"
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
            entries.append(SpriteEntry(id=_EDGE_SPRITE_ID, file=str(relative_path).replace("\\", "/"),
                                         width=sprite.width, height=sprite.height))

    tran_sprite_id = ""
    tran_leaf = find_leaf(terr_root, [_TRAN_TEXTURE_TAG])
    if tran_leaf is not None:
        sprite = decode_sprite(m_ui_data, tran_leaf.abs_off, tran_leaf.size)
        if sprite is not None:
            relative_path = Path("sprites") / "terrain" / f"{_TRAN_SPRITE_ID}.png"
            rgba = _apply_dissolve_mask(sprite.rgba)
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, rgba)
            entries.append(SpriteEntry(id=_TRAN_SPRITE_ID, file=str(relative_path).replace("\\", "/"),
                                         width=sprite.width, height=sprite.height))
            tran_sprite_id = _TRAN_SPRITE_ID

    write_json(output_dir / _TERRAIN_TEXTURES_TABLE_PATH, {"pages": page_sprite_ids, "tran": tran_sprite_id})

    return entries
