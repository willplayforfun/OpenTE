"""Decoder for the "bg6a" sprite/bitmap leaf format used throughout
`font.{}`, `bldg.{}`, `unit.{}`, `a_ui*`, `d_ui*`, `m_ui*`, and `flor.{}`.

Leaf layout: a 32-byte header followed by `width * height * bpp` bytes of
row-major pixel data (plus a 0-16 byte trailer that is ignored).

`bpp` (bytes per pixel) and the pixel format are NOT fully determined by the
header alone -- see `decode_sprite` and `_pixel_format_from_str_rel` for the
inference rules. This is a direct port of
`documentation/scripts/te_sprite.py`; see
`documentation/01-container-format.md` ("Pixel formats", "Sprite palette
format") for the full derivation.
"""
from __future__ import annotations

import struct
import zlib
from dataclasses import dataclass
from pathlib import Path

from ..containers.container import ChildEntry, DirNode, read_record

_HEADER_MAGIC = 0x61366762  # "bg6a", reversed-ASCII
_HEADER_VERSION = 2
_HEADER_SIZE = 32

_PALETTE_MAGIC = b"ap01"
_PALETTE_SIZE = 1052
_PALETTE_HEADER_SIZE = 28
_PALETTE_NUM_COLORS = 512

_TRANSPARENT_KEY_RGB = (255, 0, 255)  # DirectDraw magenta colour-key

# Every bg6a row is stored rotated left by this many BYTES (a surface-pitch
# artifact): the leftmost _ROW_WRAP_BYTES of raw pixel data for a row actually
# belong at the right edge of that row. 4 bytes = 4 px at 8bpp, 2 px at 16bpp,
# 1 px at 32bpp. See _decode_raw().
_ROW_WRAP_BYTES = 4


@dataclass
class DecodedSprite:
    """An RGBA8888 image decoded from a sprite leaf."""

    width: int
    height: int
    anchor_x: int
    anchor_y: int
    rgba: bytes  # width * height * 4 bytes, row-major


def _decode_raw(blob: bytes) -> tuple[int, int, int, int, bytes, int, int, int] | None:
    """Parses a sprite leaf's header and returns the raw pixel payload.

    Returns (width, height, anchor_x, anchor_y, pixels, field3, str_rel, bpp),
    or None if `blob` doesn't look like a sprite leaf. `bpp` is inferred by
    finding the byte width (1, 2, 3, or 4) for which the trailing slack
    `len(blob) - 32 - width*height*bpp` falls in [0, 16]; `field3 == 1`
    forces bpp == 2 (16-bit direct colour).

    Every row is stored rotated left by `_ROW_WRAP_BYTES` (4) bytes relative
    to the displayed image (a surface-pitch artifact: 4 px at 8bpp, 2 px at
    16bpp, 1 px at 32bpp), so `pixels` is un-rotated row-by-row before being
    returned.
    """
    if len(blob) < _HEADER_SIZE:
        return None

    magic, version, str_rel, field3, width, height, anchor_x, anchor_y = struct.unpack_from(
        "<8i", blob, 0
    )
    if magic != _HEADER_MAGIC or version != _HEADER_VERSION:
        return None
    if width <= 0 or height <= 0:
        return None

    num_pixels = width * height
    candidate_bpps = [2] if field3 == 1 else [1, 4, 2, 3]
    bpp = None
    for candidate in candidate_bpps:
        trailer = len(blob) - _HEADER_SIZE - num_pixels * candidate
        if 0 <= trailer <= 16:
            bpp = candidate
            break
    if bpp is None:
        return None

    pixel_bytes = num_pixels * bpp
    pixels = blob[_HEADER_SIZE:_HEADER_SIZE + pixel_bytes]

    # Every bg6a row is stored rotated left by a fixed _ROW_WRAP_BYTES (4) — a
    # surface-pitch artifact: the first 4 bytes of raw pixel data for a row
    # actually belong at the row's right edge. Un-rotate so column 0 is the
    # true left edge. This is bytes, not pixels, so it scales with bpp: 4 px at
    # 8bpp, 2 px at 16bpp, 1 px at 32bpp. (Originally only applied to 8bpp
    # paletted sprites; it is in fact universal — most visible on narrow sprites
    # like the 16x120 scrollbar `stdc.vscr`, negligible on wide ones.)
    row_bytes = width * bpp
    if _ROW_WRAP_BYTES < row_bytes:
        unrotated = bytearray(pixel_bytes)
        for y in range(height):
            row = pixels[y * row_bytes:(y + 1) * row_bytes]
            unrotated[y * row_bytes:(y + 1) * row_bytes] = (
                row[_ROW_WRAP_BYTES:] + row[:_ROW_WRAP_BYTES])
        pixels = bytes(unrotated)
    return width, height, anchor_x, anchor_y, pixels, field3, str_rel, bpp


def _decode_palette(data: bytes, offset: int) -> list[int] | None:
    """Decodes a 1052-byte 'ap01' palette blob at absolute `offset`.

    Returns 512 RGB565 colours, where `colours[256 + p]` is the final colour
    for 8bpp palette index `p`, or None if `offset` doesn't point at a valid
    palette blob.
    """
    if offset < 0 or offset + _PALETTE_SIZE > len(data):
        return None
    if data[offset:offset + 4] != _PALETTE_MAGIC:
        return None
    return list(struct.unpack_from(f"<{_PALETTE_NUM_COLORS}H", data, offset + _PALETTE_HEADER_SIZE))


def _rgb565_to_rgb(c: int) -> tuple[int, int, int]:
    r, g, b = (c >> 11) & 0x1F, (c >> 5) & 0x3F, c & 0x1F
    return (r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)


def _rgb555_to_rgb(c: int) -> tuple[int, int, int]:
    r, g, b = (c >> 10) & 0x1F, (c >> 5) & 0x1F, c & 0x1F
    return (r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2)


def _argb4444_to_rgba(c: int) -> tuple[int, int, int, int]:
    a, r, g, b = (c >> 12) & 0xF, (c >> 8) & 0xF, (c >> 4) & 0xF, c & 0xF
    return r * 17, g * 17, b * 17, a * 17


def _pixel_format_from_str_rel(str_rel: int) -> str:
    """For bpp==2 leaves, `str_rel` is a literal 4-char ASCII format tag
    (not an offset): "5650"=RGB565, "5550"=RGB555, "4444"=ARGB4444. Defaults
    to "4444" (the most common 16bpp format) for unrecognized tags."""
    tag = struct.pack("<i", str_rel)
    if tag[:2] == b"56":
        return "565"
    if tag[:2] == b"55":
        return "555"
    return "4444"


def _colorize_16bpp(pixels: bytes, fmt: str) -> bytes:
    values = struct.unpack_from(f"<{len(pixels) // 2}H", pixels, 0)
    rgba = bytearray(len(values) * 4)
    cache: dict[int, tuple[int, int, int, int]] = {}
    for i, c in enumerate(values):
        colour = cache.get(c)
        if colour is None:
            if fmt == "4444":
                colour = _argb4444_to_rgba(c)
            else:
                rgb = _rgb565_to_rgb(c) if fmt == "565" else _rgb555_to_rgb(c)
                alpha = 0 if rgb == _TRANSPARENT_KEY_RGB else 255
                colour = (*rgb, alpha)
            cache[c] = colour
        rgba[i * 4:i * 4 + 4] = bytes(colour)
    return bytes(rgba)


# Palette indices 0-7 (`colours[256+0]` .. `colours[256+7]`) are a fixed
# "rainbow ramp" (magenta, cyan, yellow, green, two reds, ...) present
# verbatim in every `ap01` palette observed (e.g. `bldg.cher.cbaz` and
# `bldg.chi1.head`). Index 0 is the DirectDraw magenta colour-key and is
# already transparent via the colour-match below; 1-7 are anti-aliasing/
# edge-blend placeholder markers that were never replaced with real colours
# -- rendering them produces the red/green pixel fringes and stripes seen on
# extracted building sprites, so treat the whole 0-7 range as transparent.
_PLACEHOLDER_RAMP_MAX_INDEX = 7


def _colorize_paletted(pixels: bytes, palette: list[int]) -> bytes:
    rgba = bytearray(len(pixels) * 4)
    cache: dict[int, tuple[int, int, int, int]] = {}
    for i, p in enumerate(pixels):
        colour = cache.get(p)
        if colour is None:
            rgb = _rgb565_to_rgb(palette[256 + p])
            transparent = p <= _PLACEHOLDER_RAMP_MAX_INDEX or rgb == _TRANSPARENT_KEY_RGB
            colour = (*rgb, 0 if transparent else 255)
            cache[p] = colour
        rgba[i * 4:i * 4 + 4] = bytes(colour)
    return bytes(rgba)


def decode_sprite(data: bytes, leaf_off: int, leaf_size: int) -> DecodedSprite | None:
    """Decodes the sprite leaf [leaf_off, leaf_off + leaf_size) to RGBA8888.

    Returns None if the leaf doesn't look like a sprite (wrong magic/version,
    or an inconsistent size).
    """
    blob = data[leaf_off:leaf_off + leaf_size]
    raw = _decode_raw(blob)
    if raw is None:
        return None
    width, height, anchor_x, anchor_y, pixels, field3, str_rel, bpp = raw

    if bpp == 4:
        rgba = pixels
    elif bpp == 1 and field3 == 19:
        palette = _decode_palette(data, leaf_off + str_rel)
        if palette is None:
            # Fall back to a flat grayscale-as-RGBA rendering.
            rgba = b"".join(bytes((p, p, p, 255)) for p in pixels)
        else:
            rgba = _colorize_paletted(pixels, palette)
    elif bpp == 2:
        rgba = _colorize_16bpp(pixels, _pixel_format_from_str_rel(str_rel))
    else:
        rgba = b"".join(bytes((p, p, p, 255)) for p in pixels)

    return DecodedSprite(width=width, height=height, anchor_x=anchor_x, anchor_y=anchor_y, rgba=rgba)


def find_leaf(node: DirNode, path: list[str]) -> ChildEntry | None:
    """Walks `path` (a list of tags) from `node` and returns the final leaf
    entry, or None if any path component is missing or not a leaf at the
    end / not a directory before the end."""
    current = node
    for i, tag in enumerate(path):
        match = next((c for c in current.children if c.tag == tag), None)
        if match is None:
            return None
        is_last = i == len(path) - 1
        if is_last:
            return match
        if match.kind != "dir" or match.dir is None:
            return None
        current = match.dir
    return None


def write_png_rgba(path: Path, width: int, height: int, rgba: bytes) -> None:
    """Writes `rgba` (width * height * 4 bytes) as an 8-bit RGBA PNG."""
    raw = bytearray()
    stride = width * 4
    for y in range(height):
        raw.append(0)  # filter type 0 (none)
        raw.extend(rgba[y * stride:(y + 1) * stride])
    compressed = zlib.compress(bytes(raw), level=9)

    def chunk(tag: bytes, payload: bytes) -> bytes:
        body = tag + payload
        return struct.pack(">I", len(payload)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    signature = b"\x89PNG\r\n\x1a\n"
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = signature + chunk(b"IHDR", ihdr) + chunk(b"IDAT", compressed) + chunk(b"IEND", b"")
    path.write_bytes(png)
