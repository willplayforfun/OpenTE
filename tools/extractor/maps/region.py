"""Exports `maps/<map-id>.json` from a `Maps/<name>.{}` region file.

See `OpenTE/spec/world-and-maps.md` ("Map data") for the target schema and
`documentation/scripts/te_map.py` for the source this was ported from.

Two things this module decodes that aren't pinned down by the spec yet:

- **Terrain band -> clone terrain-type mapping** (`_TERRAIN_TYPES` /
  `_terrain_type_for_band`): the original `mapp.terr` grid uses ~17-19 raw
  byte values clustered into bands (observed on `ep01 China`: 2, 4-8, 33-40,
  64-70, 96-102). Per `world-and-maps.md`'s "Open questions", this is a
  coarse placeholder mapping (band -> one of the clone's small terrain-type
  enum values) until the original's per-value semantics are decoded.
- **`terrain.data` "base64-rle" encoding** (`_encode_terrain_rle`): not
  specified byte-for-byte by `world-and-maps.md`, so this module defines it
  as a flat sequence of `(type_byte: uint8, run_length: uint16 LE)` pairs
  covering the row-major `width*height` grid, base64-encoded.
- **`heightmap.data` "raw-base64" encoding**: the original `mapp.alti`
  byte grid (row-major, one byte per tile, `0..255`), base64-encoded
  verbatim with no transformation -- see
  `documentation/08-investigation-needed.md` B15 for the heightfield
  validation and the `alti_byte * 10.0 / 256.0` world-height formula.
"""
from __future__ import annotations

import base64
import struct
from typing import Any

from ..containers.container import DirNode, find_child
from ..containers.record import Field, parse_record

# The clone's own small terrain-type enum (world-and-maps.md). Index into
# this list is the byte stored in `terrain.data`.
_TERRAIN_TYPES = [
    "deep_water",
    "shallow_water",
    "plains",
    "hills",
    "mountains",
    "desert",
    "forest",
]

# Coarse mapping from `mapp.terr` raw byte value to an index into
# `_TERRAIN_TYPES`, based on the band boundaries observed on `ep01 China`
# (2, 4-8, 33-40, 64-70, 96-102) -- see module docstring.
_DEEP_WATER = _TERRAIN_TYPES.index("deep_water")
_PLAINS = _TERRAIN_TYPES.index("plains")
_HILLS = _TERRAIN_TYPES.index("hills")
_MOUNTAINS = _TERRAIN_TYPES.index("mountains")


def _terrain_type_for_band(value: int) -> int:
    if value <= 2:
        return _DEEP_WATER
    if value <= 31:
        return _PLAINS
    if value <= 63:
        return _HILLS
    return _MOUNTAINS


def _encode_terrain_rle(terrain_types: bytes) -> str:
    """RLE-encodes a row-major terrain-type byte grid as base64.

    Format: a flat sequence of `(type_byte: uint8, run_length: uint16 LE)`
    pairs; consecutive equal bytes are merged into one run (max run length
    0xFFFF, split into multiple pairs if exceeded).
    """
    chunks = bytearray()
    i = 0
    n = len(terrain_types)
    while i < n:
        value = terrain_types[i]
        run = 1
        while i + run < n and terrain_types[i + run] == value and run < 0xFFFF:
            run += 1
        chunks.append(value)
        chunks += struct.pack("<H", run)
        i += run
    return base64.b64encode(bytes(chunks)).decode("ascii")


def _decode_loca(data: bytes, header_off: int, fields: dict[str, Field]) -> tuple[float | None, float | None]:
    loca = fields.get("loca")
    if loca is None:
        return None, None
    loc_off = header_off + loca.raw_rel
    xi, yi = struct.unpack_from("<ii", data, loc_off)
    return xi / 65536.0, yi / 65536.0


def _load_city_names(data: bytes, data_root: DirNode, culture: str) -> dict[int, str]:
    """Returns `{1-based index: city name}` from `Data/data.{}`'s
    `beac.<culture>` table."""
    beac_entry = find_child(data_root, "beac")
    if beac_entry is None or beac_entry.dir is None:
        return {}
    cset_entry = find_child(beac_entry.dir, culture)
    if cset_entry is None or cset_entry.dir is None:
        return {}

    names: dict[int, str] = {}
    for child in cset_entry.dir.children:
        if child.kind != "dir" or child.dir is None:
            continue
        idx = struct.unpack_from("<I", data, child.abs_off + 4)[0]
        fields, _next_off = parse_record(data, child.dir.offset)
        name_field = fields.get("name")
        # Guard: only store if value is a real string (field.value can be None
        # for unrecognised field encodings; storing None would cause
        # dict.get(key, fallback) to return None instead of the fallback when
        # the key exists, producing JSON null that C++ can't deserialise).
        if name_field is not None and isinstance(name_field.value, str):
            names[idx] = name_field.value
    return names


def extract_map(
    map_data: bytes,
    map_root: DirNode,
    *,
    map_id: str,
    episode: str,
    episode_regions: list[dict[str, Any]],
    data_data: bytes,
    data_root: DirNode,
) -> dict[str, Any]:
    """Returns a `maps/<map_id>.json` dict for one `Maps/*.{}` file.

    `episode_regions` is `episodes[episode]["regions"]` from
    `tables/episodes.py` (used for region `id`/`name`); `data_data`/
    `data_root` are `Data/data.{}` (used to resolve city names via `beac`).
    """
    mapp_entry = find_child(map_root, "mapp")
    if mapp_entry is None or mapp_entry.dir is None:
        raise ValueError("'mapp' not found in map file")
    mapp_fields, _next_off = parse_record(map_data, mapp_entry.dir.offset)

    width = mapp_fields["wide"].value
    height = mapp_fields["high"].value

    terr_field = mapp_fields["terr"]
    terr_off = mapp_entry.dir.offset + terr_field.raw_rel
    raw_terrain = map_data[terr_off:terr_off + terr_field.size]
    terrain_types = bytes(_terrain_type_for_band(v) for v in raw_terrain)

    # Per-tile texture-page index (B15 Round 35/36, terrain-blending-plan.md
    # Stage A.1): low nibble of `mapp.terr`, 1-13 indexes
    # `tables/terrain_textures.json`. Clamp out-of-range values to 1 (the
    # round's notes say 0/>13 shouldn't occur but this hasn't been
    # exhaustively verified across all maps).
    texture_indices = bytes((v & 0xF) if 1 <= (v & 0xF) <= 13 else 1 for v in raw_terrain)

    alti_field = mapp_fields["alti"]
    alti_off = mapp_entry.dir.offset + alti_field.raw_rel
    raw_alti = map_data[alti_off:alti_off + alti_field.size]

    elem_entry = find_child(map_root, "elem")
    if elem_entry is None or elem_entry.dir is None:
        raise ValueError("'elem' not found in map file")

    decorations: list[dict[str, Any]] = []
    bepe_records: list[dict[str, Any]] = []
    regi_records: list[dict[str, Any]] = []

    for child in elem_entry.dir.children:
        if child.kind != "dir" or child.dir is None:
            continue
        fields, _next_off = parse_record(map_data, child.dir.offset)
        type_field = fields.get("type")
        if type_field is None:
            continue  # the table's "+1 spillover" trailing directory header
        elem_type = type_field.value
        x, y = _decode_loca(map_data, child.dir.offset, fields)

        if elem_type == "flop":
            culture = getattr(fields.get("cult"), "value", None) or ""
            decoration_id = getattr(fields.get("spec"), "value", None)
            if decoration_id is None:
                decoration_id = 0
            decorations.append({
                "type": "flop",
                "culture": culture,
                "decoration_id": decoration_id,
                "sprite": f"flor.{culture}.{decoration_id}" if culture else "",
                "x": x or 0.0,
                "y": y or 0.0,
            })
        elif elem_type == "bepe":
            bepe_records.append({
                "name_index": fields["name"].value,
                "x": x or 0.0,
                "y": y or 0.0,
            })
        elif elem_type == "regi":
            regi_records.append({
                "culture_set": getattr(fields.get("cult"), "value", None) or "",
                "starting_player": getattr(fields.get("owne"), "value", None) or 0,
                "x": x or 0.0,
                "y": y or 0.0,
                "rotation": getattr(fields.get("rota"), "value", None) or 0,
            })

    regions: list[dict[str, Any]] = []
    for i, regi in enumerate(regi_records):
        ep_region = episode_regions[i] if i < len(episode_regions) else {}
        regions.append({
            "id": ep_region.get("id", regi["culture_set"]),
            "culture_set": regi["culture_set"],
            "starting_player": regi["starting_player"],
            "headquarters": {"x": regi["x"], "y": regi["y"], "rotation": regi["rotation"]},
        })

    cities: list[dict[str, Any]] = []
    if bepe_records:
        culture_set = regi_records[0]["culture_set"] if regi_records else ""
        city_names = _load_city_names(data_data, data_root, culture_set) if culture_set else {}
        for bepe in bepe_records:
            # city_names.get(key, fallback) only uses the fallback when the key
            # is absent; use `or` so a stored None also falls back to the string.
            name = city_names.get(bepe["name_index"]) or f"city_{bepe['name_index']}"
            cities.append({
                "name": name,
                "x": bepe["x"],
                "y": bepe["y"],
                "culture_set": culture_set,
            })

    name = episode_regions[0]["name"] if episode_regions else map_id

    culture_set = regi_records[0]["culture_set"] if regi_records else ""

    return {
        "id": map_id,
        "name": name,
        "episode": episode,
        "culture_set": culture_set,
        "width": width,
        "height": height,
        "terrain": {
            "encoding": "base64-rle",
            "data": _encode_terrain_rle(terrain_types),
        },
        "heightmap": {
            "encoding": "raw-base64",
            "data": base64.b64encode(raw_alti).decode("ascii"),
        },
        "texture_index": {
            "encoding": "base64-rle",
            "data": _encode_terrain_rle(texture_indices),
        },
        "regions": regions,
        "decorations": decorations,
        "cities": cities,
        "special_points": [],
    }
