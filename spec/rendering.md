# Rendering

This document specifies the clone's rendering pipeline: isometric
projection, sprite assets/atlasing, camera, draw ordering, and the
placement-overlay/highlight system. Built on **SDL2 + SDL2_image**
(`SDL_Renderer`, hardware-accelerated 2D) per the toolchain spike.

## Coordinate spaces

1. **Tile space** — `(tx, ty)` float tile coordinates, as defined in
   [world-and-maps.md](world-and-maps.md).
2. **World pixel space** — the isometric projection of tile space, in
   pixels, before camera transform.
3. **Screen space** — world pixel space minus camera offset, scaled by
   zoom.

### Isometric projection

Standard 2:1 "dimetric" tile projection (the same convention used by
Age of Empires/SimCity-era isometric games, and consistent with Trade
Empires' visual style):

```
screen_x = (tx - ty) * (TILE_W / 2)
screen_y = (tx + ty) * (TILE_H / 2)
```

with `TILE_W : TILE_H = 2 : 1` — **`TILE_W = 64`, `TILE_H = 32`, confirmed
from the original's projection constants**. The inverse (screen -> tile,
needed for tile picking, [input.md](input.md)):

```
tx = (screen_x / (TILE_W/2) + screen_y / (TILE_H/2)) / 2
ty = (screen_y / (TILE_H/2) - screen_x / (TILE_W/2)) / 2
```

`TILE_W`/`TILE_H` are config constants (`config.json`, see
[data-model.md](data-model.md)), not hardcoded — different sprite sets
extracted from different game versions could have different native tile
sizes.

### Camera

```cpp
struct Camera {
    glm::vec2 world_pixel_offset; // top-left of viewport, in world pixel space
    float zoom = 1.0f;
};
```

- Pan: arrow keys / edge-scroll / middle-mouse-drag adjust
  `world_pixel_offset` (see [input.md](input.md)).
- Zoom: mouse wheel adjusts `zoom`, clamped to a config-defined range (e.g.
  `[0.5, 2.0]`). `SDL_RenderSetScale` or per-draw scaling applies it.
- The camera is **not** part of `World`/`Region` — it's per-viewport UI
  state, owned by the render/input layer.

## Terrain rendering

The terrain is rendered as a **per-vertex height-displaced mesh**, matching
the original's `D3DTLVERTEX`-based tile rasterizer (confirmed by RE
analysis):

- **Vertex grid**: for a `width x height` tile region, there is a
  `(width+1) x (height+1)` grid of vertices, one per tile-grid corner. Each
  vertex is shared by up to 4 adjacent tiles, so adjacent tiles' edges always
  coincide exactly — no seams, no separate skirt/edge geometry.
- **Vertex height**: a vertex's height is the average of the
  `mapp.alti` heightmap bytes of its up-to-4 surrounding tiles (tiles off the
  map edge, and water tiles, contribute `Region::sea_level()` instead of
  their `alti` byte — this makes water render as a flat plane and the map
  edge slope smoothly down to sea level).
- **Height -> world pixels**: a vertex's raw `alti` byte (0-255) is converted
  to a vertical world-pixel offset via `render::kPixelsPerAltiUnit` (`~1.7676
  px/unit`, EXE-confirmed in `render/iso.h`) and **subtracted** from the
  vertex's projected screen Y, so higher ground displaces upward. This
  constant is defined in zoom-independent "world pixel space"; `Camera::
  world_to_screen`'s uniform `(world - offset) * zoom` then applies zoom
  correctly without any special-casing.
- **Per-tile fan**: each tile is drawn as a 4-triangle fan from a center
  vertex through the 4 shared corner vertices, matching the original's
  `D3DPT_TRIANGLEFAN(6 verts)` geometry. The center vertex (position, UV,
  color) is the average of the 4 corners. Drawn via `SDL_RenderGeometry`
  (requires SDL >= 2.0.18; available in this project's vcpkg-pinned SDL2
  2.32). The fan avoids the diagonal-seam texture artifact that a 2-triangle
  quad split produces on height-displaced non-planar tiles.
- **Per-vertex slope shading**: each vertex gets an `SDL_Color` tint computed
  from a central-difference normal (using its 4 neighboring vertices'
  heights) dotted with a fixed light direction, blended between an ambient
  floor and full brightness (see `kSlopeNormalZ`/`kLightDir`/`kAmbient` in
  `render/iso.h`). This tint is set on `SDL_Vertex::color` and multiplies the
  tile texture, approximating the original's per-vertex diffuse lighting.
  The original's exact light direction/ambient constants weren't recovered;
  the clone's values are reasonable stand-ins and can be tuned freely.
- **UV mapping**: the original's continuous `uv = (col/8, row/16)` scheme
  (B15 Round 34). Each texture page is a 256x256 seamlessly-tileable image;
  the UV formula tiles it across an 8x16-tile region (32x16 texels per tile,
  2x-magnified onto the 64x32 screen diamond). UV origin is computed per-tile
  as `fmod(tx,8)/8` / `fmod(ty,16)/16`, with each corner adding a fixed
  `1/8` or `1/16` step and the center vertex at the midpoint — this avoids
  SDL2's lack of a wrap-mode API. The texture sampled for a given tile is its
  **texture page** (see [world-and-maps.md](world-and-maps.md)'s
  `texture_index` field and `tables/terrain_textures.json`'s 13-entry `pages`
  array), looked up via `Region::texture_index_at(tx, ty)`.
- **Map-edge skirt pass**: the south and east edges of the map diamond
  (the two edges facing the camera) get vertical "skirt" quads that drop
  from each edge vertex's terrain height down to sea level, giving
  mountains at the map boundary a visible cross-section instead of
  floating in space.  Textured with the `terrain.edge` gradient (a
  256×256 vertical gradient, dark at top / earthy-brown at bottom,
  extracted from `terr/edge` in `m_ui,u.{}`).  Rendered before the main
  tile pass so the terrain surface naturally overdraws any back-facing
  skirt geometry.  Toggle: `terrain_skirts_enabled`.  Note: steep height
  differences between *interior* tiles simply produce steep quads in the
  displaced mesh — there is no separate cliff-face geometry for those.
- **Build-once caching**: the heightmap is immutable at runtime (no terrain
  editing in Stage 1), so the vertex height/color grids are computed once at
  load time (`App::build_terrain_mesh()`) and cached; only screen-space
  projection (which depends on camera pan/zoom) is recomputed per frame.

### Texture-edge blending and shore overlays

The original's D3D rasterizer additionally multi-pass-blends neighboring
tiles' textures at shared edges (an edge-feather pass, plus shore-mask
overlays and decals). The clone implements the non-deferred parts of this per
[`terrain-blending-plan.md`](../implementation/terrain-blending-plan.md)
Stages A-C and E, as additional `SDL_RenderGeometry` passes drawn on top of
each tile's base quad (same corner vertices/positions/colors, only `tex_coord`
and `color.a` differ per pass):

- **Edge blending (Stage B, implemented)**: the renderer draws up to 4
  additional quads per tile — one per N/E/S/W neighbor that's
  in the same water/land class but uses a *different* texture page — each
  sampling the **`tran`-family atlas** (`terrain.tran`, extracted from
  `terr/trch` for the `chi1`/`ep01_china` palette; other palettes use
  `tran`/`tr14`/`tr15`/`tr17`, not yet extracted/selected per-episode). Each
  pass reuses the base quad's geometry (same corner positions/heights/colors,
  only `tex_coord` differs) with UVs selecting one of the atlas's 16 cells by
  the *neighbor's* texture-page index (`col = nbr & 3, row = nbr >> 2`,
  64x64px cells, near-corner UV `(cell*64+0.5)/256` per the original's
  formula).
  Only **half** of each cell (32x32px, `kTranSampleUV`) is actually sampled
  per tile — a 2026-06-13 empirical adjustment to match the base pass's 2x
  atlas-px-to-screen-px magnification (sampling the full 64px cell made the
  dissolve dots look smaller/denser than the base texture's features; see
  `terrain-blending-plan.md` Stage B.3 "Update (scale)"). The
  dithered-dissolve pattern *is* the transition's "feather", baked into the
  atlas's pixels. The decoded `terr/trch` source is opaque (alpha==255
  everywhere) with near-black (RGB <= ~0x1F) background pixels and the
  dissolve "dots" as the only non-near-black pixels — there's no usable alpha
  gradient as-is. The extractor (`tools/extractor/sprites/terrain.py`'s
  `_apply_dissolve_mask`) recuts this into a **hard alpha mask** (dot pixels
  opaque, near-black background fully transparent, threshold
  `_TRAN_DISSOLVE_THRESHOLD = 8`), drawn with ordinary `SDL_BLENDMODE_BLEND`
  so only the dissolve dots are visible, the base terrain pass showing
  through everywhere else. `terr/edge` (a
  plain vertical gradient) is **unrelated** to this pass — it is the
  map-edge skirt texture (see "Map-edge skirt pass" above). Map-edge tiles
  (no neighbor) and cross-class (water/land) edges are skipped — the latter
  is handled by shore overlays. The per-direction rotation of the cell's
  (NW,NE,SE,SW) corners onto the tile's quad corners
  (`TerrainRenderer::render_edge_blends`'s `kEdgeBlendK = {2,3,0,1}` for
  N/E/S/W) is derived from the original's corner-rewrite arithmetic (raw
  `k = ((dir+1)&7)>>1` = `{1,2,3,0}`, then a +1 cyclic correction after
  visual checks) — tile corner `j`
  (NW=0,NE=1,SE=2,SW=3) gets cell corner `m=(j-k) mod 4` from
  `(u1,v1),(u0,v1),(u0,v0),(u1,v0)` for `m=0..3`.
  `terrain_blending_enabled` gates this pass (Stage B.5).
- **Shore overlays (Stage C)**: at water/land boundaries, up to two
  additional quads are drawn from the `terrain.coa0`/`terrain.coa1` atlases:
  an 8-direction water-neighbor mask selects an "overlay1" cell via a 256-entry
  lookup table, and a 4-corner water-flags value selects an "overlay2" cell
  (shoreline corner piece) via an 85-entry lookup table + shape map. Both
  tables and the 53-cell UV-index array are derived from the original's
  data. The square-cell-to-diamond-quad UV mapping (`kShoreCellSize` = 2/16
  of the atlas) is a documented approximation pending visual validation.
- **Master toggle (Stage E)**: `App::terrain_textures_enabled_` (mirroring
  the original's "Terrain textures" option) — when false, every tile is
  drawn as a flat slope-shaded quad with no texture and no blending/overlay
  passes. `terrain_blending_enabled_`/`shore_overlays_enabled_` independently
  gate the B/C passes. None of the three are exposed in UI yet (code-level
  only).
- **Network decals (Stage D)**: trail/road/rail/canal overlays, drawn after
  shore overlays for every tile whose `TileConnectivity` mask is non-zero.
  Uses four 256-entry LUTs (`kTrailLUT` / `kRailLUT` / `kCanalLUT` /
  `kCanalMouthLUT`, decoded byte-exact from the EXE in
  `terrain_renderer.cpp`) and the same 53-cell `kShoreUvIndex` +
  diamond-quad UV machinery as shore overlays. Priority: canal wins (a
  canal *endpoint* touching shore-water draws a sea-mouth cell 45-52, else
  a land-canal cell 0-32) → bridge-tile decal suppression (`bridge !=
  0xff`; the bridge visual is a separate sprite) → rail (41-LUT on the
  accumulated code; mixed codes = road-over-rail level crossings) →
  trail/road. Trail/road are split across two atlas pages
  (`terrain.<culture>.trail1` / `trail2`); canal uses one
  (`terrain.<culture>.canal` ← container `terr/cana/*`); rail uses one
  (`terrain.<culture>.rail` ← `terr/rail/*`). Canal is the only
  8-directional network — see [world-and-maps.md](world-and-maps.md) for
  the diagonal-bit encoding. The per-tile `TileConnectivity` struct
  (6 bytes: `trail`, `road`, `rail`, `canal`, `bridge`, `bridge_aux`)
  lives in `world::Region::connectivity_`, seeded at load from the map's
  authored `mapp.path`/`mapp.brid` records. **Implemented** in
  `terrain_renderer.{h,cpp}` (`render_network_decal` / `draw_network_conn`)
  and `terrain_tileset.{h,cpp}` (`network(layer)` accessor). Network atlas
  extraction is in `OpenTE/tools/extractor/sprites/terrain.py`
  (`_NETWORK_ATLAS_SUBTAGS`).
  See `documentation/extracted/exe_trail_re_findings.md` for full RE detail.

A detailed, staged implementation plan — including the per-tile
texture-page data model, palette/atlas extraction, and open
validation items for the shore-overlay UV mapping — lives in
[`OpenTE/implementation/terrain-blending-plan.md`](../implementation/terrain-blending-plan.md).

## Sprite assets

The extractor produces one RGBA8888 PNG per original sprite (see
[data-model.md](data-model.md)) — all original pixel-format complexity
(8bpp paletted via `ap01`, 16bpp RGB565/RGB555/ARGB4444, 32bpp RGBA8888) is
resolved **once, at extraction time**, so the game only ever deals with
plain RGBA PNGs. This is a deliberate simplification: the original's mix of
formats was a memory/bandwidth optimization for 2001 hardware that's
irrelevant on a modern target.

### Atlas packing (post-spike)

The spike loads sprites individually via `IMG_LoadTexture`. Once the game
needs many sprites (hundreds of building/unit/UI sprites), pack them into a
small number of texture atlases at **game startup** (not extraction time):

1. On first run (or when `game_data/` changes — compare a hash/mtime),
   build atlas pages by reading every PNG listed in `manifest.json.sprites`
   and packing with a simple shelf/skyline bin-packer (a single-file
   implementation is fine; no need for an external dependency at this
   scale).
2. Cache the packed atlas pages + a `sprite_id -> (page, rect)` index to a
   `cache/` directory next to `game_data/` (or under the platform's user
   cache dir), so subsequent launches skip repacking.
3. `SpriteAtlas::get(sprite_id) -> AtlasRegion{ texture*, SDL_Rect }` is the
   only API the renderer needs; callers never see individual textures.

This keeps `SDL_RenderCopy` calls batched per atlas page (fewer texture
binds = better performance) without complicating the extractor or the
`game_data/` format — atlasing is purely a render-layer cache.

### Animation playback

The original drives sprite-frame selection via a small per-entity bytecode
VM (`Data/anim.{}`). The clone should **not** reimplement this bytecode VM — it's an
implementation detail of the original engine, not a format worth
preserving. Instead, model each animated entity's visual state as a small,
explicit **state machine** defined in data:

```json
{
  "id": "char.walk",
  "states": {
    "walking": {
      "frames": ["bldg.eur1.cara.walk.0", "...walk.1", "...walk.2", "...walk.3"],
      "frame_duration_ms": 150,
      "loop": true
    },
    "idle": { "frames": ["...idle.0"], "frame_duration_ms": 0, "loop": false }
  },
  "transitions": [
    { "from": "idle", "to": "walking", "when": "moving" },
    { "from": "walking", "to": "idle", "when": "stopped" }
  ]
}
```

This captures everything the decoded original scripts actually *do*
(advance a frame index every N ticks, gate on "are we still
moving/at-waypoint", loop/wrap) in a format any contributor can read and
any modder can edit, without needing to understand a custom bytecode
language. `frame_duration_ms` replaces the original's tick-based
`f`/`D`/`F` scheduling — at the clone's fixed simulation tick rate these are
trivially interconvertible (`frame_duration_ms = ticks * (1000 / SIM_HZ)`),
but storing milliseconds keeps the data readable independent of whatever
tick rate the simulation ends up using.

Per-entity animation state (`current_state`, `current_frame`,
`elapsed_ms`) is small and lives on the entity (see
[entities.md](entities.md)), advanced during the simulation tick (frame
selection is gameplay-deterministic-ish, matching the original's
tick-driven model) and read by the renderer.

## Draw order (z-sorting)

Isometric rendering requires back-to-front painter's-algorithm ordering so
nearer objects occlude farther ones correctly. Sort key, in order:

1. **Layer**: `terrain < decorations < buildings/entities < highlight
   overlays < UI`. Each layer is drawn completely before the next.
2. Within the buildings/entities layer, sort by **`tx + ty`** (the
   isometric "depth" — tiles/entities with a larger `tx+ty` are drawn
   later/on top), then by **`ty`** as a tiebreaker (so two entities with
   the same `tx+ty` but different `ty` — i.e. on the same anti-diagonal —
   are ordered consistently), then by entity creation order/ID for full
   determinism (stable sort).
3. Multi-tile buildings sort by their **footprint's `(x0+y0)`** (top-left
   corner) — consistent with single-tile entities since a building's sprite
   is anchored at its footprint origin.

Recompute the sort only when the entity list or positions change
meaningfully (most entities are static buildings) — maintain a
mostly-sorted list and re-sort lazily, or bucket entities by tile row for
large maps, if profiling shows sorting cost matters. Don't add this
optimization preemptively; a `std::sort` over all visible entities each
frame is almost certainly fine for this game's scale and should be the
starting implementation.

## Tile highlight overlays (placement preview)

Used for building-placement ghosts and pathway drag-out previews (driven by
[input.md](input.md)'s placement-legality logic). Render as a
**semi-transparent colored quad per highlighted tile**, drawn in the
highlight-overlay layer (above buildings, below UI):

- **Valid placement tile**: light cyan/blue (the original used
  `#34BBFF`/`#009BFF`-ish tones for "available"/"hovered-and-valid" — the
  clone can pick its own palette, but a blue=valid / red=invalid convention
  matches genre expectations and is what the original's UI text implies
  even where the exact RE'd color for "invalid" wasn't found).
- **Invalid placement tile**: red/orange, semi-transparent.
- **Pathway drag preview**: render the actual trail/road/canal/rail atlas
  sprites (not colored quads) by calling `TerrainRenderer::render_preview_path(
  waypoints, cursor_tx, cursor_ty, path_type, camera)` after the main
  `render()` call. The method rasterizes waypoint segments internally (8-connected
  greedy diagonal) and builds a temporary `TileConnectivity` map in memory,
  then calls `draw_network_conn()` per tile — no Region mutation, no set/clear
  dance. Small semi-transparent colored overlays remain for waypoint markers
  (yellow dots showing clicked joints) and the live cursor tile (orange);
  these still go through the `AreaOverlayRenderer`.

Implementation: draw a flat-shaded isometric diamond (`TILE_W x TILE_H`)
at each highlighted tile's screen position with `SDL_SetRenderDrawBlendMode(
SDL_BLENDMODE_BLEND)` and the appropriate RGBA color — no need for a sprite
asset, a procedural quad is simpler and trivially recolorable. (This
applies to building placement highlights; pathway previews use real network
decal sprites as described above.)

## UI rendering

UI widgets (HUD, dialogs) are drawn after the world in screen space (not
affected by camera pan/zoom). See [ui.md](ui.md) for the widget framework;
this document only notes that the renderer must support an
"un-projected, screen-space" draw mode in addition to the
world/camera-projected mode used for tiles/entities.

## Open questions / RE gaps

- ~~**Native tile pixel dimensions** (`TILE_W`/`TILE_H`)~~ **Resolved** —
  confirmed `TILE_W = 64`, `TILE_H = 32` from the original's projection
  constants. Sprite extraction can still be used to double-check alignment,
  but these are no longer placeholders.
- **Exact "invalid placement" highlight color**: RE analysis found two
  "valid" blue tones but never located a red/invalid color in the original's
  placement-overlay logic — it may be communicated by *absence* of a
  highlight rather than a distinct color. The clone is free to choose its own
  (a red highlight is clearer UX regardless).
- ~~**Team-color tinting**~~ **Resolved (negatively)** — a full check of all
  24,065 leaves in `unit.{}` confirms all sprites decode as ARGB4444 with
  correct, full color (not grayscale masks); the earlier "grayscale mask"
  theory was a misread of ARGB4444's low byte. Sprites are extracted as
  full-color RGBA as-is. If per-player recoloring is wanted for visual
  clarity (e.g. recoloring banners/flags only), that's a clone design choice
  using `SDL_SetTextureColorMod`, not an RE-blocked item.
- ~~**3D terrain heightmap rendering**~~ **Resolved** — per-vertex height
  displacement (`~45.25*zoom` px/world-unit), 8-neighbor slope-based diffuse
  shading, and a map-edge skirt pass. See "Terrain rendering" above for
  the implemented clone model, including the per-tile texture-page selection
  and the edge-blend/shore-overlay passes (`terrain-blending-plan.md` Stages
  A-C/E). The original's `uv = (col/8, row/16)` continuous shared-atlas
  texture mapping (Stage A.0) was tried and reverted in favor of per-tile
  edge-midpoint UVs — see "Terrain rendering"'s UV mapping bullet. The exact
  light/ambient constants for slope shading (item 2 below) are still open,
  and the shore-overlay UV mapping is a documented approximation pending
  visual validation.
- **Exact slope-shading light direction/ambient constants**: RE confirmed
  *that* the original computes per-vertex normals and a diffuse term, but
  not the exact light direction or ambient floor values. `render/iso.h`'s
  `kLightDir`/`kAmbient`/`kSlopeNormalZ` are reasonable stand-ins; revisit if
  further analysis recovers the originals.
