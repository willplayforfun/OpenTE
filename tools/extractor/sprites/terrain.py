"""Extracts terrain tile and map-edge textures from `m_ui,u.{}` (B7).

`m_ui,u.{}`'s `terr/` directory has 93 leaves, each a 256x256 RGBA8888
`bg6a` sprite (e.g. `terr/deep`, `terr/ts11`, `terr/edge`). Per
`documentation/08-investigation-needed.md` B7, the per-tile lookup table the
original game uses to pick a texture for each `mapp.terr` byte value hasn't
been decoded yet, so this picks one representative texture per clone
`world::TerrainType` enum value (`_TERRAIN_TYPE_TEXTURES`) -- a coarse
"textured but not authentic" placeholder, same spirit as
`maps.region._terrain_type_for_band`.

Each chosen texture is downsampled from 256x256 to a `kTileWidth` x
`kTileHeight` (64x32) tile and, for the terrain types, masked to the
isometric diamond shape (alpha=0 outside the diamond) so adjacent tiles
don't overdraw each other's corners. The edge texture (`terr_edge`, a
vertical rock/cliff face -- see B11/roadmap item 3) is downsampled to the
same tile size but left as a full rectangle, for use as a border "skirt".
"""
from __future__ import annotations

from pathlib import Path

from ..containers.container import DirNode, find_child
from ..manifest import SpriteEntry
from .sprite import decode_sprite, find_leaf, write_png_rgba

_TILE_WIDTH = 64
_TILE_HEIGHT = 32

# (clone TerrainType name, `m_ui,u.{}` `terr/<tag>` leaf) -- order matches
# `world::TerrainType` / `maps.region._TERRAIN_TYPES`.
_TERRAIN_TYPE_TEXTURES = [
    ("deep_water", "deep"),
    ("shallow_water", "seas"),
    ("plains", "ts11"),
    ("hills", "dirt"),
    ("mountains", "mntn"),
    ("desert", "sand"),
    ("forest", "ts20"),
]

_EDGE_TEXTURE_TAG = "edge"
_EDGE_SPRITE_ID = "terrain.edge"


def _downsample(rgba: bytes, src_size: int, dst_w: int, dst_h: int) -> bytes:
    """Nearest-neighbour downsample of a `src_size` x `src_size` RGBA8888
    image to `dst_w` x `dst_h`."""
    out = bytearray(dst_w * dst_h * 4)
    for dy in range(dst_h):
        sy = dy * src_size // dst_h
        for dx in range(dst_w):
            sx = dx * src_size // dst_w
            si = (sy * src_size + sx) * 4
            di = (dy * dst_w + dx) * 4
            out[di:di + 4] = rgba[si:si + 4]
    return bytes(out)


def _diamond_mask(rgba: bytes, w: int, h: int) -> bytes:
    """Sets alpha=0 for every pixel outside the `w` x `h` isometric diamond."""
    out = bytearray(rgba)
    cx, cy = w / 2.0, h / 2.0
    for y in range(h):
        for x in range(w):
            if abs((x + 0.5) - cx) / cx + abs((y + 0.5) - cy) / cy > 1.0:
                out[(y * w + x) * 4 + 3] = 0
    return bytes(out)


def extract_terrain_textures(m_ui_data: bytes, m_ui_root: DirNode, output_dir: Path) -> list[SpriteEntry]:
    """Returns `SpriteEntry` list for the per-`TerrainType` tile textures
    plus the map-edge "skirt" texture, writing PNGs under
    `<output_dir>/sprites/terrain/`."""
    terr_entry = find_child(m_ui_root, "terr")
    if terr_entry is None or terr_entry.dir is None:
        return []
    terr_root = terr_entry.dir

    sprites_dir = output_dir / "sprites" / "terrain"
    sprites_dir.mkdir(parents=True, exist_ok=True)

    entries: list[SpriteEntry] = []

    for type_name, tag in _TERRAIN_TYPE_TEXTURES:
        leaf = find_leaf(terr_root, [tag])
        if leaf is None:
            continue
        sprite = decode_sprite(m_ui_data, leaf.abs_off, leaf.size)
        if sprite is None:
            continue
        tile = _diamond_mask(_downsample(sprite.rgba, sprite.width, _TILE_WIDTH, _TILE_HEIGHT),
                              _TILE_WIDTH, _TILE_HEIGHT)
        sprite_id = f"terrain.{type_name}"
        relative_path = Path("sprites") / "terrain" / f"{sprite_id}.png"
        write_png_rgba(output_dir / relative_path, _TILE_WIDTH, _TILE_HEIGHT, tile)
        entries.append(SpriteEntry(id=sprite_id, file=str(relative_path).replace("\\", "/"),
                                     width=_TILE_WIDTH, height=_TILE_HEIGHT))

    edge_leaf = find_leaf(terr_root, [_EDGE_TEXTURE_TAG])
    if edge_leaf is not None:
        sprite = decode_sprite(m_ui_data, edge_leaf.abs_off, edge_leaf.size)
        if sprite is not None:
            tile = _downsample(sprite.rgba, sprite.width, _TILE_WIDTH, _TILE_HEIGHT)
            relative_path = Path("sprites") / "terrain" / f"{_EDGE_SPRITE_ID}.png"
            write_png_rgba(output_dir / relative_path, _TILE_WIDTH, _TILE_HEIGHT, tile)
            entries.append(SpriteEntry(id=_EDGE_SPRITE_ID, file=str(relative_path).replace("\\", "/"),
                                         width=_TILE_WIDTH, height=_TILE_HEIGHT))

    return entries
