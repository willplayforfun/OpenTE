"""Extracts font glyph atlases and metrics from ``font.{}`` for OpenTE.

Output layout under ``game_data/fonts/``::

    clea_8pt_atlas.png   — RGBA atlas, white ink, alpha from glyph coverage
    clea_8pt.json        — glyph table + cmap + face metrics
    … (one pair per face/size)

Atlas format notes
------------------
* ``font.{}`` stores each glyph **upside-down** (high atlas-y = top of glyph).
  We Y-flip the entire atlas on export so normal top-down reads are correct.
* Raw pixel values are in [0, 8] where 8 = fully opaque.  We scale to
  SDL-alpha via ``alpha = min(255, value * 32)``.
* The source rect for glyph *i* in the exported PNG is::

      x = glyphs[i]["atlas_x"],   y = glyphs[i]["atlas_y"],
      w = glyphs[i]["slot_w"],    h = glyphs[i]["ink_h"]

* Destination baseline offset:  ``dest_y = baseline_y - ink_h + 1``
  (bottom of ink aligns with baseline for non-descenders; glyphs with
  ``atlas_y < descender`` extend ink below the baseline).

JSON schema
-----------
::

    {
      "face": "clea", "pt": 12, "atlas_w": <int>, "atlas_h": <int>,
      "metrics": {
        "ascender": <int>,    // px above baseline for tallest glyph
        "descender": <int>,   // px below baseline (positive)
        "max_advance": <int>, "line_height": <int>
      },
      "cmap": [<uint16>, ...],  // dense: cmap[codepoint] = glyph_index
      "glyphs": [
        { "atlas_x": <int>, "atlas_y": <int>,
          "slot_w": <int>,  "ink_h": <int>, "advance": <int> },
        ...
      ]
    }
"""
from __future__ import annotations

import json
import struct
from pathlib import Path

from ..containers.container import DirNode

# Face tags present in font.{} and the point sizes each carries.
_FACE_SIZES: dict[str, list[int]] = {
    "clea": [8, 9, 10, 11, 12, 14, 18, 24],
    "cour": [10],
    "sans": [8, 9, 10, 11, 12, 14, 18, 24],
    "seri": [8, 9, 10, 11, 12, 14, 18, 24],
}


def _pt_tag(pt: int) -> str:
    """Return the 4-byte tag used for a point-size directory (big-endian int)."""
    return struct.pack(">i", pt).decode("latin1")


# SDL2 hard-limits textures to 16384×16384.  Keep rows well under that.
_MAX_ROW_WIDTH = 4096


def _flip_and_scale_rgba(raw: bytes, w: int, h: int) -> bytes:
    """Y-flip the atlas and scale pixel values [0-8] → alpha [0-255].

    Returns a flat RGBA byte array with R=G=B=255 and A=min(255, v*32).
    y=0 in the result is the original bottom row (glyph-top after flip).
    """
    rgba = bytearray(w * h * 4)
    for y in range(h):
        src_row = h - 1 - y          # flip: original bottom row → new row 0
        for x in range(w):
            v = raw[src_row * w + x]
            a = v * 32
            if a > 255:
                a = 255
            i = (y * w + x) * 4
            rgba[i]     = 255
            rgba[i + 1] = 255
            rgba[i + 2] = 255
            rgba[i + 3] = a
    return bytes(rgba)


def _repack_rows(strip_rgba: bytes, strip_w: int, row_h: int,
                 glyph_recs: list[dict]) -> tuple[bytes, int, int]:
    """Repack a single-strip flipped RGBA atlas into a multi-row layout.

    Each glyph rec must have orig_x, slot_w, new_atlas_x, new_row already set.
    Returns (new_rgba, new_w, new_h).
    """
    num_rows = (max(g["new_row"] for g in glyph_recs) + 1) if glyph_recs else 1
    new_w = max(
        (g["new_atlas_x"] + g["slot_w"]) for g in glyph_recs if g["slot_w"] > 0
    ) if any(g["slot_w"] > 0 for g in glyph_recs) else 1
    new_h = num_rows * row_h

    new_rgba = bytearray(new_w * new_h * 4)
    for g in glyph_recs:
        if g["slot_w"] <= 0:
            continue
        orig_x  = g["orig_x"]
        new_x   = g["new_atlas_x"]
        new_yb  = g["new_row"] * row_h
        sw      = g["slot_w"]
        for row in range(row_h):
            src = (row * strip_w + orig_x) * 4
            dst = ((new_yb + row) * new_w + new_x) * 4
            new_rgba[dst:dst + sw * 4] = strip_rgba[src:src + sw * 4]

    return bytes(new_rgba), new_w, new_h


def _write_png_rgba(path: Path, w: int, h: int, rgba: bytes) -> None:
    from PIL import Image
    Image.frombytes("RGBA", (w, h), rgba).save(path)


def extract_fonts(font_data: bytes, font_root: DirNode, output_dir: Path) -> list[str]:
    """Extract all font faces/sizes from already-loaded ``font.{}`` bytes.

    *font_root* is the parsed tree returned by ``parse_tree(font_data, footer)``.
    Writes PNG + JSON pairs under ``output_dir / "fonts"``.
    Returns a list of written stems (e.g. ``["clea_8pt", "clea_9pt", ...]``).
    """
    fonts_dir = output_dir / "fonts"
    fonts_dir.mkdir(parents=True, exist_ok=True)

    written: list[str] = []

    for face_child in font_root.children:
        if face_child.kind != "dir" or face_child.tag not in _FACE_SIZES:
            continue
        face_dir: DirNode = face_child.dir  # type: ignore[assignment]

        # Group the face's leaf children by their pt-size tag.
        by_tag: dict[str, list] = {}
        for leaf in face_dir.children:
            by_tag.setdefault(leaf.tag, []).append(leaf)

        for pt in _FACE_SIZES[face_child.tag]:
            tag = _pt_tag(pt)
            leaves = by_tag.get(tag, [])
            if len(leaves) < 4:
                continue  # need atlas, metrics, cmap, big

            atlas_leaf, metrics_leaf, cmap_leaf, big_leaf = leaves[:4]

            # --- Atlas (bg6a header: 8 × int32, then w*h raw bytes) ---
            aoff = atlas_leaf.abs_off
            strip_w, row_h = struct.unpack_from("<ii", font_data, aoff + 16)
            raw_pix = font_data[aoff + 32: aoff + 32 + strip_w * row_h]
            strip_rgba = _flip_and_scale_rgba(raw_pix, strip_w, row_h)

            # --- Metrics: 4 × int32 = (neg_ascender, neg_descender, max_adv, line_h) ---
            m = struct.unpack_from("<4i", font_data, metrics_leaf.abs_off)
            metrics = {
                "ascender":    -m[0],   # positive = px above baseline
                "descender":   -m[1],   # positive = px below baseline
                "max_advance":  m[2],
                "line_height":  m[3],
            }

            # --- CMap: dense uint16 array, index = codepoint, value = glyph_index ---
            cmap_count = cmap_leaf.size // 2
            cmap_list = list(struct.unpack_from(
                f"<{cmap_count}H", font_data, cmap_leaf.abs_off))

            # --- Big table: 28 bytes/glyph (7 × int32) ---
            # f[0]=orig_atlas_x  f[1]=within_row_y (in flipped strip)
            # f[2]=slot_w        f[3]=advance
            # f[4]=unused        f[5]=ink_h   f[6]=unused
            #
            # Remap glyphs into rows of ≤_MAX_ROW_WIDTH pixels so the atlas
            # PNG stays within SDL2's 16384px texture limit.
            n_glyph = big_leaf.size // 28
            glyph_recs: list[dict] = []
            cur_row_x = 0
            cur_row   = 0
            for gi in range(n_glyph):
                rec = struct.unpack_from("<7i", font_data,
                                        big_leaf.abs_off + gi * 28)
                sw = rec[2]
                if sw > 0 and cur_row_x + sw > _MAX_ROW_WIDTH:
                    cur_row  += 1
                    cur_row_x = 0
                glyph_recs.append({
                    "orig_x":      rec[0],
                    "within_row_y": rec[1],
                    "slot_w":      sw,
                    "advance":     rec[3],
                    "ink_h":       rec[5],
                    "new_atlas_x": cur_row_x,
                    "new_row":     cur_row,
                })
                cur_row_x += sw

            # Build the repacked atlas PNG.
            new_rgba, new_w, new_h = _repack_rows(
                strip_rgba, strip_w, row_h, glyph_recs)
            stem = f"{face_child.tag}_{pt}pt"
            _write_png_rgba(fonts_dir / f"{stem}_atlas.png", new_w, new_h, new_rgba)

            # Build glyph list with updated atlas coordinates.
            # atlas_y = row_offset + within_row_y, so SDL_RenderCopy source
            # rect (atlas_x, atlas_y, slot_w, ink_h) is correct directly.
            glyphs = []
            for g in glyph_recs:
                glyphs.append({
                    "atlas_x": g["new_atlas_x"],
                    "atlas_y": g["new_row"] * row_h + g["within_row_y"],
                    "slot_w":  g["slot_w"],
                    "advance": g["advance"],
                    "ink_h":   g["ink_h"],
                })

            out = {
                "face":    face_child.tag,
                "pt":      pt,
                "atlas_w": new_w,
                "atlas_h": new_h,
                "metrics": metrics,
                "cmap":    cmap_list,
                "glyphs":  glyphs,
            }
            json_path = fonts_dir / f"{stem}.json"
            with open(str(json_path), "w", encoding="utf-8") as f:
                json.dump(out, f, separators=(",", ":"))

            written.append(stem)

    return written
