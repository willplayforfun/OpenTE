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
from the original EXE's `TMapView` projection constants** (`0x5fc738` =
64.0, `0x5fc73c` = 32.0; see `documentation/03-exe-analysis.md` Round 19).
The inverse (screen -> tile, needed for tile picking,
[input.md](input.md)):

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
VM (`Data/anim.{}`, see `documentation/03-exe-analysis.md` Rounds 9-11/14).
The clone should **not** reimplement this bytecode VM — it's an
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
- **Pathway drag preview**: same convention, one quad per tile along the
  computed/previewed path.

Implementation: draw a flat-shaded isometric diamond (`TILE_W x TILE_H`)
at each highlighted tile's screen position with `SDL_SetRenderDrawBlendMode(
SDL_BLENDMODE_BLEND)` and the appropriate RGBA color — no need for a sprite
asset, a procedural quad is simpler and trivially recolorable.

## UI rendering

UI widgets (HUD, dialogs) are drawn after the world in screen space (not
affected by camera pan/zoom). See [ui.md](ui.md) for the widget framework;
this document only notes that the renderer must support an
"un-projected, screen-space" draw mode in addition to the
world/camera-projected mode used for tiles/entities.

## Open questions / RE gaps

- ~~**Native tile pixel dimensions** (`TILE_W`/`TILE_H`)~~ **Resolved** —
  confirmed `TILE_W = 64`, `TILE_H = 32` from the EXE's `TMapView`
  projection constants (`documentation/03-exe-analysis.md` Round 19,
  Workstream B item 7e/B9). Sprite extraction can still be used to
  double-check alignment, but these are no longer placeholders.
- **Exact "invalid placement" highlight color**: the original RE notes
  (`03-exe-analysis.md` Round 14) found two "valid" blue tones but never
  located a red/invalid color in the disassembled placement-overlay code —
  it may be communicated by *absence* of a highlight rather than a distinct
  color. The clone is free to choose its own (a red highlight is clearer
  UX regardless).
- ~~**Team-color tinting**~~ **Resolved (negatively)** — Workstream D Round
  11's full 24,065-leaf check of `unit.{}` confirms all sprites decode as
  ARGB4444 with correct, full color (not grayscale masks); the earlier
  "grayscale mask" theory was a misread of ARGB4444's low byte (see
  `documentation/08-investigation-needed.md` T0.5). Sprites are extracted as
  full-color RGBA as-is. If per-player recoloring is wanted for visual
  clarity (e.g. recoloring banners/flags only), that's a clone design choice
  using `SDL_SetTextureColorMod`, not an RE-blocked item.
