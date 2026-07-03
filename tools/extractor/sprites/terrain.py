"""Extracts terrain texture-page, shore-overlay, and map-edge textures from
`m_ui,u.{}` (B7/B15), plus the `terrain_textures.json` palette table.

`m_ui,u.{}`'s `terr/` directory has 93 leaves, each a 256x256 RGBA8888
`bg6a` sprite (e.g. `terr/deep`, `terr/ts11`, `terr/edge`, `terr/coa0`).

Per `OpenTE/implementation/terrain-blending-plan.md` Stage A, the original
picks a per-tile texture from a 13-slot "texture page" table built from one
`terr/sets/<N>` palette record (`documentation/extracted/
data_catalog_terr_sets.txt`, B15 Round 36).  The 13-slot ordering was
confirmed by full disassembly of `fcn.0x466790` (`documentation/extracted/
exe_b15_round36_init.txt` lines 170-470): the function writes each palette
field to a destination struct at a fixed stride of 0x1c (28) bytes/slot
starting at `esi+0x1c`, and slot 11 (`esi+0x134`) is **never written** --
the write sequence jumps directly from soil (slot 10, `esi+0x118`) to
dsr0 (slot 12, `esi+0x150`).

Slot -> struct-field mapping (applies identically to every palette):
  1=deep  2=seas  3=alps  4=bld0  5=bld1  6=bld2  7=hill  8=mntn
  9=undr  10=soil  [11 never written]  12=dsr0  13=dsr1

`tran` is at a separate offset (`esi+0x1b3c`) outside the 13-slot table.

`_CULTURES` below encodes this for the four named palettes
(`documentation/extracted/data_catalog_terr_sets.txt`, palette indices
0/15/16/17 = ind1/eur1/chi1/per1).  Index 11 is left unmapped for all
cultures (empty sprite id; renderer falls back to flat shading).

Sprite IDs use the form `terrain.<culture>.page<NN>` so all four cultures'
assets can coexist in the same manifest.  Shore atlases (`terrain.coa0`,
`terrain.coa1`), the map-edge texture (`terrain.edge`), and the starfield
background (`terrain.hidd`) are shared across all cultures and keep their
culture-free IDs.

`terrain_textures.json` schema:
  {
    "cultures": {
      "<culture>": {
        "pages": ["", "terrain.<culture>.page01", ..., "terrain.<culture>.page13"],
        "tran": "terrain.<culture>.tran",
        "hidd": "terrain.hidd"      // same string for every culture
      },
      ...
    }
  }
  "pages" is a 14-element list (index 0 unused/empty, indices 1-13 are the
  page sprite IDs; index 11 is always empty).

Each texture page is extracted at full native resolution (256x256).  Per
B15 Round 34, the renderer's continuous `uv = (col/8, row/16)` scheme
means each page tiles seamlessly across an 8x16-tile region -- the texture
must stay at native resolution for that math to land correctly.
"""
from __future__ import annotations

from pathlib import Path

from ..containers.container import DirNode, find_child
from ..manifest import SpriteEntry
from ..tables.json_io import write_json
from .sprite import decode_sprite, find_leaf, write_png_rgba

# Per-culture page sources.  Slot ordering confirmed by EXE fcn.0x466790
# disasm (exe_b15_round36_init.txt lines 170-470); palette indices and
# sprite tags from data_catalog_terr_sets.txt.
# Key: culture name.  Value: {pages: {slot->terr/<tag>}, tran: <tag>}.
_CULTURES: dict[str, dict] = {
    "chi1": {  # palette 16
        "pages": {
            1: "deep", 2: "seas", 3: "snow",
            4: "ts17", 5: "ts16", 6: "ts15",
            7: "chhi", 8: "chmo",
            9: "ts14", 10: "ts18",
            # slot 11: never written by fcn.0x466790
            12: "sand", 13: "sand",
        },
        "tran": "trch",
    },
    "eur1": {  # palette 15
        "pages": {
            1: "deep", 2: "seas", 3: "snow",
            4: "ts23", 5: "ts25", 6: "ts26",
            7: "chhi", 8: "bigm",
            9: "ts24", 10: "sand",
            12: "sand", 13: "sand",
        },
        "tran": "tr15",
    },
    "per1": {  # palette 17
        "pages": {
            1: "deep", 2: "seas", 3: "snow",
            4: "ts11", 5: "ts12", 6: "ts13",
            7: "chhi", 8: "bigm",
            9: "ts14", 10: "ts14",
            12: "dirt", 13: "sand",
        },
        "tran": "tr17",
    },
    "ind1": {  # palette 0
        "pages": {
            1: "deep", 2: "seas", 3: "snow",
            4: "ts19", 5: "ts20", 6: "ts21",
            7: "chhi", 8: "mntn",
            9: "ts22", 10: "ts22",
            12: "sand", 13: "sand",
        },
        "tran": "tr14",
    },
}

# Maps every culture code that appears in regi.cult to the named palette that
# holds its terrain sprites.  data_catalog_terr_sets.txt has only 4 named
# entries (ind1/eur1/chi1/per1 at indices 0/15/16/17); numbered variants
# (chi2, eur2-5, ind2-3, per2-3, mon1-2) differ only in city names / building
# sprites and share the same terrain art as their base palette.
_CULTURE_TO_BASE: dict[str, str] = {
    "chi1": "chi1",
    "chi2": "chi1",
    "eur1": "eur1",
    "eur2": "eur1",
    "eur3": "eur1",
    "eur4": "eur1",
    "eur5": "eur1",
    "ind1": "ind1",
    "ind2": "ind1",
    "ind3": "ind1",
    "per1": "per1",
    "per2": "per1",
    "per3": "per1",
    "mon1": "ind1",
    "mon2": "ind1",
}

_EDGE_TEXTURE_TAG = "edge"
_EDGE_SPRITE_ID = "terrain.edge"

_HIDD_TEXTURE_TAG = "hidd"
_HIDD_SPRITE_ID = "terrain.hidd"

# Shore-overlay atlases (Stage C.1) -- shared across all cultures.
_SHORE_ATLAS_TAGS = {
    "coa0": "terrain.coa0",
    "coa1": "terrain.coa1",
}

# Network-overlay atlas sub-tags per base culture (Stage D, trail-rendering-plan.md).
# Container layout: terr/trai/<subtag>, terr/cana/<subtag>, terr/rail/<subtag>.
# trail1/trail2 are the two 53-cell atlas pages for roads+trails (atlas pages 0xf4/0xf5).
# rail is page 0xf6 (drawn by the byte-2 connectivity network), canal page 0xfa
# (byte-3 network) -- see documentation/extracted/path-rendering-handoff.md §0.
_NETWORK_ATLAS_SUBTAGS: dict[str, dict[str, str]] = {
    "chi1": {"trail1": "chi1", "trail2": "chi2", "canal": "chi1", "rail": "chi1"},
    "eur1": {"trail1": "eur1", "trail2": "eur2", "canal": "eur1", "rail": "eur1"},
    "ind1": {"trail1": "ind1", "trail2": "ind2", "canal": "ind1", "rail": "ind1"},
    "per1": {"trail1": "per1", "trail2": "per2", "canal": "per1", "rail": "per1"},
}

# `tran` atlases decode fully opaque (alpha==255 everywhere) with near-black
# (RGB <= ~0x1F) dither-dot pixels.  Recut to hard alpha so the edge-blend
# pass (SDL_BLENDMODE_BLEND) shows only the dots.
_TRAN_DISSOLVE_THRESHOLD = 8

_TERRAIN_TEXTURES_TABLE_PATH = Path("tables") / "terrain_textures.json"


def _apply_dissolve_mask(rgba: bytes, threshold: int = _TRAN_DISSOLVE_THRESHOLD) -> bytes:
    out = bytearray(rgba)
    for i in range(0, len(out), 4):
        out[i + 3] = 255 if max(out[i], out[i + 1], out[i + 2]) > threshold else 0
    return bytes(out)


def extract_terrain_textures_all_cultures(
    m_ui_data: bytes,
    m_ui_root: DirNode,
    output_dir: Path,
) -> list[SpriteEntry]:
    """Extracts terrain texture pages, tran atlases, shore overlays, and the
    map-edge texture for all four named cultures (chi1/eur1/per1/ind1).

    Writes PNGs under `<output_dir>/sprites/terrain/` and the combined
    palette table to `<output_dir>/tables/terrain_textures.json`.
    Returns a `SpriteEntry` list for the manifest.
    """
    terr_entry = find_child(m_ui_root, "terr")
    if terr_entry is None or terr_entry.dir is None:
        return []
    terr_root = terr_entry.dir

    sprites_dir = output_dir / "sprites" / "terrain"
    sprites_dir.mkdir(parents=True, exist_ok=True)

    all_entries: list[SpriteEntry] = []
    cultures_json: dict = {}

    for culture, spec in _CULTURES.items():
        page_sprite_ids: list[str] = [""]  # index 0 unused
        tran_sprite_id = ""

        for index in range(1, 14):
            tag = spec["pages"].get(index)
            leaf = find_leaf(terr_root, [tag]) if tag is not None else None
            if leaf is None:
                page_sprite_ids.append("")
                continue
            sprite = decode_sprite(m_ui_data, leaf.abs_off, leaf.size)
            if sprite is None:
                page_sprite_ids.append("")
                continue
            sprite_id = f"terrain.{culture}.page{index:02d}"
            relative_path = Path("sprites") / "terrain" / f"{sprite_id}.png"
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
            all_entries.append(SpriteEntry(
                id=sprite_id,
                file=str(relative_path).replace("\\", "/"),
                width=sprite.width,
                height=sprite.height,
            ))
            page_sprite_ids.append(sprite_id)

        tran_tag = spec.get("tran", "")
        if tran_tag:
            tran_leaf = find_leaf(terr_root, [tran_tag])
            if tran_leaf is not None:
                sprite = decode_sprite(m_ui_data, tran_leaf.abs_off, tran_leaf.size)
                if sprite is not None:
                    tran_sprite_id = f"terrain.{culture}.tran"
                    relative_path = Path("sprites") / "terrain" / f"{tran_sprite_id}.png"
                    rgba = _apply_dissolve_mask(sprite.rgba)
                    write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, rgba)
                    all_entries.append(SpriteEntry(
                        id=tran_sprite_id,
                        file=str(relative_path).replace("\\", "/"),
                        width=sprite.width,
                        height=sprite.height,
                    ))

        # Network atlases (trail1/trail2/canal/rail) per-culture.
        net_subtags = _NETWORK_ATLAS_SUBTAGS.get(culture, {})
        network_sprite_ids: dict[str, str] = {}
        for net_key, dir_tag in (("trail1", "trai"), ("trail2", "trai"),
                                  ("canal", "cana"), ("rail", "rail")):
            subtag = net_subtags.get(net_key)
            if not subtag:
                network_sprite_ids[net_key] = ""
                continue
            leaf = find_leaf(terr_root, [dir_tag, subtag])
            if leaf is None:
                network_sprite_ids[net_key] = ""
                continue
            sprite = decode_sprite(m_ui_data, leaf.abs_off, leaf.size)
            if sprite is None:
                network_sprite_ids[net_key] = ""
                continue
            net_sprite_id = f"terrain.{culture}.{net_key}"
            relative_path = Path("sprites") / "terrain" / f"{net_sprite_id}.png"
            rgba = _apply_dissolve_mask(sprite.rgba)
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, rgba)
            all_entries.append(SpriteEntry(
                id=net_sprite_id,
                file=str(relative_path).replace("\\", "/"),
                width=sprite.width,
                height=sprite.height,
            ))
            network_sprite_ids[net_key] = net_sprite_id

        cultures_json[culture] = {
            "pages": page_sprite_ids,
            "tran": tran_sprite_id,
            "hidd": _HIDD_SPRITE_ID,
            "trail1": network_sprite_ids.get("trail1", ""),
            "trail2": network_sprite_ids.get("trail2", ""),
            "canal": network_sprite_ids.get("canal", ""),
            "rail": network_sprite_ids.get("rail", ""),
        }

    # Shared sprites: shore overlays, map-edge skirt, starfield background.
    for tag, sprite_id in _SHORE_ATLAS_TAGS.items():
        leaf = find_leaf(terr_root, [tag])
        if leaf is None:
            continue
        sprite = decode_sprite(m_ui_data, leaf.abs_off, leaf.size)
        if sprite is None:
            continue
        relative_path = Path("sprites") / "terrain" / f"{sprite_id}.png"
        rgba = _apply_dissolve_mask(sprite.rgba)
        write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, rgba)
        all_entries.append(SpriteEntry(
            id=sprite_id,
            file=str(relative_path).replace("\\", "/"),
            width=sprite.width,
            height=sprite.height,
        ))

    edge_leaf = find_leaf(terr_root, [_EDGE_TEXTURE_TAG])
    if edge_leaf is not None:
        sprite = decode_sprite(m_ui_data, edge_leaf.abs_off, edge_leaf.size)
        if sprite is not None:
            relative_path = Path("sprites") / "terrain" / f"{_EDGE_SPRITE_ID}.png"
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
            all_entries.append(SpriteEntry(
                id=_EDGE_SPRITE_ID,
                file=str(relative_path).replace("\\", "/"),
                width=sprite.width,
                height=sprite.height,
            ))

    hidd_leaf = find_leaf(terr_root, [_HIDD_TEXTURE_TAG])
    if hidd_leaf is not None:
        sprite = decode_sprite(m_ui_data, hidd_leaf.abs_off, hidd_leaf.size)
        if sprite is not None:
            relative_path = Path("sprites") / "terrain" / f"{_HIDD_SPRITE_ID}.png"
            write_png_rgba(output_dir / relative_path, sprite.width, sprite.height, sprite.rgba)
            all_entries.append(SpriteEntry(
                id=_HIDD_SPRITE_ID,
                file=str(relative_path).replace("\\", "/"),
                width=sprite.width,
                height=sprite.height,
            ))

    # Populate alias entries so C++ can do a direct lookup by any regi.cult value.
    # Aliases share the base palette's sprite IDs (no extra PNGs needed).
    for alias, base in _CULTURE_TO_BASE.items():
        if alias not in cultures_json and base in cultures_json:
            cultures_json[alias] = cultures_json[base]

    write_json(output_dir / _TERRAIN_TEXTURES_TABLE_PATH, {"cultures": cultures_json})

    return all_entries
