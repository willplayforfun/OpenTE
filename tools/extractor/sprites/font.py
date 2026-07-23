"""Extracts font glyph atlases and metrics from ``font.{}`` for OpenTE.

Output layout under ``game_data/fonts/``::

    clea_8pt_atlas.png   — RGBA atlas, white ink, alpha from glyph coverage
    clea_8pt.json        — glyph table + cmap + face metrics
    … (one pair per face/size)

Atlas format notes
------------------
Each face/size has four leaves, in order: ``[atlas, metrics, cmap, big]``.

* **Atlas leaf** (``bg6a``, ``field3 == 7`` = 8bpp grayscale): a 36-byte
  ``bg6a`` header (9 × int32: magic, version, str_rel, field3, width, height,
  off_x, off_y, dword9 — the 9th dword at ``+0x20``, read by the engine only
  for odd-``field3`` formats), then a **fixed 512-byte sub-header**, then
  ``width * height`` raw pixel bytes (row-major, top-down).  Pixel values are
  in ``[0, 8]`` and scale to SDL alpha via ``alpha = min(255, value * 32)``.

  IMPORTANT: the pixel data does NOT start immediately after the header —
  there are 548 bytes (36-byte header + 512-byte sub-header) before the pixels.
  Ordinary (non-font) bg6a sprites put pixels at ``+0x24`` (36); font atlases
  have this extra sub-header, so reading from ``+36`` (or the old ``+32``)
  shifts every row left and scrambles the whole strip (this was the
  long-standing "unintelligible fonts" bug).  We read the payload from the
  *end* of the leaf (``abs_off + size - width*height``), which equals ``+548``
  for every observed atlas and is robust to any trailing slack.

  The strip is a single ``height``-tall row holding every glyph side-by-side;
  glyph *i* occupies columns ``[orig_x, orig_x + slot_w)``.  No Y-flip is
  needed — the strip is already stored top-down.

* **Metrics leaf**: 4 × int32 = ``(-ascender, -descender, max_advance,
  line_height)``.

* **CMap leaf**: dense ``uint16`` array, ``cmap[codepoint] = glyph_index``.

* **Big (glyph) leaf**: 28 bytes/glyph (7 × int32):
  ``[orig_x, top_row, slot_w, advance, left_bearing, ink_h, ink_w]``.
  ``orig_x`` is the cumulative sum of ``slot_w`` (the glyph's column in the
  strip).  ``top_row`` is the strip row of the glyph's ink top; the ink
  occupies strip rows ``[top_row, top_row + ink_h)``.  The glyph metrics
  (slot_w/advance/ink_h/top_row) are in standard glyph-id order and indexed
  directly by the cmap value.

Vertical placement
------------------
The baseline sits at strip row ``B`` (computed as the common ``top_row +
ink_h`` of the capital letters A–Z, which rest on the baseline).  Each glyph
is emitted with ``y_off = top_row - B``: the offset, in pixels, from the text
baseline to the *top* of the glyph's ink (negative = above the baseline).
The renderer draws each glyph at ``dst.y = baseline_y + y_off``.

JSON schema
-----------
::

    {
      "face": "clea", "pt": 12, "atlas_w": <int>, "atlas_h": <int>,
      "metrics": {
        "ascender": <int>, "descender": <int>,
        "max_advance": <int>, "line_height": <int>, "baseline": <int>
      },
      "cmap": [<uint16>, ...],            // dense: cmap[codepoint] = glyph_index
      "glyphs": [
        { "atlas_x": <int>, "atlas_y": <int>,
          "slot_w": <int>,  "ink_h": <int>,
          "advance": <int>, "y_off": <int> },
        ...
      ]
    }
"""
from __future__ import annotations

import json
import struct
from collections import Counter
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

            # --- Atlas (bg6a header: 9 × int32, then a 512-byte sub-header,
            #     then width*height raw grayscale bytes at the END of the leaf) ---
            aoff = atlas_leaf.abs_off
            strip_w, row_h = struct.unpack_from("<ii", font_data, aoff + 16)
            # Pixel payload sits at the end of the leaf, after the 36-byte
            # header + a fixed 512-byte sub-header (548 bytes total).  Anchor to
            # the end so any trailing slack is ignored and the header/sub-header
            # are skipped exactly.
            pix_base = aoff + atlas_leaf.size - strip_w * row_h

            # --- Metrics: 4 × int32 = (-ascender, -descender, max_adv, line_h) ---
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
            # f[0]=orig_x  f[1]=top_row  f[2]=slot_w (bitmap width)
            # f[3]=unused  f[4]=left_bearing  f[5]=height-to-baseline
            # f[6]=advance (pen advance; e.g. i/l/!=5, m=15, W=14, space=5).
            # NOTE: f[5] is `baseline - top_row` (ink top down to the baseline),
            # NOT the full ink height — it omits descender rows.  The real ink
            # height is recovered per-glyph by scanning the strip (true_ink_h
            # below); using f[5] directly clips the tails off g/p/q/y/j/Q.
            # NOTE: the advance is f[6], NOT f[3] — f[3] tracks ink_h and gives
            # nonsense spacing (narrow 'i' would advance like a capital).
            n_glyph = big_leaf.size // 28
            raw_recs = [
                struct.unpack_from("<7i", font_data, big_leaf.abs_off + gi * 28)
                for gi in range(n_glyph)
            ]

            # Baseline strip row: capitals A–Z rest on the baseline, so their
            # ink bottom (top_row + ink_h) marks it.  Fall back to line_height.
            cap_bottoms = Counter()
            for cp in range(ord("A"), ord("Z") + 1):
                if cp < cmap_count:
                    gi = cmap_list[cp]
                    if gi < n_glyph:
                        r = raw_recs[gi]
                        cap_bottoms[r[1] + r[5]] += 1
            baseline = cap_bottoms.most_common(1)[0][0] if cap_bottoms else m[3]
            metrics["baseline"] = baseline
            # The binary's stored ascender (-m[0]) is not in pixels; use the
            # empirical baseline strip row instead, which equals the maximum
            # above-baseline extent of any glyph (y_off = top_row - baseline,
            # so baseline = -min(y_off) for the tallest glyphs).
            metrics["ascender"] = baseline

            # Compute the TRUE ink height of a glyph by scanning the strip.
            #
            # The big-table's f[5] is NOT the full ink height — it measures
            # `baseline - top_row`, i.e. the height from the glyph's ink top
            # down to the baseline ONLY, with descender rows (below the
            # baseline) excluded.  Cropping to f[5] therefore chops the tails
            # off 'g','p','q','y','j','Q', … .  Recover the real bottom by
            # scanning the glyph's column band downward for the last non-blank
            # strip row.  (max_advance/descender keep the descender well inside
            # row_h, so we never read past the strip.)
            def true_ink_h(orig_x: int, top_row: int, slot_w: int, fallback: int) -> int:
                if slot_w <= 0:
                    return fallback
                last_nz = -1
                for row in range(top_row, row_h):
                    base = pix_base + row * strip_w + orig_x
                    if any(font_data[base + j] for j in range(slot_w)):
                        last_nz = row
                return (last_nz - top_row + 1) if last_nz >= top_row else fallback

            # Lay glyphs out into rows of ≤_MAX_ROW_WIDTH px so the atlas PNG
            # stays within SDL2's 16384px texture limit.  Each glyph copies its
            # tight ink box [top_row, top_row+ink_h) × [orig_x, orig_x+slot_w)
            # from the strip; vertical placement is recovered at draw time from
            # y_off = top_row - baseline.
            glyph_recs: list[dict] = []
            cur_row_x = 0
            cur_row = 0
            for gi in range(n_glyph):
                orig_x, top_row, slot_w, _h1, _lb, _ink_h_to_baseline, advance = raw_recs[gi]
                ink_h = true_ink_h(orig_x, top_row, slot_w, _ink_h_to_baseline)
                if slot_w > 0 and cur_row_x + slot_w > _MAX_ROW_WIDTH:
                    cur_row += 1
                    cur_row_x = 0
                glyph_recs.append({
                    "orig_x":     orig_x,
                    "top_row":    top_row,
                    "slot_w":     slot_w,
                    "advance":    advance,
                    "ink_h":      ink_h,
                    "new_atlas_x": cur_row_x,
                    "new_row":    cur_row,
                    "y_off":      top_row - baseline,
                })
                cur_row_x += slot_w

            # --- Build the repacked atlas RGBA ---
            num_rows = (max(g["new_row"] for g in glyph_recs) + 1) if glyph_recs else 1
            new_w = max(
                (g["new_atlas_x"] + g["slot_w"]) for g in glyph_recs if g["slot_w"] > 0
            ) if any(g["slot_w"] > 0 for g in glyph_recs) else 1
            new_h = num_rows * row_h
            new_rgba = bytearray(new_w * new_h * 4)
            for g in glyph_recs:
                sw, ih = g["slot_w"], g["ink_h"]
                if sw <= 0 or ih <= 0:
                    continue
                band_top = g["new_row"] * row_h
                for k in range(ih):
                    src = pix_base + (g["top_row"] + k) * strip_w + g["orig_x"]
                    dst = ((band_top + k) * new_w + g["new_atlas_x"]) * 4
                    for j in range(sw):
                        v = font_data[src + j]
                        a = v * 32
                        if a > 255:
                            a = 255
                        o = dst + j * 4
                        new_rgba[o] = 255
                        new_rgba[o + 1] = 255
                        new_rgba[o + 2] = 255
                        new_rgba[o + 3] = a

            stem = f"{face_child.tag}_{pt}pt"
            _write_png_rgba(fonts_dir / f"{stem}_atlas.png", new_w, new_h, bytes(new_rgba))

            # --- Glyph list with atlas coordinates + baseline-relative offset ---
            glyphs = []
            for g in glyph_recs:
                glyphs.append({
                    "atlas_x": g["new_atlas_x"],
                    "atlas_y": g["new_row"] * row_h,
                    "slot_w":  g["slot_w"],
                    "ink_h":   g["ink_h"],
                    "advance": g["advance"],
                    "y_off":   g["y_off"],
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
