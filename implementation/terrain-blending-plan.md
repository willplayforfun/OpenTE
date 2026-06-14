# Terrain rendering — RE findings index & remaining work

This document indexes the B15 reverse-engineering rounds (Rounds 25-40,
`documentation/03-exe-analysis.md`) that informed terrain rendering, tracks
the remaining open items, and describes the one deferred stage (D — road/
river decals). **Stages 0, A, B, C, and E are fully implemented** in
`TerrainRenderer` (`OpenTE/game/src/render/terrain_renderer.cpp`) and the
extractor (`OpenTE/tools/extractor/sprites/terrain.py`,
`maps/region.py`).

## B15 round-by-round coverage map

| Rounds | Finding | Implementation |
|--------|---------|----------------|
| 25-27 | `alti*10/256` world-height formula, `alti>=13` walkability cutoff, bilinear sampler | `TerrainRenderer` constructor, `iso.h` |
| 28 | 8-neighbor slope normal + diffuse lighting, `D3DTLVERTEX` output | `TerrainRenderer` constructor (vertex colors) |
| 31 | `[this+0xd8] = -45.25*zoom` px/world-height-unit (corrects Round 28's false "always 0") | `iso.h` `kPixelsPerAltiUnit` |
| 32 | Full render call chain, D3DTLVERTEX confirmed, `+0x2c0` mask | `TerrainRenderer::render()` |
| 33 | Rasterizer object (18-slot vtable), `tu`/`tv` = 16.16 fixed-point, `alti>=13` mask has no render consumer — no skirt pass | No skirt pass needed |
| 34 | `tu = col/8`, `tv = row/16` — continuous world-space UV | *Not used* — per-tile `[0,1]` edge-midpoint UV retained (see "Continuous UV" below) |
| 35 | Multi-pass blend pipeline: base + 4 edge-blend + 2 shore-overlay + decal; water-class vs land-class gating | `render()` / `render_edge_blends()` / `render_shore_overlays()` |
| 36 | `terr/sets/<N>` palette (18 records, 13 fields -> texture pages), per-tile byte = `mapp.terr` | `terrain.py` `_TEXTURE_PAGE_SOURCES`, `region.py` texture_index extraction |
| 37 | `terr/edge` atlas, `terr/coa0`/`coa1` shore atlases, LUT0/LUT85/UV-index tables, option flags `+0x249/0x24a/0x24b` | `render_shore_overlays()`, `terrain_renderer.h` toggle bools |
| 38-39 | 1-13 texture-page index table (Round 39 corrects Round 38), `tran`-family dithered-dissolve atlases identified | `_TEXTURE_PAGE_SOURCES` table, `tran` extraction |
| 40 | `tran` atlas IS the edge-blend source (not `terr/edge`); 4-direction neighbor loop with `0.25`-UV cells; `terr/edge` is unrelated (separate offset `+0x1c28`) | `render_edge_blends()` uses `tran` atlas |

## Continuous UV — implemented

Round 34's `(col/8, row/16)` UV scheme is now active. The extracted texture
pages are full native-resolution 256x256 seamlessly-tileable images (not
64x32 per-tile crops — that was the earlier broken configuration). Each tile
samples a 32x16 texel region (1/8 x 1/16 of the texture), 2x-magnified
onto the 64x32 screen diamond. UV origin is `fmod(tx,8)/8` /
`fmod(ty,16)/16` per tile (not per vertex) to simulate wrap mode without
SDL2 API support.

The base pass uses a 5-vertex center-based fan (matching the original's
`D3DPT_TRIANGLEFAN` geometry) rather than a 2-triangle diagonal split,
eliminating the diagonal-seam texture artifact on height-displaced tiles.

## Remaining open items

These are non-blocking accuracy/completeness gaps, not implementation
blockers.

1. **Per-episode palette selection** — only `chi1` (set 16, `ep01_china`)
   is currently extracted/used. Other episodes' `terr/sets/<N>` palette
   choice is unresolved. Hardcoded `N=0` default, visually validate per
   episode as more maps are tested.

2. **`kShoreUvIndex` approximation** — the cell-to-diamond-quad UV-rect
   mapping in `render_shore_overlays()` is a documented square-to-diamond
   approximation, not validated against the original's actual atlas layout.

3. **`kTranSampleUV` half-cell sampling** — only half of each 64px `tran`
   cell (32px, `kTranSampleUV = 32/256`) is sampled per tile, an empirical
   visual-match adjustment. The EXE samples the full cell — the discrepancy
   likely stems from a different screen-quad size in the original's
   edge-blend pass that hasn't been identified.

4. **`kEdgeBlendK` rotation** — the `{2,3,0,1}` corner-rotation mapping was
   derived empirically (two incorrect values tried first). The EXE's
   `k_raw = ((dir+1)&7)>>1` formula gives `{1,2,3,0}`, but vertex-slot-to-
   screen-corner has a cyclic offset of -1 in practice. Functionally correct
   but the *why* is unexplained.

5. **`terr/edge` purpose** — confirmed NOT the edge-blend atlas (that's
   `tran`). Read at draw-offset `+0x1c28` by a small standalone function.
   Likely a map-border/skirt texture. Not currently used.

6. **Other palettes' `tran` atlases** — `tr14`/`tr15`/`tr17`/`tran` (vs.
   `trch` currently extracted) are not yet extracted or selected per-episode.

## Stage D — Decal pass (roads/rivers) — DEFERRED

The 5th D3D pass draws roads/rivers as a decal over blended terrain.
Deferred until Stage 3 (pathways/connectivity, per `roadmap.md`) — the
decal pass and Stage 3's connection-sprite-variant work are the same
underlying feature and should be designed together.

## Texture-page index table (reference)

From B15 Round 39 (corrects Round 38), cross-checked against
`terrain.py`'s `_TEXTURE_PAGE_SOURCES`:

```
index:  1     2     3     4     5     6     7     8     9     10    11       12    13
field:  deep  seas  alps  bld0  bld1  bld2  hill  mntn  undr  soil  (none)   dsr0  dsr1
```

- `hidd` (slot 0) is not part of this table — `mapp.terr & 0xf` is
  bounds-checked `0 < eax < 14`, so index 0 is never selected. `terr/hidd`
  is a starfield texture tiled behind the map (visible past its edges).
- Index 11 is never written by the terrain-palette init function — left
  unmapped (renderer falls back to flat shading).
- Index 13 = `dsr1`, not `tran`. `tran` is written to a separate offset
  (`esi+0x1b3c`) and is the edge-blend dissolve atlas, not a ground texture.
