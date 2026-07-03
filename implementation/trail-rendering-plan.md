# Trail Segment Rendering Plan

> **STATUS (2026-07-02): COMPLETE.** All phases implemented; trail, road,
> rail, and canal render from the correct atlas cells. Late corrections
> this plan predates (see `documentation/extracted/path-rendering-handoff.md`
> §0): (1) trail is the LOW-nibble network and road the HIGH nibble; (2)
> connectivity byte 2 = RAIL (the 41-code accumulated-LUT network, page
> 0xf6) and byte 3 = CANAL (the 33-code 8-directional network, page 0xfa)
> — this plan's byte names for those two networks are reversed throughout.
> Bridges are planned separately in `bridge-plan.md`.

This plan covers **Stage D** of the terrain pipeline — the decal pass that draws
trail, road, rail, and canal segments over the blended terrain mesh.  Stages A–C
and E are already implemented in `TerrainRenderer`; this plan picks up where
`terrain-blending-plan.md` left off.

**Reference files (read before starting any phase):**
- `OpenTE/implementation/terrain-blending-plan.md` — Stage D note, context
- `documentation/extracted/exe_b15_overlay_atlas_reference.md` — fully decoded
  shore pipeline (LUT0/LUT85/UV-index tables, diamond geometry, known traps)
- **`documentation/extracted/exe_trail_re_findings.md`** — **COMPLETE RE
  findings** for trail/road/canal/rail decal pipeline (LUT tables, connectivity
  mask layout, atlas page assignments, init functions). **Read this before
  implementing Phases 4-6.**
- `OpenTE/game/src/render/terrain_renderer.cpp` — `render_shore_overlays()`
  is the implementation template (UV mechanics are identical)
- `OpenTE/spec/world-and-maps.md` §"Connectivity" — `TileConnectivity` data model
- `documentation/03-exe-analysis.md` Rounds 16/35 — mask-byte layout and
  the 5-pass D3D pipeline (Round 35, "decal" = 5th pass)

---

## What we already have

### Atlases (already extracted)

`documentation/extracted/m_ui_terr_sprites/` contains the trail/road/rail/canal
atlases — same 256×256 size as the shore atlases.  No extractor work is needed
to produce the raw PNGs; they just need registering in the manifest.

| Sprite tag (raw)       | Network | Culture | Notes |
|------------------------|---------|---------|-------|
| `terr_trai_chi1`       | Trail   | China   | primary variant |
| `terr_trai_chi2`       | Trail   | China   | secondary variant (see §Open questions) |
| `terr_trai_eur1`       | Trail   | Europe  | primary |
| `terr_trai_eur2`       | Trail   | Europe  | secondary |
| `terr_trai_ind1`       | Trail   | India   | primary |
| `terr_trai_ind2`       | Trail   | India   | secondary |
| `terr_trai_per1`       | Trail   | Persia  | primary |
| `terr_trai_per2`       | Trail   | Persia  | secondary |
| `terr_cana_chi1`       | Canal   | China   | |
| `terr_cana_eur1`       | Canal   | Europe  | |
| `terr_cana_ind1`       | Canal   | India   | |
| `terr_cana_mon1`       | Canal   | (?)     | extra culture — see §Open questions |
| `terr_cana_per1`       | Canal   | Persia  | |
| `terr_rail_chi1`       | Rail    | China   | |
| `terr_rail_eur1`       | Rail    | Europe  | |
| `terr_rail_ind1`       | Rail    | India   | |
| `terr_rail_per1`       | Rail    | Persia  | |
| `terr_grid`            | ?       | —       | 256×256; purpose TBD (see §Open questions) |

**Notable absence: no `terr_road_*` atlas** — see §Open questions.

Bridge/rail-bridge sprites (`terr_brid_*`, `terr_rbrd_*`) are separate sprite-sheet
items, not atlas cells, and are out of scope for this plan.

### Shore pipeline as template

The shore overlay code (`render_shore_overlays()`, `kShoreLUT0`, `kShoreLUT85`,
`kShoreDlToShape`, `kShoreUvIndex`) is production-ready and is the **exact
template** for trail rendering.  The same 3-stage pipeline applies:

1. 8-bit connectivity neighbor mask → cell code via a 256-entry LUT0
2. 4-corner diagonal neighbor flags → shape via 85-entry LUT85 + 15-entry
   dl-to-shape jump table
3. Cell/shape → atlas UV via the 53-entry `kShoreUvIndex` + float-array formula

The UV index array and float-array formula (`float_arr[k] = (k-2)/16`) are expected
to be **identical** for all overlay types (same 64×32 diamond grid).  Confirm
whether the EXE reuses `0x5f8940`/`0x5f88f0` or has a separate copy for
road/trail.

### Known LUT pitfalls (from shore work)

These gotchas hit us on `kShoreLUT85` and `kShoreDlToShape` and will apply to
the analogous trail LUTs:

- **LUT85 tail alignment**: the 85 entries are NOT evenly distributed.  Valid
  `flags` values (subset-sums of {1,4,16,64}) land at specific indices, and
  the SW-corner group (indices ≥ 63) must stay at those high positions.  An
  off-by-2 error earlier shifted the tail and mis-selected SW-corner cells.
  When reading the EXE bytes, count every byte in the 85-entry block — do not
  skip apparent `15` runs.
- **Jump table dl ordering**: stubs in memory are NOT in `dl` index order.
  Always index through the jump table (dispatch at `0x42b560` for shores);
  do not read stub addresses sequentially.  The wrong ordering was the source
  of a scrambled shape table that looked plausible but drew wrong corner pieces.

---

## Open questions — RESOLVED by RE

### 1. What renders "roads"?  **RESOLVED**

Roads and trails share the same atlas pair (`terr_trai_XXX1` / `terr_trai_XXX2`).
The decal pipeline uses a single accumulated connectivity code that encodes per-
direction "road" (bits 0–3) vs "trail" (bits 4–7) independently — so road-only
connections use cells 0–10 of `trai1`, trail-only use cells 11–21, and mixed
road/trail connections use cells 22–52 (trai1) and 0–27 (trai2).  There is no
separate `terr_road_*` atlas.

### 2. Purpose of the two `trai` variants per culture  **RESOLVED**

`trai1` and `trai2` are **NOT** two overlay passes (unlike `coa0`/`coa1` for
shores).  They are a single atlas split in half by cell count:
- `trai1` (page 0xf4) = cells 0–52 (code 0–52 from the trail LUT)
- `trai2` (page 0xf5) = cells 53–80 → drawn as cells 0–27 on that page

The split point is exactly 53 cells per page (= `kShoreUvIndex` array length).
Selection: `atlas = trail_code / 53`, `cell = trail_code % 53`.  There is only
**one** decal pass for trail/road (not two like shore overlays).

### 3. `terr_grid` purpose

Still unconfirmed.  Not part of the network-decal path.  Defer.

### 4. `terr_cana_mon1` — extra culture for canals

Still unconfirmed.  Likely maps to a Mongol/steppe episode palette.  Defer.

---

## Phases

### Phase 1 — Extractor: register trail/canal/rail atlases  *(low risk)*

**Goal**: make `terrain.trai_chi1` etc. available in `manifest.json` so the
renderer can load them.

**Files**: `OpenTE/tools/extractor/sprites/terrain.py`,
`OpenTE/tools/run_extractor.py`

**Work**:

1. In `terrain.py`'s `_CULTURES` dict, extend each culture entry with trail,
   canal, and rail atlas tags, following the same pattern as `tran`:
   ```python
   "chi1": {
       "pages": { ... },
       "tran": "trch",
       "trai1": "trai/chi1",   # primary trail atlas
       "trai2": "trai/chi2",   # secondary (or road)
       "cana":  "cana/chi1",
       "rail":  "rail/chi1",
   }
   ```
   Sprite IDs: `terrain.trai_chi1`, `terrain.trai_chi2`, `terrain.cana_chi1`,
   `terrain.rail_chi1` (consistent with existing `terrain.coa0` naming).

2. Add extraction calls for each atlas, following `_extract_shore_atlas()`.
   Each is a straightforward `bg6a` leaf → RGBA PNG, no special processing.

3. Register `terrain.grid` while here (resolves Open question 3 cheaply).

4. Re-run extractor to confirm manifested IDs and PNG sizes.

**Done when**: `game_data/sprites/terrain.trai_chi1.png` etc. exist and are
256×256 RGBA PNGs.

---

### Phase 2 — Data model: TileConnectivity in Region  *(low risk)*

**Goal**: `Region` exposes per-tile connectivity bitmasks so the renderer can
look up which directions each network connects.

**Files**: `OpenTE/game/src/world/region.h`, `OpenTE/game/src/world/region.cpp`,
`OpenTE/tools/extractor/maps/region.py`

**Spec reference**: `OpenTE/spec/world-and-maps.md` §"Connectivity" — the
struct and 8-direction encoding are already designed.

**Work**:

1. Add to `Region`:
   ```cpp
   struct TileConnectivity {
       uint8_t trail_extra = 0;  // trail-only dirs (bit d = direction d connects)
       uint8_t road        = 0;  // road dirs (trail can also use these)
       uint8_t rail        = 0;
       uint8_t canal       = 0;
       // deep_water = 0xFF always; not stored.
   };
   // Indexed [ty * width_ + tx], same stride as heightmap_.
   std::vector<TileConnectivity> connectivity_;
   ```

2. Add accessor:
   ```cpp
   const TileConnectivity& connectivity_at(int tx, int ty) const;
   ```

3. In `Region::load()`, after loading the heightmap, parse `mapp.path` records
   from the region JSON.  Each record has `{x, y, mask_bytes[6]}` (Round 16
   layout: `b0`=trail_extra, `b1`=road, `b2`=rail, `b3`=canal, `b4`=deep/0xFF,
   `b5`=unused).  Write into `connectivity_[ty*width+tx]`.

4. In the extractor (`maps/region.py`), emit the `path` records as a JSON array
   alongside the existing `elem`/`alti`/`terr` arrays.

5. Add a `mark_dirty(int tx, int ty)` mechanism (just `dirty_ = true` on a
   bool for now — terrain mesh is immutable but connectivity can change after
   construction).

**Done when**: `region.connectivity_at(tx, ty)` returns correct bitmasks for
tiles that have pre-built roads in `Maps/*.{}`, verifiable by printing a few
known-connected tiles from `mapp.path`.

---

### Phase 3 — RE: find trail/road LUTs in the EXE  ✅ DONE

**Findings document**: `documentation/extracted/exe_trail_re_findings.md`  
**Scripts**: `documentation/scripts/exe_trail_re1.py` – `exe_trail_re6.py`

**Key finding: the trail/canal/rail pipeline is structurally different from
the shore pipeline.**  There is **no LUT85 / dl-to-shape secondary pass**.
Instead, each network uses a single 256-entry inverse lookup table built at
runtime from a static seed.

#### What was found

**Three runtime LUTs** (in `.data`, built during init from `.rdata` seeds):

| Network | Runtime LUT | Seed address | Seed entries |
|---------|-------------|--------------|--------------|
| Trail/Road | `0x646fa0` | `0x5fc594` | 81 bytes |
| Rail (hash) | `0x646da0` | `0x5fc5e8` | 33 bytes |
| Canal | `0x646ea0` | `0x5fc60c` | 41 bytes |

Each LUT is built as: `fill[256]=0xff; for i,b in enumerate(seed): lut[b]=i`.
At runtime, the accumulated connectivity code is used as the index; `0xff` means
"no decal".

**Accumulated connectivity code** (8 bits):
- Bits 0–3: N/E/S/W road connections (from road byte, bits 0x02/0x08/0x20/0x80)
- Bits 4–7: N/E/S/W trail connections (trail-extra byte moves road bit → trail bit)
- For canal: canal-dir byte OR-ed into bits 0–3 (alongside road bits)

For trail/road LUT: 81 valid codes (3^4, each direction = absent / road / trail).  
For canal LUT: 41 valid codes.  
For rail hash: 33 valid byte patterns (opaque 8-bit bitmask including diagonals).

**Atlas assignments**:
- Trail/road: page 0xf4 (trai1, cells 0–52) and 0xf5 (trai2, cells 53–80)
- Canal: page 0xf6 (cana1, cells 0–40; all < 53 so always one page)
- Rail: page 0xfa (rail1, cells 0–52+)

`kShoreUvIndex @ 0x5f8940` and `float_arr @ 0x5f88f0` are **shared** with
shore — the diamond UV geometry is identical.

The full LUT tables (decoded from seeds) are in the findings document.

---

### Phase 4 — Rendering: Stage D decal pass  *(low risk, follows shore template)*

**Goal**: draw trail/canal/rail segments on the terrain mesh.

**Files**: `OpenTE/game/src/render/terrain_renderer.h`,
`OpenTE/game/src/render/terrain_renderer.cpp`

**Background — pipeline differences from shore**:
- Shore: two passes (LUT0 overlay1 + LUT85/dl-to-shape overlay2)
- Trail/Canal/Rail: **one pass only** — no secondary corner-piece overlay
- Trail/Canal: indexed by an 8-bit accumulated code built from road/trail/canal
  bytes; NOT by a raw 8-bit neighbor bitmask
- UV machinery: identical to shore (`kShoreUvIndex`, float-array, diamond quad)

**Work**:

1. In `TerrainTileset`, add texture slots for trail/canal/rail atlases.
   Each culture needs `trai1`, `trai2`, `cana`, `rail` (on top of existing
   `coa0`/`coa1` shore slots).

2. Add LUT constants from `exe_trail_re_findings.md` to `terrain_renderer.cpp`.
   These are **runtime-init tables** in the original; for OpenTE they can be
   `constexpr` since the seeds are static.  Use the `exe_trail_re6.py` output
   which gives the fully-expanded LUT (256 entries, most `0xff`):

   ```cpp
   // Trail/Road: accumulated_code → cell_code (0-80) or 0xff
   constexpr uint8_t kTrailLUT[256] = {
       0xff, 48,  46,  5,   50,  3,   2,   9,
       44,   0,   4,   8,   1,   7,   6,   10,
       49,   /* ... see exe_trail_re6_output.txt */ };

   // Canal: accumulated_code → cell_code (0-40) or 0xff
   constexpr uint8_t kCanalLUT[256] = { ... };

   // Rail hash: rail_byte → cell (0-32) or 0xff
   constexpr uint8_t kRailLUT[256] = { ... };
   ```

3. Add `render_network_decal(int tx, int ty, const SDL_Vertex corners[4]) const`
   to `TerrainRenderer`.  Algorithm (from `exe_trail_re_findings.md` §2-7):

   ```cpp
   void TerrainRenderer::render_network_decal(
       int tx, int ty, const SDL_Vertex corners[4]) const {

       const auto& conn = region_->connectivity_at(tx, ty);

       // ── Rail (highest priority) ──────────────────────────────────
       if (conn.rail) {
           uint8_t cell_code = kRailLUT[conn.rail];
           if (cell_code != 0xff)
               render_overlay_cell(cell_code, tileset_->rail, corners);
           return;
       }

       // ── Build 8-bit accumulated connectivity code ────────────────
       // Phase 1: road → bits 0-3 (N=0,E=1,S=2,W=3 from raw bits 1,3,5,7)
       uint8_t code = 0;
       auto extract4 = [](uint8_t raw) -> uint8_t {
           return ((raw>>1)&1) | ((raw>>2)&2) | ((raw>>3)&4) | ((raw>>4)&8);
       };
       code = extract4(conn.road);
       // Phase 2: trail_extra → moves road bits 0-3 → trail bits 4-7
       uint8_t te4 = extract4(conn.trail_extra);
       code &= ~te4;             // clear road bits for trail directions
       code |= (te4 << 4);       // set trail bits
       // Phase 3: canal_dir → OR into bits 0-3 (if canal present)
       bool has_canal = (conn.canal != 0);
       if (has_canal)
           code |= extract4(conn.canal);

       if (!code) return;  // no connections at all

       // ── Canal (if canal present) ─────────────────────────────────
       if (has_canal) {
           uint8_t cell_code = kCanalLUT[code];
           if (cell_code != 0xff) {
               // canal always fits in one page (code < 53)
               render_overlay_cell(cell_code, tileset_->canal, corners);
           }
           return;
       }

       // ── Trail / Road ─────────────────────────────────────────────
       uint8_t cell_code = kTrailLUT[code];
       if (cell_code == 0xff) return;
       SDL_Texture* atlas = (cell_code < 53) ? tileset_->trail1 : tileset_->trail2;
       uint8_t cell = cell_code % 53;
       render_overlay_cell(cell, atlas, corners);
   }
   ```

   Notes:
   - `extract4()` mirrors the EXE's `test byte, 0x02/0x08/0x20/0x80 → bit 0/1/2/3`
     extraction (bits 1,3,5,7 of raw byte → packed as bits 0,1,2,3).
   - `conn.canal` stores the canal-direction byte; `conn.canal != 0` means canal
     present.  The original uses a separate "canal flag" byte at mask[4] which
     is `0xff` for no canal and non-`0xff` when canal is present — `conn.canal`
     should encode this same information.
   - `render_overlay_cell()` and UV lookup are identical to `render_shore_overlays()`.
   - There is **no LUT85 / corner pass** for any network type.

4. Call `render_network_decal()` from `render()` after the shore-overlay pass,
   inside the per-tile loop, before moving to the next tile.

5. Add toggle bool `network_decals_enabled = true` matching the existing
   `shore_overlays_enabled` pattern.

**Data**: the full LUT arrays (256 entries each) are in
`documentation/extracted/exe_trail_re6_output.txt` (both as "Trail/Road LUT"
and "Trail seed bytes by cell" sections).  Use the former for the `constexpr`
arrays directly.

**Validation**: load a region JSON whose `mapp.path` records include pre-built
paths; toggle `network_decals_enabled` on/off and confirm segments appear/hide.

---

### Phase 5 — Construction mode wiring  *(low risk)*

**Goal**: when the player confirms trail placement, update `Region` connectivity
so the newly placed segments appear on the next render.

**Files**: `OpenTE/game/src/gameplay/construction_mode.cpp`,
`OpenTE/game/src/gameplay/gameplay_scene.cpp`

**Work**:

1. `ConstructionMode::commit_trail()` (new method, called when player confirms):
   for each consecutive pair of committed `TrailMarker`s `(A, B)`, compute the
   direction `dir` from A to B (using the 8-direction table), then:
   ```cpp
   region->connectivity_at(A.tx, A.ty).trail_extra |= (1 << dir);
   region->connectivity_at(B.tx, B.ty).trail_extra |= (1 << opposite(dir));
   region->mark_dirty(A.tx, A.ty);
   region->mark_dirty(B.tx, B.ty);
   ```

2. In `GameplayScene`, after `commit_trail()`, the renderer reads connectivity
   directly from `Region` each frame — no separate invalidation needed as long
   as `Region::connectivity_at()` returns the updated data immediately.

3. Restrict trail placement to cardinal and diagonal directions where the
   connectivity bit layout is valid (all 8 are valid in the current model).

**Note**: the `mark_dirty()` mechanism from Phase 2 is available here but not
strictly needed until the terrain mesh itself becomes mutable.  Trail segments
don't change the height mesh, only the overlay pass — which reads connectivity
live each frame already.

---

### Phase 6 — Per-episode atlas selection  *(low risk)*

**Goal**: use the correct culture's trail/canal/rail atlas for the active episode.

**Files**: `OpenTE/game/src/render/terrain_tileset.h`, `GameplayScene`

**Work**:

Each episode maps to a culture (ind1, eur1, chi1, per1 — from
`data_catalog_terr_sets.txt`).  The existing `TerrainTileset` already selects
the correct terrain texture pages per culture; extend it to select
`terrain.trai_chi1` vs `terrain.trai_eur1` etc. using the same culture-key
lookup used for `terrain.chi1.page00`.

The two `trai` variants (primary/secondary) need to be tested after Phase 3
resolves whether they are two overlay passes (like `coa0`/`coa1`) or trail vs.
road.

---

## Phase ordering and risk summary

| Phase | Risk | Status | Blocking on |
|-------|------|--------|-------------|
| 1 — Extractor | Low | Not started | Nothing |
| 2 — Region data model | Low | Not started | Extractor (optional) |
| 3 — RE: find LUTs | ~~High~~ | **Done** — see `exe_trail_re_findings.md` | — |
| 4 — Render pass | Low | Not started | Phase 3 (done), LUT arrays ready |
| 5 — Construction wiring | Low | Not started | Phase 4 working |
| 6 — Per-episode atlas | Low | Not started | Phase 4 working |

**Recommended order**: 1 → 2 → 4 → 5 → 6.  Phase 3 is complete; Phase 4 is
now unblocked.  The LUT arrays can be copied directly from
`exe_trail_re6_output.txt` into the `constexpr` arrays in Phase 4.

---

## Deferred / out of scope

- **Bridge rendering** (`terr_brid_*`, `terr_rbrd_*`): bridges are sprites
  placed at specific waypoints, not tile overlays.  Separate feature — see
  `bridge-plan.md` (canal sea-mouth decals + bridge decal suppression are
  done; the sprite pass and construction auto-bridging remain).
- **Canal water animation**: canal cells in the game animate.  Deferred until
  the animation system exists.
- **Multi-network overlap on a single tile**: the current render call draws
  trail/canal/rail independently.  If two networks occupy the same tile (e.g.
  a road and a canal crossing), the draw order may need attention — deferred
  until we have real test cases.
- **`terr_grid` usage**: resolved as an open question; if it turns out to be
  a construction overlay it belongs in `AreaOverlayRenderer`, not here.
