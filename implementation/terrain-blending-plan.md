# Terrain rendering — full B15 implementation plan

**Status**: Stages A, B, C, and E are implemented (see
`OpenTE/game/src/render/terrain_renderer.cpp`'s `TerrainRenderer::render`/
`render_edge_blends`/`render_shore_overlays`/`load_textures` and
`OpenTE/tools/extractor/sprites/terrain.py`/`maps/region.py`). Stage D
remains intentionally out of scope (see its section below). This document
covers all terrain rendering RE findings (Rounds 25-37) plus the related
terrain-type/texture mapping work, organized as a staged build order. Stage 0 (the 3D mesh/heightmap/shading foundation)
was already implemented before this plan.

Remaining open items (not blocking, see `rendering.md`'s
"Texture-edge blending and shore overlays" and `implementation/roadmap.md`'s
placeholder table):

- Stage C's `kShoreUvIndex` cell -> diamond-quad UV-rect mapping is a
  documented square-to-diamond approximation, not validated against the
  original's actual atlas layout.

- **RESOLVED (B15 Round 40)**: the `tran`-family sprites ARE the edge-blend
  atlas. `[ebx+0x1bb8]` (= `tran`'s draw-side offset, `init_offset 0x1b3c +
  0x7c`) is read inside the D3DRasterizer's 4-direction neighbor loop and bound as
  the texture for up to 4 additional per-tile draw calls (one per
  differently-textured neighbor direction), each reusing the base quad's
  geometry with new UVs selecting one of the `tran` atlas's 4x4 (`0.25`-UV)
  cells. `terr/edge` is unrelated — it's read separately at draw-offset
  `+0x1c28` by a small standalone function. Stage B should be
  rewritten around the `tran`-atlas/4-direction mechanism; see
  `03-exe-analysis.md` Round 40 for the full writeup and the still-open
  "which of the 4 directions maps to which `[ebx+0x14]` array slot / which
  `tran` cell" detail.

(Stage A.3's 1-13 texture-page index table is resolved — B15 Round 39,
corrects an earlier Round 38 misreading.)

Each stage is independently shippable/visible — don't attempt this as one
big patch.

## B15 round-by-round coverage map

To make it easy to verify this plan accounts for every B15 finding:

| Rounds | Finding | Plan stage |
|--------|---------|------------|
| 25-27 | `alti*10/256` world-height formula, `alti>=13` walkability cutoff, bilinear sampler, `Get/SetTerrainHeight` | Stage 0 (done) |
| 28 | 8-neighbor slope normal + diffuse lighting, `D3DTLVERTEX` output record | Stage 0 (done) |
| 31 | `[this+0xd8] = -45.25*zoom` px/world-height-unit (corrects Round 28's false "always 0"), `+0xe4/+0xe8/+0xec` vertex-grid dims | Stage 0 (done) |
| 32 | Full render call chain (`virtual_20` -> `fcn.0046b620` -> `virtual_244` -> `virtual_252`), D3DTLVERTEX structurally confirmed, `+0x2c0` mask traced | Stage 0 (done) |
| 33 | `[this+0x258]` = Rasterizer object (D3D/Software/SilkRoad, 18-slot vtable), `tu`/`tv` = 16.16 fixed-point, `alti>=13` mask has no render consumer — no skirt pass | Stage 0 (done) |
| 34 | `tu = col/8`, `tv = row/16` — continuous world-space UV, pure function of vertex grid coordinate | **Stage A.0** (not yet done) |
| 35 | Multi-pass blend pipeline: base pass (texture page `mapp.terr & 0xf`), 4 edge-blend passes, 2 shore-overlay passes, decal pass; water-class (`<=2`) vs land-class (`>2`) gating | **Stages A-E** |
| 36 | `terr/sets/<N>` palette table (18 records, 13 fields -> texture pages), per-tile byte = `mapp.terr` confirmed | **Stage A** |
| 37 | `terr/edge` atlas source, `terr/coa0`/`coa1` shore atlases, LUT0/LUT85/UV-index tables, `+0x249/0x24a/0x24b` = "Terrain textures"/"Terrain blending"/"Beaches" option flags | **Stages B-D** |

## Stage 0 — 3D heightmap mesh with slope shading (DONE)

Already implemented in `App::build_terrain_mesh()` / `App::render_terrain()`
([app.cpp](../game/src/core/app.cpp)) and [iso.h](../game/src/render/iso.h).
Listed here so this plan is a complete record of B15.

**What's in place** (Rounds 25-28, 31-33):

- **(width+1) x (height+1) shared-corner vertex grid** — each vertex's
  height is the average of its up-to-4 surrounding tiles' `mapp.alti` bytes
  (water tiles and off-map tiles contribute `Region::sea_level()` instead).
- **Per-vertex height displacement**: `screen_y -= alti_byte *
  kPixelsPerAltiUnit * zoom`, where `kPixelsPerAltiUnit = 45.25 * 10/256 ≈
  1.7676 px/alti-byte` (EXE-confirmed, Round 31).
- **Per-vertex slope shading**: central-difference normal from neighboring
  vertex heights, dotted with a fixed light direction, blended between an
  ambient floor and full brightness. Produces smooth slope-based brightness
  gradients matching the original's per-vertex diffuse lighting. The
  original's exact light direction and ambient constants (`[esi+0x23c]` ≈
  0.137, `[esi+0x240]` ≈ 0.0427) weren't fully recovered — `iso.h`'s
  `kLightDir`/`kAmbient` are reasonable stand-ins.
- **No skirt/edge-cliff pass**: Round 33 confirmed the `alti>=13` mask has
  no renderer-geometry consumer. Steep height differences produce steep
  quads in the displaced mesh naturally.
- **Build-once caching**: vertex height/color grids computed at load time,
  immutable at runtime.

**What Round 34 found that is NOT yet implemented** (the UV scheme — this
is Stage A.0 below):

Round 34 confirmed the original's per-vertex texture coordinates are a pure
linear function of the vertex's absolute grid coordinate: `tu = col/8`,
`tv = row/16` (scale constants derived from `TILE_W=64`/`TILE_H=32` /
`256.0` / `2.0`). This gives continuous cross-tile texture wrapping — shared
vertices have identical UVs, so the texture flows seamlessly across tile
boundaries with no visible seams.

OpenTE uses (and, per A.0 below, continues to use) per-tile `[0,1]` UV
(edge-midpoint convention: NW=(0.5,0), NE=(1,0.5), SE=(0.5,1), SW=(0,0.5)),
which samples each tile's texture independently. This produces visible
alignment discontinuities between adjacent tiles of the same texture — a
problem the edge-blend passes (Stage B) partially mask at *type* boundaries,
but not at same-texture-same-type boundaries (the majority of tile pairs).
Switching to the original's continuous UV was attempted as a prerequisite
for Stage A's texture-page work, but reverted (see A.0) because our
single-tile-sized extracted textures aren't a shared atlas; Stage A's
texture-page *selection* (A.1-A.5) was kept on top of the existing per-tile
UV instead.

## Stage A — Continuous UV + per-tile texture pages

### A.0 — Switch base pass to continuous UV (tried, reverted)

**Goal** (as originally planned): replace the per-tile `[0,1]` edge-midpoint
UV with the original's continuous `uv = (col/8, row/16)` scheme (Round 34),
so adjacent tiles' textures tile seamlessly.

**Outcome: reverted.** This was implemented (`tex_coord = {col/8.0f,
row/16.0f}` per vertex) and produced a completely broken render — every tile
showed a tiny, heavily-magnified ~8x2px sliver of its texture page, creating
a moire/chevron pattern across the whole map. Root cause: this UV scheme
assumes a *large shared atlas* where one `[0,1]` UV unit spans an 8x16-tile
region; our extracted texture pages (A.2) are single 64x32 per-tile images,
so a UV span of `1/8 x 1/16` only covers an 8x2px region of that small
texture, magnified to fill the tile.

Building the large continuous atlas this scheme actually needs (option 2
below, an 8x16-tile/512x512px pre-baked atlas per page) was judged
out-of-scope effort/risk for this pass. **The base pass instead keeps the
per-tile `[0,1]` edge-midpoint UV** (`OpenTE/spec/rendering.md`'s "UV
mapping" bullet) — only Stage A.1-A.5's *texture-page selection* (which page
each tile samples, via `texture_index`) was kept, replacing the old 7-
`TerrainType` mapping. If continuous UV is revisited later, option 2 (pre-
baked atlas) is the only viable path given SDL2's lack of an explicit texture
wrap-mode API:

- **Pre-bake a tiled atlas**: instead of one 64x32 texture per page, build an
  8x16-tile atlas (512x512 px) at extraction time by repeating the base
  texture, and UV-map into it with `[0,1]`-range UVs. This guarantees
  correct tiling regardless of SDL backend, at the cost of larger textures.

The diamond-mask treatment on extracted textures (transparent corners) is
**not needed** with the (retained) per-tile edge-midpoint UV — the quad
geometry only samples the texture's inscribed-diamond region in UV space, so
an unmasked square texture (as extracted by A.2) renders correctly.

### A.1 — Data model changes (per-tile texture-page index)

- **`game_data/maps/<id>.json`**: add a second per-tile byte grid,
  `terrain.texture_index` (1-13, row-major, same RLE/base64 convention as
  `terrain.data`), extracted from `mapp.terr & 0xf` (bounds-checked to
  1..13 per B15 Round 35 — clamp/log if a map ever produces 0 or >13, which
  per the RE notes shouldn't happen but hasn't been exhaustively verified).
  `terrain.data` (the 7-value `TerrainType` enum) **stays** — it's still
  needed for buildability/connectivity rules (water vs. land vs.
  hills/mountains) per [input.md](../spec/input.md) and
  [world-and-maps.md](../spec/world-and-maps.md), which are orthogonal to
  which *texture* is drawn.
- **`game_data/tables/terrain_textures.json`**: new table, one entry per
  texture-page index 1-13, each naming a `terr/<tag>` sprite id (see A.2 for
  which tag). This is the extracted form of one `terr/sets/<N>` palette
  record (13 of its ~15 fields — see A.3 for the index<->field mapping).
- **`world::Region`**: add `std::vector<uint8_t> texture_index;
  // width*height, values 1-13` alongside the existing `terrain` vector.
- **`render::TerrainTextures`** (new small struct/array in `app.cpp` or a
  new `render/terrain_textures.h`): `SDL_Texture* by_index[14]` (1-13 used,
  0 unused/nullptr), loaded from `terrain_textures.json` + the existing
  per-sprite PNG loading path.

### A.2 — Extractor: full `terr/*` texture-page extraction

`OpenTE/tools/extractor/sprites/terrain.py` currently extracts 7
hand-picked `terr/<tag>` leaves (one per `TerrainType`) plus `terr/edge`.
Extend it to extract **every `terr/<tag>` leaf referenced by any field of
the chosen `terr/sets/<N>` palette** (see A.3), using the same
downsample-to-64x32 treatment already implemented. **Re-evaluate the
diamond-mask treatment**: with continuous UV (A.0), the texture should be a
full square tile, not diamond-masked — the quad's geometry defines the
diamond, and the texture needs to tile seamlessly at its edges. If the
original `terr/*` source textures are already full squares (likely — the
mask was an OpenTE extraction convention, not original data), extract
them *without* the diamond mask for the Stage A texture-page textures.
Keep the old diamond-masked versions for backward compatibility with any
code that still references them, or delete them if nothing does.

Concretely: instead of `_TERRAIN_TYPE_TEXTURES: dict[TerrainType, str]` (7
entries), build `_TEXTURE_PAGE_SOURCES: dict[int, str]` (13 entries, index
1-13 -> `terr/<tag>`), populated from the palette record. Each gets
extracted to `sprites/terrain/terrain.page<NN>.png` (or similar — naming is
free, just needs to match `terrain_textures.json`'s sprite ids).

### A.3 — Decode the `terr/sets/<N>` palette record -> 13-slot table

**RESOLVED (B15 Round 39, corrects an earlier Round 38 misreading)** — exact
destination-offset re-read of the terrain-palette init function's order
(`exe_b15_round36_init.txt`, 28-byte/`0x1c` stride from `hidd` at `esi+0`),
cross-checked against `OpenTE/tools/extractor/sprites/terrain.py`'s
already-correct `_TEXTURE_PAGE_SOURCES` table (from a prior session with
direct user input). The per-tile texture-page table used by `mapp.terr &
0xf` (bounds-checked to **1..13**) is:

```
index:  1     2     3     4     5     6     7     8     9     10    11       12    13
field:  deep  seas  alps  bld0  bld1  bld2  hill  mntn  undr  soil  (none)   dsr0  dsr1
```

- `hidd` (slot 0, `esi+0x00`) is **not** part of this table — `mapp.terr &
  0xf` is bounds-checked `0 < eax < 14`, so index 0 is never selected via
  `mapp.terr`. Per the user (2026-06-12), `terr/hidd` is a starfield texture
  tiled *behind* the map (visible past its edges), not a fog-of-war overlay.
  OpenTE doesn't need it for the current per-tile texture-page work.
- **Index 11** is **never written** by the terrain-palette init function — no
  `terr.sets` field maps to it. Leave it unmapped (renderer falls back to
  flat shading for `texture_index == 11`), matching `terrain.py`.
- **Index 13 = `dsr1`**, not `tran`. `tran` (and its palette variants
  `tr14`/`tr15`/`tr17`/`trch`) is written to a *separate* offset
  (`esi+0x1b3c`), grouped with `coa0`/`coa1`/`edge`/decoration refs — it is
  **not** a 14th slot in this table. Visual inspection
  (`extracted/m_ui_terr_sprites/terr_tran_*.png` etc.) confirms `tran`-family
  sprites are 4x4 dithered-dot dissolve atlases, not ground textures — see
  `03-exe-analysis.md` Round 39. Its actual consumer (which blend pass reads
  `esi+0x1b3c`'s draw-side counterpart) is a new **open item for Stage B**
  (see below).

This 1-13 table (11 unmapped) is confirmed from both the disassembly and
`terrain.py`'s existing extraction code.

### A.4 — Which `terr/sets/<N>` palette (`N`, 0-17) does `ep01_china` use?

**Open RE question carried over from B15/B36 — not resolved by this plan.**
Two pragmatic options, either is fine to ship Stage A with:

1. **Guess-and-visually-validate**: try `N=0` first (often the "default"
   slot in these tables), render, compare to the user's screenshot,
   iterate. China is a temperate/agricultural episode, so palette fields
   like `hill`/`soil`/`undr` should look like grass/dirt, not desert/snow.
2. **Hardcode a per-episode override table** (`{"ep01": 0, ...}`) in
   `terrain_textures.json`'s extraction config, defaulting to `N=0`, and
   revisit per-episode as more maps are extracted (Stage 8 / multi-region
   work). This avoids blocking Stage A on a definitive answer for all 18
   episodes at once.

Either way, **do not block Stage A on fully resolving this** — a
plausible-looking palette for `ep01_china` is enough to validate the
pipeline; refinement is the same kind of "polish, not blocking" item B7
already flagged for the `TerrainType` mapping.

### A.5 — Renderer change

In `App::render_terrain()`, replace the `TerrainType -> texture` lookup with
`texture_index[ty*width+tx] -> texture` (via the new `TerrainTextures`
table). Per-vertex height/color and `SDL_RenderGeometry` call are
**unchanged** from the current implementation. UV mapping uses the
continuous `(col/8, row/16)` scheme from A.0 (already in place by this
point).

**Exit criteria for Stage A**: build + run `opente.exe`, China map renders
with up to 13 distinct ground textures (vs. today's 7), continuously tiled
with no per-tile seams, still smooth 3D mesh with slope shading. Ask the
user for a screenshot to sanity-check the palette choice (A.4).

## Stage B — Edge-blend passes (feathered neighbor-texture overlay) — IMPLEMENTED

**Status (2026-06-13)**: implemented in
`TerrainRenderer::render_edge_blends` (`terrain_renderer.cpp`), gated by
`terrain_blending_enabled` (B.5). The extractor
(`tools/extractor/sprites/terrain.py`) extracts `terr/trch` (the `chi1`
palette's `tran` field, per `data_catalog_terr_sets.txt` set 16) as
`terrain.tran`, full 256x256 resolution, and records it in
`terrain_textures.json`'s `"tran"` field. Per-tile candidate detection (B.2)
and the cell-selection UV formula (B.3, `col = nbr & 3, row = nbr >> 2`,
`(cell*64+0.5)/256` with a half-texel inset on both edges) are implemented as
specified below.

**Update (2026-06-13, full disassembly of the D3DRasterizer's corner-rotation
arithmetic)**: the
per-direction corner rotation is now derived rather than placeholder. The
original copies the tile's 6-vertex (2-triangle, fan-style: center + 4
corners + repeated first corner) geometry into a scratch buffer and rewrites
only vertices 1-4's (the 4 corners') `tu`/`tv`. For direction `dir` (1/3/5/7
= N/E/S/W), let `k_raw = ((dir+1)&7)>>1` (N->1, E->2, S->3, W->0). Tile corner
`j` (NW=0,NE=1,SE=2,SW=3) is assigned `tran`-cell corner `m = (j - k) mod 4`,
where `corner_uv[m]` for `m=0..3` is `(u1,v1),(u0,v1),(u0,v0),(u1,v0)`
(`u0,v0` = the cell's near-edge UV, `u1,v1` = its far-edge UV, both with the
half-texel inset from B.3). This is implemented as `kEdgeBlendK` + the
`corner_u`/`corner_v`/`m` logic in `render_edge_blends`. (Vertex 0, the fan's
center vertex, gets the cell's center UV in the original but has no
equivalent in this renderer's 2-triangle-via-index-buffer quad, so it's
dropped — only the 4 corners matter for a flat-shaded quad.)

**Update (2026-06-13, visual check)**: `k_raw = {1,2,3,0}` (using vertex-slot
index directly as screen corner `j`) rendered the whole pattern rotated 90
degrees from correct. The first correction tried (`k_raw - 1 = {0,1,2,3}`)
was still off by 90 degrees the other way; the working value is
`kEdgeBlendK = {2,3,0,1}` (`k_raw + 1`) — i.e. the EXE's vertex-slot-to-
screen-corner mapping has a cyclic offset of -1 (`j = (vertex_slot - 1) mod
4`). This is now `kEdgeBlendK`'s value in `terrain_renderer.cpp`.

**Update (2026-06-13, scale)**: with the rotation fixed, the dissolve dots
looked noticeably smaller/denser than the base terrain texture's features.
Sampling the *full* 64px `tran` cell (`u1 = u0 + 63/256`, the raw EXE
formula) onto the same screen quad as the base pass gives a ~1x:1x
(horizontal) / ~1x:0.5x (vertical) atlas-px-to-screen-px ratio, vs. the base
pass's 2x:2x (32x16px atlas region -> 64x32px screen diamond,
terrain-blending-plan.md Stage A.0). To match that apparent scale, only half
of each cell (32x32px, `kTranSampleUV = 32/256`) is sampled per tile now —
see `kTranSampleUV`'s doc comment in `terrain_renderer.cpp`. This is an
empirical visual-match adjustment, not derived from the disassembly (the EXE
itself samples the full cell — it may rely on a different screen-quad size
for the edge-blend pass that we haven't identified).

Other palettes' `tran`/`tr14`/`tr15`/`tr17` atlases are not yet
extracted/selected per-episode (only `ep01_china`/`chi1` is). B.1/B.4 (the
`terr/edge`-based plan) are superseded by Round 40's `tran`-atlas finding and
no longer apply.

> **CORRECTION (B15 Round 40, 2026-06-13)**: B.1-B.5 below were written
> assuming the edge-blend atlas is `terr/edge` with cells selected by the
> *neighbor's* texture index (`nbr&3, nbr>>2`). Round 40 (see
> `03-exe-analysis.md`) found this is wrong: the atlas is actually **`tran`**
> (a 4x4 dithered-dissolve atlas, one per palette — `tr14`/`tr15`/`tr17`/
> `trch`/`tran`), each cell is exactly `0.25x0.25` UV (with a half-texel
> inset), and the cell is selected by the **direction index** (1/3/5/7) via
> a modular-arithmetic formula in the D3DRasterizer (from the disassembly
> dump), NOT by the neighbor's texture index. `terr/edge`
> is unrelated — a separate single texture read at draw-offset `+0x1c28` by
> a different, tiny function, likely a map-border/skirt
> texture, out of scope here. **B.1-B.5 need a rewrite** around the `tran`
> atlas before implementation — in particular B.1's atlas-content
> description, B.3's UV-cell-per-neighbor scheme (replace with UV-cell-per-
> direction), and B.4's `terr/edge`-specific fallback. The gating condition in
> B.2 (same-class, different-index neighbor) and B.5 (option toggle) remain
> correct. The exact direction->cell mapping needs one more disassembly pass
> (the modular arithmetic in the disassembly) before B.3 can
> be written precisely — left as a follow-up for whoever picks up Stage B.

**Goal**: implement the up to 4 additional draw passes (directions 1, 3, 5,
7 — i.e. N/E/S/W in the clone's 8-direction order from
[world-and-maps.md](../spec/world-and-maps.md), B15 Round 35) that overlay a
neighbor tile's texture with a feathered alpha, when the neighbor is the
**same class** (water-class `idx<=2` vs land-class `idx>2`) but a
**different texture index**.

### B.1 — Extract `terr/edge` at full resolution, inspect layout

`terrain.edge.png` is already extracted (64x32, downsampled, no diamond
mask — per the file listing from the prior session). Before writing any
code: **re-extract it at full native resolution (256x256, no downsample) and
visually inspect it** (read the PNG with the `Read` tool, which supports
images). Round 35/37 establish that it's used as a **4-column cell grid**
keyed by `(nbr_tex & 3, nbr_tex >> 2)` for `nbr_tex` in 1-13 — i.e. up to a
4x4 grid (columns 0-3, rows 0-3, though `nbr_tex` only reaches 13 so row 3
only has columns 0-1 populated, `nbr_tex` 12-13). Confirming the actual cell
boundaries/content visually (does each cell look like a small
feathered-alpha thumbnail of *that* texture-page's appearance, or something
more generic/abstract?) is **necessary before writing the UV-cell math** —
the round notes establish the *selection* scheme but the *visual content*
of each cell needs eyeballing.

### B.2 — Per-tile edge-blend candidate detection

For each tile `(tx,ty)` and each of the 4 directions `{1,3,5,7}` (N/E/S/W —
edge-adjacent, not corner-adjacent neighbors):

```
nbr = texture_index[neighbor(tx,ty,dir)]
own = texture_index[tx,ty]
same_class = (own <= 2) == (nbr <= 2)   // both water-class or both land-class
if same_class and nbr != own:
    queue an edge-blend pass: (dir, nbr)
```

Map edges (`neighbor` out of bounds): per B15, treat as "no blend" (skip) —
consistent with how the height mesh already treats off-map tiles specially
for height but the *texture* blend pipeline has no off-map fallback
documented; skip is the safe default. **Open item**: confirm this doesn't
leave visible hard edges at map borders worse than today (it shouldn't —
today *every* edge is hard).

### B.3 — Edge-blend draw pass

For each queued `(dir, nbr)`:

- Build a second `SDL_Vertex[4]` array for the same tile quad (same screen
  positions/heights/colors as the base pass — only UVs differ).
- UV per corner = the `terr/edge` atlas cell for `nbr`
  (`col=nbr&3, row=nbr>>2`, each cell `0.25 x 0.25` in atlas-UV space, i.e.
  `u in [col*0.25, (col+1)*0.25]`, `v in [row*0.25,(row+1)*0.25]`), **rotated
  per `dir`** — Round 35: `(dir+4..7)&3`-style rotation of which tile-corner
  maps to which cell-corner, so the feather always fades away from the
  shared edge with `nbr`'s tile. Concretely: the cell-corner assignment
  rotates by `((dir - 1) / 2) % 4` steps (dir 1->rotation 0, dir 3->1, dir
  5->2, dir 7->3) relative to the base NW/NE/SE/SW corner order — **derive
  the exact rotation direction empirically** (render one tile with a single
  forced edge-blend pass in each of the 4 directions against a
  high-contrast `terr/edge` cell, and check which screen-edge the feather
  hugs; fix the rotation sign until it hugs the correct edge).
- `SDL_RenderGeometry` with `SDL_BLENDMODE_BLEND` (additive-over, alpha from
  `terr/edge`'s own per-cell alpha feather — this is why B.1's visual
  inspection matters: the feather has to be *baked into* `terr/edge`'s alpha
  channel for a single extra draw call to work; if it isn't, this stage
  needs a separate procedural alpha gradient instead, which is a bigger
  change — see "Fallback" below).
- Draw all queued edge-blend passes for a tile **after** that tile's base
  pass and **before** moving to the next tile (so a tile's own blends don't
  get covered by a later tile's base pass — painter's-order is still
  row-major/tile-by-tile, same as today).

### B.4 — Fallback if `terr/edge` has no baked alpha feather

If B.1's inspection shows `terr/edge` is just flat texture swatches with no
gradient (i.e. the "feather" is computed per-vertex in D3D, not baked into
the texture), implement the feather as a **per-vertex alpha gradient on the
edge-blend pass's `SDL_Vertex::color.a`**: the two corners on the shared
edge with `nbr` get `alpha=255`, the two corners on the opposite edge get
`alpha=0`. This is strictly simpler than the original (which may do a
smoother 2D falloff) but achieves the visible goal (neighbor's texture fades
in near the shared edge) with zero new art-asset requirements — **prefer
this fallback unless B.1 shows clear baked-feather art**, since it avoids
depending on `terr/edge`'s exact cell layout entirely (only `nbr`'s own
*base* texture is needed, reusing Stage A's `TerrainTextures` table — no
`terr/edge` extraction needed at all).

### B.5 — Gating

Add a renderer-level bool (`bool terrain_blending_enabled = true;` on `App`
or a render-settings struct), mirroring the original's `TMapView+0x24a`
"Terrain blending" option (B15 Round 37). Not exposed in UI yet (no options
menu exists) — just a code-level toggle for debugging/perf comparison.

**Exit criteria for Stage B**: build + run, screenshot shows soft transitions
between adjacent same-class differing-texture tiles (e.g. between two
different land textures) instead of hard diamond edges. Water/land
boundaries are **not** expected to look different yet (different class ->
no edge-blend; that's Stage C's job).

## Stage C — Shore-overlay passes

**Goal**: implement the up to 2 additional passes that draw shore/beach
transition art at water/land boundaries, using the
`LUT0`/`LUT85`/shape/UV-index tables from
`the extracted shore-overlay tables (RE artifacts)`.

### C.1 — Extract `terr/coa0` and `terr/coa1`

Not yet extracted. Both are 16x16-cell atlases (per Round 37); extract at
native resolution, no downsampling/masking (the cell UVs are computed
exactly via the decode tables, not the diamond-mask convention used for base
textures). Add to the extractor as two new fixed sprite ids (e.g.
`terrain.coa0`, `terrain.coa1`), similar to how `terr/edge` was added.

### C.2 — Per-tile water-neighbor mask -> `LUT0`

For each tile, compute an 8-bit mask: bit `i` (for the 8 neighbor
directions, same `0..7` order as [world-and-maps.md](../spec/world-and-maps.md)'s
connectivity directions) set iff neighbor `i`'s texture-page index is
"type 2" (per B15 Round 37 — re-confirm "type 2" means `texture_index==2`
specifically, vs. "`<=2`" used for B's class check; the round notes say
"neighbor is type-2/water" as an 8-bit mask, distinct from B's
water-*class* check which is `<=2`).

- `overlay1_code = LUT0[mask]` (256-byte table from
  `exe_b15_round37_shore_tables.txt`). `255` = "none, skip overlay pass 1".
  Values `0..52` index directly into the 53-cell UV-index array (page
  `coa0`); values `>=53` (up to 59) select page `coa1` with index
  `overlay1_code - 53` (re-check this offset arithmetic against the round's
  notes — the doc says ">=53 selects shore-atlas page 1" but the exact
  re-basing of the index for page 1 should be read directly from the round
  notes/LUT0 dump, not assumed).
- If `overlay1_code != 255`: draw shore-overlay pass 1 — `SDL_Vertex[4]`
  with UVs from `UV-index array[overlay1_code]` (each entry is `(idx0,idx1)`
  in 2-15, used as `idx/16` cell anchors into the 16x16 atlas — i.e. cell
  size `1/16` in atlas-UV space, same float-array table
  `{0/16..16/16}` table gives the exact corner coordinates). `SDL_RenderGeometry`,
  `SDL_BLENDMODE_BLEND`, atlas = `coa0` or `coa1` per above.

### C.3 — Corner-bit `flags` -> `LUT85` -> shape -> overlay pass 2

- Compute `flags` from the 4 *corner* neighbors' type-2 status (Round 37:
  "corner-bit-pattern-derived flags" — the exact bit layout (which corner =
  which bit, and whether edge-neighbors also contribute) needs to be read
  directly from `exe_b15_round37_shore_tables.txt`'s `LUT85` derivation
  section, not guessed; re-open that file when implementing this step).
- `dl = LUT85[flags - 1]` (85-byte table; `15` = "none, skip overlay pass
  2"; valid range `0-14`).
- `shape = dl_to_shape[dl]` via the fixed table:
  `{0:1,1:2,2:3,3:0,4:5,5:4,6:6,7:9,8:8,9:7,10:10,11:11,12:12,13:13,14:14}`.
- If `dl != 15`: the full "`flags -> overlay2 shape`" table in
  `exe_b15_round37_shore_tables.txt` lists only 15 of 85 `flags` values as
  producing a shape 0-14 (the rest are "no overlay 2") — use that table
  directly (it's presumably `flags -> dl -> shape` composed, i.e. just a
  84-entry direct lookup; clarify during implementation whether `LUT85` and
  the "full flags table" are the same data presented two ways, or two
  separate steps — re-read the extracted file, don't assume).
- `shape` (0-14) indexes into the same 53-cell UV-index array (shapes are a
  subset of the 53 cells — confirm the shape->UV-index-array-entry mapping
  in the extracted file; it may be `shape` used directly as an index, or via
  another small table).
- Draw shore-overlay pass 2 with the resulting UVs, same atlas/blend setup
  as C.2.

### C.4 — Gating

Mirror `TMapView+0x24b` "Beaches" — a second debug toggle
(`bool shore_overlays_enabled = true;`), independent of Stage B's
`terrain_blending_enabled`.

**Exit criteria for Stage C**: build + run, screenshot shows shore/beach
transition art at water/land boundaries instead of (or in addition to) any
Stage B edge-blend at those boundaries (recall Stage B explicitly skips
differing-class neighbors, so water/land edges were untouched until now).

## Stage D — Decal pass (roads/rivers)

**Goal**: the 5th D3D pass (roads/rivers drawn as a decal over the blended
terrain).

**Recommendation: defer indefinitely, re-scope when Stage 3
(pathways/connectivity, [roadmap.md](roadmap.md)) is implemented.** Per
[roadmap.md](roadmap.md)'s placeholder table, "Road/trail tiles use flat
debug overlay, not real connection-sprite variants" is already a tracked
Stage 3 placeholder — the decal pass is the *same* underlying feature
(connection-dependent road/river sprite drawn over terrain) and should be
designed once alongside Stage 3's connectivity-sprite-variant work, not
bolted onto the terrain renderer ahead of time. No further detail here;
this stage exists in this plan only to record that it's B15's 5th pass and
intentionally out of scope until Stage 3.

## Stage E — "Terrain textures" master gate (`TMapView+0x249`)

Round 37 found that `+0x249` ("Terrain textures") gates the **entire**
textured-terrain path in `fcn.0046b620` — when off, terrain is drawn
without textures at all (presumably a wireframe/flat-shaded fallback for
low-end hardware). OpenTE doesn't need a "no textures" mode, but **a
code-level `bool terrain_textures_enabled = true;` toggle** (sibling of
B.5's `terrain_blending_enabled` and C.4's `shore_overlays_enabled`)
completes the set of three original options. Implement alongside or after
whichever of B/C lands first — trivial (skip the texture bind in
`render_terrain()`'s draw loop, or draw solid-colored quads using only the
slope-shading vertex colors).

## Cross-cutting notes

- **Performance**: Stage A.0 is free (same draw call count, different UVs).
  Stage A.1-A.5 is free (same draw call count, different texture lookup).
  Stages B/C add up to 6 extra `SDL_RenderGeometry` calls per tile in the
  worst case (4 edge-blends + 2 shore-overlays), but in practice most tiles
  have 0 (interior same-texture tiles) — only boundary tiles between
  different texture-pages/classes pay the cost. For a 128x128 map this is
  still at most ~98K extra draw calls in the worst pathological case (every
  tile a different texture from every neighbor) — if profiling shows this
  matters, batch by atlas texture (group all edge-blend passes using the
  same `terr/edge`/`coa0`/`coa1` atlas across the whole visible area into
  one `SDL_RenderGeometry` call with a larger vertex/index buffer) before
  considering anything more invasive. Don't pre-optimize — measure first,
  per CLAUDE.md's general guidance against speculative work.
- **Caching**: like the base mesh (`App::build_terrain_mesh()`), the
  per-tile pass *lists* (which directions need edge-blends, which shore
  shapes apply) are static for an immutable map — compute once at load time
  into a `std::vector<TileBlendPlan>` (or similar), not per-frame.
- **`SDL_BLENDMODE_BLEND` + opaque base pass**: ensure the base pass uses
  `SDL_BLENDMODE_NONE` or writes alpha=255 (it's currently opaque terrain,
  this should already hold) so that subsequent `BLEND`-mode passes correctly
  composite over it.
- **Spec doc updates** (do this as each stage lands, not all upfront):
  `rendering.md`'s "Deferred: texture blending" subsection should be
  updated as stages land, and `roadmap.md`'s placeholder-inventory row 205
  updated similarly. `world-and-maps.md`'s map-file-format JSON schema
  needs the new `terrain.texture_index` field documented (Stage A.1).
  `rendering.md`'s "UV mapping" paragraph should be updated to document the
  continuous `(col/8, row/16)` scheme once A.0 lands.

## Summary of open RE items this plan depends on (none block starting A.0)

1. **A.0**: whether `SDL_RenderGeometry` wraps UVs outside `[0,1]` on the
   target SDL backend — test empirically; fall back to pre-baked atlas if
   not.
2. **A.3**: resolved — index 11 has no source and stays unmapped, `hidd` is
   not part of the 1-13 table (B15 Round 39). Remaining: `tran`-family
   atlases' draw-side consumer (new Stage B item, see "Remaining open
   items" above).
3. **A.4**: which `terr/sets/<N>` (`N` 0-17) palette `ep01_china` (and other
   episodes) use — guess `N=0`, validate visually, refine later.
4. **B.1**: whether `terr/edge` has baked per-cell alpha feathers (use as
   designed) or is flat swatches (use the B.4 per-vertex-alpha fallback,
   which sidesteps needing `terr/edge` at all) — also re-check the
   `tran`-family dithered-dot atlases (`tr14`/`tr15`/`tr17`/`trch`/`tran`)
   as a candidate alternative source for this pass (Round 39).
5. **B.3**: exact corner-rotation direction for edge-blend UVs per
   direction — derive empirically with a high-contrast test texture.
6. **C.2/C.3**: precise bit-layout of the water-neighbor `mask` and
   corner-derived `flags`, and the exact `LUT0`/`LUT85`/shape/UV-index
   composition — all should be re-derived directly from
   `the extracted shore-overlay tables (RE artifacts)` during
   implementation rather than from this plan's paraphrase, which may have
   simplified/elided details.

None of these require new EXE disassembly sessions to *start* — they're
either "read the existing extracted table file more carefully", "test on the
target machine", or "render and visually compare", all implementation-time
activities.
