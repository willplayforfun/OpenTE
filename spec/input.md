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

This formula matches the original's tile-picking implementation
(EXE-confirmed, see `documentation/03-exe-analysis.md` Round 19) — the
clone's direct floor-based computation replaces the original's precomputed
lookup-table optimization, which isn't needed at this scale.

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

This reproduces the original's decoded placement-legality checks (fully
derived in `documentation/03-exe-analysis.md` Rounds 18 and 29), restated as
a clean function returning a typed reason instead of a magic int written to
a global field:

```cpp
enum class PlacementError {
    Ok,
    NoRailConnectivity,        // rail buildings need a rail connectivity bit
    CapacityExceeded,          // recipe input exceeds building capacity
    OverlapsBlockingEntity,    // tile occupied by a non-overlap-allowing entity
    TooManyBlockedNeighbors,   // too many impassable/occupied neighbor tiles
    CanalNotAdjacentToWater,   // canal buildings need adjacent navigable water
    OverlapsExistingFootprint, // AABB overlap with another entity's footprint
    SpecialAdjacencyFailed,    // building-category adjacency requirement not met
    TooCloseToSameType,        // minimum-spacing rule vs. same building type
    InvalidTerrain,            // tile's terrain/connectivity mask disallows this building
};

PlacementError check_placement(const World& world, const Building& b,
                                glm::ivec2 origin, int player_id);
```

High-level algorithm (per footprint tile `(tx,ty)` in
`[origin.x, origin.x+w) x [origin.y, origin.y+h)`), returning the **first**
failing check (matching the original's "return false with reason code"
short-circuit behavior):

1. **Terrain** (`InvalidTerrain`): the tile's connectivity mask must allow
   this building's terrain category, and pathway placements may only extend
   a network the tile already has a connectivity bit for (byte0=Trail,
   byte1=Road, byte2=Rail — see [world-and-maps.md](world-and-maps.md)'s
   connectivity mask). The clone derives this from `buildings.json.terrain`
   and the tile's terrain type.
2. **Entity overlap** (`OverlapsBlockingEntity` / `OverlapsExistingFootprint`):
   the tile must not fall inside another entity's footprint, unless that
   entity explicitly allows co-location. The clone gives each entity kind an
   `allows_overlap` flag, defaulting to `false`; Dwellings never allow
   overlap (confirmed in the original).
3. **Rail connectivity** (`NoRailConnectivity`, rail buildings only): at
   least one footprint tile must have a rail connectivity bit set
   ([world-and-maps.md](world-and-maps.md)'s `rail` mask).
4. **Canal water adjacency** (`CanalNotAdjacentToWater`, canal-type
   buildings only): at least one footprint tile must be adjacent to a
   navigable-water tile.
5. **Blocked-neighbor count** (`TooManyBlockedNeighbors`): Round 29
   (`documentation/03-exe-analysis.md`) linearized the original's reason code
   `0xe`: across all footprint tiles, count tiles whose "primary direction"
   neighbor has connectivity-mask byte0 (Trail) `& 0xf == 2`; reject if that
   count is **> 1**. The clone's "at most one footprint-edge tile adjacent to
   a category-2 trail/road tile" check is a direct port of this part. **Gap**:
   reason codes `0x10`/`0x11` are *additional* fallback rejections (a second
   "diagonal-blocked" tile counter plus per-tile "corner flags", each compared
   against small constants) that Round 29 could not fully isolate
   arithmetically — the original's full `TooManyBlockedNeighbors` predicate is
   confirmed to be **stricter** than the `0xe`-only check above, but the exact
   diagonal/corner thresholds are not known. See Open questions.
6. **Capacity check** (`CapacityExceeded`, recipe-producing buildings): Round
   29 resolved the numerator — `fcn.408a10(recipe_id)` =
   `data.bldg.<episode>.<recipe_id>.cost` (already extracted, known field).
   **Gap**: the denominator (`this->+0x10.vtable[0]()`, a virtual call on an
   unidentified runtime object — likely a per-building or per-player
   "capacity" accessor) was not resolved, so the comparison
   `cost > capacity` cannot be ported. Until the denominator's source is
   found, this check has **no original behavior to replicate** for the clone
   — see Open questions.
7. **Footprint overlap with other entities** (`OverlapsExistingFootprint`):
   AABB-overlap test of the new footprint against existing entities' rects.
8. **Special-type adjacency** (`SpecialAdjacencyFailed`): Round 29 resolved
   that `fcn.473ae0(building_id)` looks up `data.bldg.defa.<id>.type` and
   **every one of the 153 buildings** falls into one of four categories
   (`bpro`/`bdem`/`bdep`/`bres`) — this is a type-category branch that applies
   to *all* buildings, not a small ports/temples allowlist as earlier
   speculated. **Gap**: what each category-branch of `fcn.473ae0` actually
   *checks* (the adjacency rule itself — e.g. what tile/entity a `bpro`
   building must be adjacent to) was not decoded; two type-tag globals
   (`0x647734`/`0x644568`) that the dispatcher reads are zero-initialized
   `.data` cells set at runtime and would need dynamic analysis to pin down.
   The clone can populate `buildings.json[id].type` (`bpro`/`bdem`/`bdep`/
   `bres`) from already-decoded data now, but
   `requires_adjacent_type`'s per-category *rule* remains undesigned pending
   that RE work — see Open questions.
9. **Minimum-spacing rule** (`TooCloseToSameType`): no footprint tile may be
   within Chebyshev distance 1 of an existing entity of the **same building
   type** (and, per the original, same area/region scope). This prevents
   e.g. two markets from being built adjacent to each other.

[ui.md](ui.md) maps each `PlacementError` to a player-facing tooltip string.

### Footprint "shape" (exclusion radius)

The original's hover-time check additionally supports **non-rectangular
"shapes"** for occupancy/exclusion-radius tests (a `shape_id` selects a
per-row horizontal-extent table — e.g. "no other market within N tiles in a
diamond pattern"). Round 29 (`documentation/03-exe-analysis.md`) extracted
the 4 underlying `(min_dx, max_dx)`-per-row tables (`shape_id` 4/6/8/10,
`shape_id` 5/7/9/other = "no shape" = footprint-only), each following
`(-(r), r-1)` per row for an increasing per-row radius `r` (a diamond/circle,
larger `shape_id` = larger radius) — this part is concrete, real game data.

**Gap**: the **building -> `shape_id` mapping is not known** — Round 29
located and dumped the dispatcher (`fcn.00466e40`) but did not trace which
buildings' placement checks pass which `shape_id`. Until that mapping is
recovered, the clone has no original data to port for *which* buildings get a
non-footprint exclusion radius or how large. The clone's current
`buildings.json[id].exclusion_shape` (`{"radius": N, "shape": "diamond"}` or
`"square"`, checked as part of step 9 above) is a **placeholder
implementation detail** — not a deviation that's been accepted, but a stand-in
for data that hasn't been recovered yet. Once the mapping is found, seed
`exclusion_shape` directly from the extracted `(-(r), r-1)` row tables above
(which already match the clone's diamond-shape representation closely) rather
than hand-tuning radii. See Open questions.

## Pathway drag-out

### Flow

1. Player selects a pathway type (Trail/Road/Rail/Canal) from the build
   menu -> `InputMode::DraggingPathway`, with the selected network type.
2. On left-mouse-down, record the start tile. While dragging, compute the
   tile-to-tile path the drag implies (typically: an L-shaped or straight
   line from start to current hover tile — the original had a similar
   per-drag path-building entry point, see `documentation/03-exe-analysis.md`
   Round 15).
3. For each tile pair along the implied path, check the "can network N step
   from A to B" rule ([world-and-maps.md](world-and-maps.md)). Render
   per-tile highlights: valid segments blue, invalid segments red.
4. On mouse-up: if the **entire** dragged path is valid, issue a
   `BuildPathwayCommand` for each segment (deduct cost — see
   [simulation.md](simulation.md), default cost-per-tile from `config.json`;
   the original used a flat cost of 1000 for all network types, see
   `documentation/03-exe-analysis.md` Round 15). If any segment is invalid,
   the whole drag is rejected (no partial builds) — matches the original's
   "pay only follows a fully-valid build" structure (see
   `documentation/03-exe-analysis.md` Round 17).
5. On commit, update the connectivity grid for every affected tile (both
   endpoints of each segment, per [world-and-maps.md](world-and-maps.md)'s
   "Building/extending a pathway segment" algorithm) and mark affected tiles
   dirty for re-rendering (sprite variant depends on connections).

### Drag-path computation

Round 29 (`documentation/03-exe-analysis.md`) confirmed `fcn.0049e5a0`
converts both drag endpoints to tile coordinates and builds/extends a
**segment list** from the raw endpoints via a helper chain
(`fcn.416750`/`fcn.4168c0`/`fcn.4a1880`) — i.e. the original is **not** running
a general per-drag A* search, so a full pathfinding-based "snap to nearest
valid route" drag is confirmed unnecessary. This much is a port-validated
fact, not an approximation.

**Gap**: the *exact segment shape* — whether the original ever produces more
than one segment, and if so the precise tie-breaking rule for orientation
(horizontal-first vs. vertical-first, how it picks between the two possible L
orientations, edge-case behavior at map boundaries) — depends on
`fcn.416750`/`fcn.4168c0`/`fcn.4a1880`/`fcn.46d180`'s internals, which weren't
decoded further. The clone's **two-segment L-path (horizontal-then-vertical or
vertical-then-horizontal, whichever has fewer invalid tiles)** is therefore a
**stand-in for an unconfirmed tie-break rule**, not a deliberate UX choice
known to match the original — the original could just as well always be
"horizontal-first" or use some other deterministic rule. Until
`fcn.416750`/`fcn.4a1880` are decoded, treat the L-path's tie-break as
unverified; see Open questions.

## Sound feedback

Per [audio.md](audio.md), a "spend" sound cue plays on any successful
treasury debit (building purchase, pathway construction), played only for
the local human player's transactions (matches the original, see
`documentation/03-exe-analysis.md` Round 17).

## Open questions / RE gaps

Most placement-legality internals are resolved at the RE level — see
`documentation/08-investigation-needed.md` (item B14 and related) for the
full derivation. The items below are the **specific remaining RE gaps**
(not design decisions) that block a full port of `check_placement`; each is
tracked in
[`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
and `documentation/00-roadmap.md`'s spec-fidelity workstream:

- **OPEN — `CapacityExceeded` denominator** (spec-deviations item 6): the
  numerator (`data.bldg.<episode>.<recipe_id>.cost`) is known, but the
  denominator is a virtual call on an unidentified runtime object. Without
  it, there is no original comparison to port — confirm via dynamic analysis
  whether this check ever actually fires in practice before deciding the
  clone's behavior (no-op vs. designing a "capacity" mechanic around `cost`).
- **OPEN — `TooManyBlockedNeighbors` reasons `0x10`/`0x11`**
  (spec-deviations item 1): reason `0xe` is fully ported (step 5 above), but
  the original's full predicate also folds in a second "diagonal-blocked"
  counter and per-tile corner flags whose exact thresholds weren't isolated —
  the original is confirmed **stricter** than the `0xe`-only port.
- **OPEN — `SpecialAdjacencyFailed` per-category rules**
  (spec-deviations item 7): `defa.type` ∈ {`bpro`,`bdem`,`bdep`,`bres`} is
  known for all 153 buildings, but what each category's adjacency check
  actually *requires* (the dispatch branches' effects, plus two
  runtime-initialized type-tag globals) wasn't decoded — `requires_adjacent_type`
  has no rule to populate yet.
- **OPEN — exclusion-radius `shape_id` mapping** (spec-deviations item 2):
  the 4 shape tables' row data is extracted, but which buildings use which
  `shape_id` (or none) wasn't traced — the clone has no original data for
  *which* buildings get a non-footprint exclusion radius.
- **OPEN — drag-path segment tie-break rule** (spec-deviations item 3): the
  original builds a segment list (not A*) from raw drag endpoints, confirming
  no general pathfinding is needed, but the precise orientation/tie-break
  rule inside `fcn.416750`/`fcn.4a1880`/`fcn.46d180` wasn't decoded — the
  clone's two-segment L-path heuristic is unverified against it.
