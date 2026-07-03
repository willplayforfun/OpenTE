"""Exports `maps/<map-id>.json` from a `Maps/<name>.{}` region file.

See `OpenTE/spec/world-and-maps.md` ("Map data") for the target schema and
`documentation/scripts/te_map.py` for the source this was ported from.

Two things this module decodes that aren't pinned down by the spec yet:

- **`mapp.terr` -> clone terrain-type mapping** (`_TERRAIN_TYPES` /
  `_terrain_type_for_value`): each byte packs (high_nibble=band,
  low_nibble=texture_page). Water vs land is keyed off the texture page
  (page 1 = `deep` -> deep_water, page 2 = `seas` -> shallow_water), matching
  the renderer's `own_index <= 2` water test and altitude data. The land
  buildable/impassable split (pages 3-13) is still a coarse band placeholder
  (top band -> impassable) pending spec-deviations #8.
- **`terrain.data` "base64-rle" encoding** (`_encode_terrain_rle`): not
  specified byte-for-byte by `world-and-maps.md`, so this module defines it
  as a flat sequence of `(type_byte: uint8, run_length: uint16 LE)` pairs
  covering the row-major `width*height` grid, base64-encoded.
- **`heightmap.data` "raw-base64" encoding**: the original `mapp.alti`
  byte grid (row-major, one byte per tile, `0..255`), base64-encoded
  verbatim with no transformation -- see
  `documentation/08-investigation-needed.md` B15 for the heightfield
  validation and the `alti_byte * 10.0 / 256.0` world-height formula.
- **`connectivity.data` "base64-rle6" encoding** (`_decode_path_brid` /
  `_encode_connectivity_rle`): the per-tile 6-byte network-connectivity mask
  (trail/road/rail/canal/bridge/bridge_aux -- matching C++
  `world::TileConnectivity`), seeded from the map's authored `mapp.path` and
  `mapp.brid` record arrays exactly as `SilkRoadMap::Load` does
  (`documentation/03-exe-analysis.md` Round 23). Tiles not covered by a
  `path`/`brid` record keep the engine's default-on-miss mask
  `{0,0,0,0,0xff,0}` (no networks; `bridge == 0xff` means "no bridge").
  RLE-encoded as `(tile: 6 bytes, run_length: uint16 LE)` pairs.
"""
from __future__ import annotations

import base64
import struct
from typing import Any

from ..containers.container import DirNode, find_child
from ..containers.record import Field, parse_record

# The clone's own small terrain-type enum (world-and-maps.md). Index into
# this list is the byte stored in `terrain.data`.  Must stay in sync with
# world/region.h's TerrainType enum.
_TERRAIN_TYPES = [
    "deep_water",
    "shallow_water",
    "buildable",
    "impassable",
]

# `mapp.terr` bytes pack (high_nibble=terrain_band, low_nibble=texture_page).
# Observed values on `ep01 China`: 2, 4-8, 33-40, 64-70, 96-102 -- four
# high-nibble bands (0, 2, 4, 6) crossed with texture pages 1-8.
_DEEP_WATER = _TERRAIN_TYPES.index("deep_water")
_SHALLOW_WATER = _TERRAIN_TYPES.index("shallow_water")
_BUILDABLE = _TERRAIN_TYPES.index("buildable")
_IMPASSABLE = _TERRAIN_TYPES.index("impassable")


def _texture_page(value: int) -> int:
    """Texture-page index (1-13) for a `mapp.terr` byte = its low nibble,
    indexing `tables/terrain_textures.json` (B15 Round 35/36,
    terrain-blending-plan.md Stage A.1). Page 1 = `deep`, page 2 = `seas`,
    pages 3-13 = land textures (terr/sets palette slot order,
    data_catalog_terr_sets.txt). 0 / out-of-range clamp to 1 (the round's
    notes say those shouldn't occur but it isn't exhaustively verified)."""
    page = value & 0xF
    return page if 1 <= page <= 13 else 1


def _terrain_type_for_value(value: int) -> int:
    # Water vs land is decided by the texture PAGE (low nibble), not the band
    # (high nibble): page 1 = `deep` (deep water), page 2 = `seas` (shallow
    # water). This matches the renderer's own water test (`own_index <= 2` in
    # terrain_renderer.cpp) and is altitude-consistent (every page-2 tile sits
    # at water level, including band-shifted bytes like 0x22 that the old
    # band-only rule misclassified as buildable land -- the source of the
    # spurious map-edge coastline).
    page = _texture_page(value)
    if page == 1:
        return _DEEP_WATER
    if page == 2:
        return _SHALLOW_WATER
    # Land pages (3-13). Buildable vs impassable is still a coarse band
    # (high-nibble) placeholder pending spec-deviations #8: the top band
    # (bytes 96-102, high_nibble=6) is treated as impassable, the rest buildable.
    if value <= 95:
        return _BUILDABLE
    return _IMPASSABLE


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


# Engine default-on-miss connectivity mask (Round 16): no trail/road/canal/rail,
# `bridge == 0xff` ("no bridge" sentinel -- MUST be 0xff, see world::TileConnectivity).
_CONN_DEFAULT = bytes((0, 0, 0, 0, 0xFF, 0))


def _decode_path_brid(
    map_data: bytes, header_off: int, mapp_fields: dict[str, Field], width: int, height: int
) -> bytes:
    """Builds the row-major per-tile 6-byte connectivity grid from
    `mapp.path` / `mapp.brid`, mirroring `SilkRoadMap::Load`
    (`documentation/03-exe-analysis.md` Round 23).

    Both arrays are `0x40` reference fields holding back-to-back
    `{x:int16, y:int16, flags:uint32}` (8-byte) elements:

    - **path** -- *overwrites* the tile's mask with
      `trail=flags>>24`, `road=(flags>>16)&0xff`, `rail=(flags>>8)&0xff`,
      `canal=0`, `bridge=0xff`, `bridge_aux=0`.
    - **brid** -- *overwrites* bytes 4/5 of the tile's current mask
      (preserving any path data in bytes 0-3): `bridge = flags & 0xff`,
      `bridge_aux = (flags >> 8) & 0xff`. Confirmed plain `mov`s at EXE
      0x461df8/0x461dfb -- an earlier RE pass (Round 23) misread these as
      ORs, which would have made brid records a no-op against the 0xff
      default. A bridge tile (`bridge != 0xff`) suppresses the Stage-D
      network decal; the bridge visual is a separate sprite.

    Tiles with no record keep `_CONN_DEFAULT`.
    """
    total = width * height
    grid = bytearray(_CONN_DEFAULT * total)

    def _elems(field: Field | None):
        if field is None or field.size <= 0:
            return
        base = header_off + field.raw_rel
        if base < 0 or base + field.size > len(map_data):
            return
        for i in range(field.size // 8):
            x, y, flags = struct.unpack_from("<hhI", map_data, base + i * 8)
            if 0 <= x < width and 0 <= y < height:
                yield (y * width + x) * 6, flags

    for off, flags in _elems(mapp_fields.get("path")):
        grid[off + 0] = (flags >> 24) & 0xFF  # trail
        grid[off + 1] = (flags >> 16) & 0xFF  # road
        grid[off + 2] = (flags >> 8) & 0xFF   # rail
        grid[off + 3] = 0                      # canal
        grid[off + 4] = 0xFF                   # bridge ("no bridge" sentinel)
        grid[off + 5] = 0                      # bridge_aux

    for off, flags in _elems(mapp_fields.get("brid")):
        grid[off + 4] = flags & 0xFF           # bridge
        grid[off + 5] = (flags >> 8) & 0xFF    # bridge_aux

    return bytes(grid)


def _encode_connectivity_rle(grid: bytes) -> str:
    """RLE-encodes the 6-byte-per-tile connectivity grid as base64.

    Format: a flat sequence of `(tile: 6 bytes, run_length: uint16 LE)` pairs;
    consecutive identical tiles are merged into one run (max run 0xFFFF).
    """
    chunks = bytearray()
    n = len(grid) // 6
    i = 0
    while i < n:
        tile = grid[i * 6:(i + 1) * 6]
        run = 1
        while i + run < n and grid[(i + run) * 6:(i + run + 1) * 6] == tile and run < 0xFFFF:
            run += 1
        chunks += tile
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


def get_map_culture(map_data: bytes, map_root: DirNode) -> str:
    """Return the first `regi.cult` value found in the map file, or ``''``."""
    elem_entry = find_child(map_root, "elem")
    if elem_entry is None or elem_entry.dir is None:
        return ""
    for child in elem_entry.dir.children:
        if child.kind != "dir" or child.dir is None:
            continue
        fields, _next_off = parse_record(map_data, child.dir.offset)
        tf = fields.get("type")
        if tf is None:
            continue
        if tf.value == "regi":
            cv = getattr(fields.get("cult"), "value", None) or ""
            if cv:
                return cv
    return ""


def extract_map(
    map_data: bytes,
    map_root: DirNode,
    *,
    map_id: str,
    episode: str,
    episode_regions: list[dict[str, Any]],
    data_data: bytes,
    data_root: DirNode,
    fallback_culture: str = "",
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
    terrain_types = bytes(_terrain_type_for_value(v) for v in raw_terrain)

    # Per-tile texture-page index (B15 Round 35/36, terrain-blending-plan.md
    # Stage A.1): low nibble of `mapp.terr`, 1-13 indexes
    # `tables/terrain_textures.json`. Shares `_texture_page` with the
    # terrain-type classifier so `terrain.data` and `texture_index.data` stay
    # consistent (water classification derives from this same page).
    texture_indices = bytes(_texture_page(v) for v in raw_terrain)

    alti_field = mapp_fields["alti"]
    alti_off = mapp_entry.dir.offset + alti_field.raw_rel
    raw_alti = map_data[alti_off:alti_off + alti_field.size]

    # Per-tile network connectivity, seeded from authored `mapp.path`/`mapp.brid`
    # (03-exe-analysis.md Round 23). 6 bytes/tile matching world::TileConnectivity.
    connectivity_grid = _decode_path_brid(map_data, mapp_entry.dir.offset, mapp_fields, width, height)

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
        culture_set = regi_records[0]["culture_set"] if regi_records else fallback_culture
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

    culture_set = regi_records[0]["culture_set"] if regi_records else fallback_culture

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
        "connectivity": {
            "encoding": "base64-rle6",
            "data": _encode_connectivity_rle(connectivity_grid),
        },
        "regions": regions,
        "decorations": decorations,
        "cities": cities,
        "special_points": [],
    }
