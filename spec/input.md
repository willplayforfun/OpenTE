# Input

This document specifies input handling: event flow, camera controls, tile
picking, and the building-placement / pathway-drag-out interaction. The
placement-legality rules are **ported from the original**
(`documentation/03-exe-analysis.md` Rounds 12-18, the most thoroughly
decoded subsystem in the RE notes) since they directly define core gameplay
("can I build here?"); the input *handling code* itself (event loop,
state machine) is a clean SDL2-based design.

## Event flow

```cpp
void App::handle_event(const SDL_Event& e) {
    if (ui.handle_event(e)) return;       // ui.md widgets get first refusal
    switch (e.type) {
        case SDL_KEYDOWN:    input_state.on_key_down(e.key); break;
        case SDL_KEYUP:      input_state.on_key_up(e.key); break;
        case SDL_MOUSEMOTION: input_state.on_mouse_move(e.motion); break;
        case SDL_MOUSEBUTTONDOWN: input_state.on_mouse_down(e.button); break;
        case SDL_MOUSEBUTTONUP:   input_state.on_mouse_up(e.button); break;
        case SDL_MOUSEWHEEL: camera.on_wheel(e.wheel); break;
        case SDL_QUIT: running = false; break;
    }
}
```

`InputState` is a small state machine with modes:

```cpp
enum class InputMode { Default, PlacingBuilding, DraggingPathway, ... };
```

UI widgets (toolbars, dialogs) consume input first; world input (camera,
placement) only runs if no widget claimed the event. This is the standard
"UI on top" layering — see [ui.md](ui.md).

## Camera controls

- **Pan**: arrow keys (continuous, while held) and/or edge-scroll (cursor
  near viewport edge) and middle-mouse-drag. All adjust
  `Camera::world_pixel_offset` ([rendering.md](rendering.md)).
- **Zoom**: mouse wheel adjusts `Camera::zoom`, clamped per config.
- These reuse the spike's existing arrow-key movement code
  (`OpenTE/game/src/core/app.cpp`'s `kMoveStep` handling), generalized from
  "move the sprite" to "move the camera".

## Tile picking

Convert mouse position (screen space) to a tile coordinate using
[rendering.md](rendering.md)'s inverse isometric projection:

```cpp
glm::ivec2 pick_tile(glm::vec2 mouse_screen, const Camera& cam) {
    glm::vec2 world_px = mouse_screen / cam.zoom + cam.world_pixel_offset;
    float tx = (world_px.x / (TILE_W/2) + world_px.y / (TILE_H/2)) / 2.0f;
    float ty = (world_px.y / (TILE_H/2) - world_px.x / (TILE_W/2)) / 2.0f;
    return { (int)std::floor(tx), (int)std::floor(ty) };
}
```

Out-of-bounds results (`tx`/`ty` outside `[0, width)`/`[0, height)`) are
valid — callers (placement preview, etc.) should check bounds and treat
out-of-bounds as "no tile hovered".

This formula matches the original's `TMapView::OnMouseMove`
(`fcn.004689a0`, `documentation/03-exe-analysis.md` Round 19): the EXE
divides mouse-screen Y/X by the zoomed `TILE_H/2`/`TILE_W/2`, converts to
16.16 fixed-point, and floors each independently to get the diamond
row/col — i.e. exactly the `tx`/`ty` formula above. The EXE then maps the
diamond row/col through a precomputed per-viewport lookup table
(`fcn.0046d100`/`0046d1c0`) for a minor perf optimization that the clone
doesn't need.

## Building placement

### Flow

1. Player selects a building type from the build menu ([ui.md](ui.md)) ->
   `InputMode::PlacingBuilding`, with the selected `building_id`.
2. Each frame, `pick_tile()` gives the hovered tile; compute the
   building's footprint rect `{tx, ty, tx+w, ty+h}` (from
   `buildings.json[building_id].footprint`).
3. Run the **placement legality check** (below) against the current world
   state. Render the highlight overlay ([rendering.md](rendering.md)):
   blue/valid or red/invalid per tile in the footprint.
4. On left-click, if legal: issue a `PlaceBuildingCommand` (deduct cost,
   spawn the entity — see [entities.md](entities.md)). On right-click or
   Escape: cancel, return to `InputMode::Default`.

### Placement legality check (ported algorithm)

This reproduces `fcn.4ac8e0`'s decoded checks (`03-exe-analysis.md` Round
18), restated as a clean function returning a typed reason instead of a
magic int written to a global field:

```cpp
enum class PlacementError {
    Ok,
    NoRailConnectivity,        // original code 0x9
    CapacityExceeded,          // 0xc
    OverlapsBlockingEntity,     // 0xd
    TooManyBlockedNeighbors,    // 0xe / 0x10 / 0x11 (adjacency-count family)
    CanalNotAdjacentToWater,    // 0xf
    OverlapsExistingFootprint,  // 0x12
    SpecialAdjacencyFailed,     // 0x13 / 0x14
    TooCloseToSameType,         // 0x15  (minimum-spacing rule)
    InvalidTerrain,             // fcn.4640c0, originally a silent failure
};

PlacementError check_placement(const World& world, const Building& b,
                                glm::ivec2 origin, int player_id);
```

High-level algorithm (per footprint tile `(tx,ty)` in
`[origin.x, origin.x+w) x [origin.y, origin.y+h)`):

1. **Terrain check** (`InvalidTerrain`): tile must be valid terrain for this
   building (e.g. not deep water, unless `terrain.requires_water_adjacent`
   logic applies — see canal below). Original: `fcn.4640c0`, internals not
   decoded; clone defines its own simple rule from `buildings.json.terrain`
   and the tile's terrain type ([world-and-maps.md](world-and-maps.md)).
2. **Entity overlap** (`OverlapsBlockingEntity` / `OverlapsExistingFootprint`):
   the tile must not fall inside another entity's footprint, unless that
   entity explicitly allows co-location (original: `fcn.4a4250(entity, 1)` —
   the clone defines an explicit `allows_overlap` flag per entity kind,
   defaulting to false).
3. **Rail connectivity** (`NoRailConnectivity`, rail buildings only): at
   least one footprint tile must have a rail connectivity bit set
   ([world-and-maps.md](world-and-maps.md)'s `rail` mask).
4. **Canal water adjacency** (`CanalNotAdjacentToWater`, canal-type
   buildings only): at least one footprint tile must be adjacent to a
   navigable-water tile.
5. **Road/blocked-neighbor count** (`TooManyBlockedNeighbors`): the original
   counts blocked-vs-road-adjacent tiles around the footprint and rejects if
   more than one tile is "blocked" (exact original predicate not fully
   linearized — `[ebp-0x48] > 1`). The clone's interpretation: **a building
   must have at most one side adjacent to impassable terrain/other
   buildings** (i.e. it needs reasonable access). Tune via playtesting.
6. **Capacity check** (`CapacityExceeded`, recipe-producing buildings):
   compare the recipe's required input capacity against
   `this->+0x10.vtable[0]()` — the clone's equivalent is "does the building
   type have a defined recipe capacity that the requested
   recipe/commodity exceeds" (`buildings.json` should carry a
   `max_recipe_tier` or similar if this matters; if not modeled, this check
   is a no-op for the clone — see Open questions).
7. **Footprint-overlap with same-area entities** (`OverlapsExistingFootprint`):
   AABB-overlap test of the new footprint against existing entities' rects
   (original: `fcn.466f50`).
8. **Special-type adjacency** (`SpecialAdjacencyFailed`): for buildings of a
   "special" type (original compares against two unidentified type-tag
   globals — likely things like ports/temples that must be adjacent to a
   specific feature), check adjacency to the required nearby
   entity/feature. The clone should make this **data-driven**:
   `buildings.json[id].requires_adjacent_type` (optional field), rather than
   hardcoding two magic type tags.
9. **Minimum-spacing rule** (`TooCloseToSameType`): no footprint tile may be
   within Chebyshev distance 1 of an existing entity of the **same building
   type** (and, per the original, same "area"/region scope). This prevents
   e.g. two markets from being built adjacent to each other.

Return the **first** failing check's `PlacementError` (the original's
"return false with reason code" short-circuit behavior) — [ui.md](ui.md)
maps each `PlacementError` to a player-facing tooltip string.

### Footprint "shape" (exclusion radius)

The original's hover-time check (`fcn.0054de20`, Round 13) additionally
supports **non-rectangular "shapes"** for occupancy/exclusion-radius tests
(a `shape_id` selects a per-row horizontal-extent table — e.g. "no other
market within N tiles in a diamond pattern"). The clone should support this
as an **optional per-building `exclusion_shape`** field
(`buildings.json[id].exclusion_shape`, e.g. `{"radius": 3, "shape":
"diamond"}` or `"square"`), checked as part of step 9 above. Most buildings
have no `exclusion_shape` (radius 0 = footprint-only); only special cases
(markets, ports) need it. The original's exact shape tables
(`fcn.00466e40`'s 4 row-extent tables) were not extracted — the clone
defines its own simple shapes (square/diamond radius) rather than reverse a
binary table for marginal fidelity.

## Pathway drag-out

### Flow

1. Player selects a pathway type (Trail/Road/Rail/Canal) from the build
   menu -> `InputMode::DraggingPathway`, with the selected network type.
2. On left-mouse-down, record the start tile. While dragging, compute the
   tile-to-tile path the drag implies (typically: an L-shaped or straight
   line from start to current hover tile — original had a
   `Pathfinder::FindPath`-style entry point per drag gesture,
   `03-exe-analysis.md` Round 15's `fcn.0049e5a0`).
3. For each tile pair along the implied path, check the "can network N step
   from A to B" rule ([world-and-maps.md](world-and-maps.md)). Render
   per-tile highlights: valid segments blue, invalid segments red.
4. On mouse-up: if the **entire** dragged path is valid, issue a
   `BuildPathwayCommand` for each segment (deduct cost — see
   [simulation.md](simulation.md), default cost-per-tile from `config.json`,
   originally a flat 1000 "DefaultCostCalculator" for all network types,
   Round 15). If any segment is invalid, the whole drag is rejected (no
   partial builds) — matches the original's "PayForPathwayCommand only
   follows a fully-valid MakePathwayCommand" structure (Round 17).
5. On commit, update the connectivity grid for every affected tile (both
   endpoints of each segment, per [world-and-maps.md](world-and-maps.md)'s
   "Building/extending a pathway segment" algorithm) and mark affected tiles
   dirty for re-rendering (sprite variant depends on connections).

### Drag-path computation

For the spike, a simple **two-segment L-path** (horizontal-then-vertical or
vertical-then-horizontal, whichever has fewer invalid tiles) from start to
end tile is sufficient and matches typical city-builder UX. A full
pathfinding-based "snap to nearest valid route" drag (closer to what the
original's `RoadFinderIterator`-driven drag may have done) can be a later
refinement — track in [implementation/](../implementation/) if the simple
L-path feels wrong in practice.

## Sound feedback

Per [audio.md](audio.md), a "spend" sound cue plays on any successful
treasury debit (building purchase, pathway construction) — matches the
original's `'spnd'/'inte'/'soun'` cue (`03-exe-analysis.md` Round 17),
played only for the local human player's transactions.

## Open questions / RE gaps

- **Capacity check (`CapacityExceeded`)**: the original's
  `fcn.408a10(recipe_id) > this->+0x10.vtable[0]()` comparison wasn't fully
  resolved to concrete data fields. Likely a no-op for the clone unless a
  concrete "production tier capacity" mechanic is designed.
- **Blocked-neighbor adjacency rule** (`TooManyBlockedNeighbors`): the
  original's exact threshold/predicate (`[ebp-0x48] > 1` etc.) is
  approximated above as "at most one blocked side" — tune via playtesting.
- **Special-type adjacency** (`SpecialAdjacencyFailed`): which building
  types this applies to, and what "nearby entity" is required, wasn't
  identified beyond two unresolved type-tag constants. Data-driven
  `requires_adjacent_type` lets specific buildings opt in once identified
  (or designed fresh).
- **Exclusion-radius shape tables**: not extracted; clone defines its own
  (square/diamond radius), see "Footprint shape" above.
- **Drag-path algorithm**: simple L-path vs. pathfinding-snapped drag — see
  above, defer to [implementation/](../implementation/) playtesting.
