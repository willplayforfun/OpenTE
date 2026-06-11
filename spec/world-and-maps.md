# World & Maps

This document specifies the clone's map/world representation: the on-disk
map format under `game_data/maps/`, the in-memory world model, coordinate
systems, terrain, and the pathway/connectivity network used by both
pathfinding ([entities.md](entities.md)) and building placement
([input.md](input.md)).

## Coordinate system

- The world is a **128x128 tile grid** per region (matches every original
  map observed — `documentation/04-other-formats.md`). The clone keeps this
  size as the default but should not hardcode it where avoidable (store
  `width`/`height` in the map file).
- **Tile coordinates** are integers `(tx, ty)`, `0 <= tx < width`,
  `0 <= ty < height`.
- **World/entity positions** use **fixed-point or float tile coordinates**
  for sub-tile precision (e.g. a merchant partway between two tiles). The
  original used 16.16 fixed-point integers (`loca` fields); the clone should
  use **`double` or `float` tile coordinates** (`x = 12.5` means "halfway
  across tile 12") instead — fixed-point arithmetic was a 2001-era
  performance/determinism choice that modern float math doesn't need, and
  floats are far easier to work with in C++/SDL. If lockstep multiplayer
  determinism is ever required, revisit this (see Open questions).
- **Footprint rectangles** for buildings are `{x0, y0, x1, y1}` in tile
  coordinates, computed from a placement origin tile + the building's
  `footprint.width`/`.height` (see [data-model.md](data-model.md)).

## Map file format (`game_data/maps/<map-id>.json`)

```json
{
  "id": "ep01_china",
  "name": "China",
  "episode": "ep01",
  "width": 128,
  "height": 128,
  "terrain": {
    "encoding": "base64-rle",
    "data": "..."
  },
  "regions": [
    {
      "id": "chin",
      "culture_set": "chi1",
      "starting_player": 1,
      "headquarters": { "x": 64, "y": 70, "rotation": 0 }
    }
  ],
  "decorations": [
    { "type": "flop", "culture": "chin", "decoration_id": 12, "x": 30, "y": 41 }
  ],
  "cities": [
    { "name": "Anyang", "x": 60, "y": 65, "culture_set": "chi1" }
  ],
  "special_points": [
    { "id": "wafr", "kind": "spro", "x": 10, "y": 90, "culture": "per2" }
  ]
}
```

Notes:

- **`terrain.data`** is a 1-byte-per-tile, row-major grid of **terrain type
  IDs** (0-255), matching the original `mapp.terr` grid 1:1 in spirit. The
  clone defines its **own small enum** of terrain types (e.g. `deep_water`,
  `shallow_water`, `plains`, `hills`, `mountains`, `desert`, `forest`) rather
  than carrying forward the original's ~17-19 raw IDs verbatim — the
  extractor maps the original's terrain bands to this enum (see Open
  questions for the exact band mapping). This keeps the clone's terrain
  rendering/rules code working from a small, named, documented set instead
  of magic numbers.
- The original's `mapp.alti` byte grid is **not yet** carried into the
  clone's map format. An earlier RE pass treated it as rendering-only
  jitter, but a re-check (see `documentation/08-investigation-needed.md`
  B15) found it's a genuine per-tile heightfield, not noise: across all 74
  `Maps/*.{}` files, `alti` is consistently 1.5x-12x smoother
  (tile-to-tile) than the categorical `mapp.terr` grid, and the EXE's
  `SilkRoadMap` loader (`method.SilkRoadMap.virtual_4` @ `0x4615a0`) loads
  `alti` into a per-tile array structurally identical to (and loaded
  alongside) `terr` — confirming it's live data, not vestigial. The initial
  ep01-China-only finding that "water tiles cluster near `alti~=7-8` (sea
  level) and the highest terrain band averages `alti~=112`" does **not**
  generalize as a universal constant — per-map elevation ranges vary widely
  (e.g. `ep06 Persepolis`'s lowest band averages `alti~=132`), and 15/74
  maps have their lowest-`terr`-band tiles at a *higher* mean `alti` than
  the rest of the map. Any "sea level" / water-plane height should be
  derived **per-map** (e.g. from `alti`'s own minimum), not from a
  `terr`-band lookup. B15 is the open investigation to find the
  height->screen-pixel scale factor and add a `heightmap` field (mirroring
  `terrain`'s `{encoding, data}` shape) plus the per-tile vertical offset and
  height-difference "skirt" rendering it implies.
- **`decorations`** are static, non-interactive scatter objects (trees,
  rocks). They have no gameplay effect and are rendered behind/below
  entities. `culture`/`decoration_id` select which sprite from the
  extracted decoration set to use — the exact `decoration_id -> sprite`
  mapping is part of `tables/decorations.json` (extracted from the
  per-culture decoration sprite sheets, see `04-other-formats.md`'s
  `bldg.{}`/`unit.{}` notes).
- **`regions`** describe per-player starting areas within a map (some
  episodes are multi-region). `headquarters` is the starting depot/HQ
  placement.
- **`cities`**/**`special_points`** seed the initial set of named markets
  and special-resource sites; gameplay then spawns/grows entities on top of
  these (see [entities.md](entities.md)).
- This format is the **scenario/initial-state** format. **Save games** are a
  separate format layered on top — see "Save format" below.

## Terrain & the pathway connectivity grid

Every tile has:

1. A **terrain type** (from `terrain.data`, immutable for a given map).
2. A **connectivity mask** — which of the 6 movement networks (`none`,
   `trail`, `road`, `rail`, `canal`, `deep_water`) can be entered from this
   tile, and in which of the 8 directions. This is **mutable at runtime**:
   building a road/rail/canal segment updates the mask for the affected
   tiles (and their neighbors), exactly mirroring the original's incremental
   "connectivity hash map" update on path-segment commit
   (`documentation/03-exe-analysis.md` Round 17/18).

### Modern representation

Rather than the original's sparse hash-map-of-6-byte-records (a 2001
memory-footprint optimization), the clone stores connectivity as a **dense
2D array of small structs**, one per tile:

```cpp
struct TileConnectivity {
    // bit i set => network can use direction i (8 directions, see below)
    uint8_t trail_extra = 0;  // original "b0": trail-only extra connections
    uint8_t road = 0;         // original "b1": also usable by trail (trail = road | trail_extra)
    uint8_t rail = 0;         // original "b2"
    uint8_t canal = 0;
    uint8_t deep_water = 0xFF; // open ocean: all directions, by default
    uint8_t reserved = 0;
};
```

128x128 tiles x 6 bytes = 96 KB per region — trivial for a modern target;
no need for the original's hash map at all. `width*height` array, row-major,
indexed `[ty*width + tx]`.

**8-direction order** (clockwise from NW, matches the decoded original for
easy cross-checking against RE notes):
`0=NW(-1,-1) 1=N(0,-1) 2=NE(1,-1) 3=E(1,0) 4=SE(1,1) 5=S(0,1) 6=SW(-1,1) 7=W(-1,0)`.

### "Can network N step from tile A to tile B?" rule

This is the rule the original's `RoadFinderIterator`/`TrailFinderIterator`
etc. implement (Round 15/16), reproduced here for the clone:

- **Trail**: `(trail_extra | road) & (1 << dir)` must be set on tile A.
- **Road**: `road & (1 << dir)` must be set on tile A.
- **Rail**: `rail & (1 << dir)` must be set on tile A.
- **Canal**: `canal & (1 << dir)` must be set on tile A (and the
  building-placement gate additionally requires water adjacency for canal
  *buildings* — see [input.md](input.md)).
- **Deep water**: `deep_water & (1 << dir)` set on tile A — defaults to "all
  directions" for open ocean tiles, `0` for land tiles not explicitly marked
  navigable.

A pathfinder step from A to B (B = A + delta(dir)) is legal iff the rule
above holds **and** B is in bounds. (The original also checks a reciprocal
mask on B in some cases — not confirmed; the clone can start with the
single-direction check above and revisit if pathfinding looks one-way where
it shouldn't.)

### Building/extending a pathway segment

When a player commits a pathway segment (Trail/Road/Rail/Canal) between two
adjacent tiles in direction `dir`:

1. Set the corresponding bit (`road`, `rail`, `canal`, or `trail_extra`) for
   direction `dir` on the source tile, **and** the bit for the opposite
   direction (`(dir+4) % 8`) on the destination tile — connectivity is
   stored per-tile-per-outgoing-direction, so both ends need updating for a
   bidirectional edge.
2. Mark both tiles' connectivity-derived rendering (which sprite variant a
   road tile uses depends on which neighbors it connects to) dirty for
   re-evaluation next render/update.
3. (Original behavior, optional for clone) clear any decoration on the
   affected tiles ("remove the tree now under the road").

### Initial population from terrain

At map load, the clone should derive an initial connectivity grid from
`terrain.data`:

- `deep_water` tiles: `deep_water = 0xFF` (all directions navigable).
- Land tiles adjacent to water: leave `canal`/`deep_water` at `0` until a
  canal is built (canals are player-constructed).
- All other networks (`trail`/`road`/`rail`) start at `0` everywhere —
  buildable only where the player constructs them, **except** the
  `trail_extra` bit may be pre-set on certain "natural path" terrain types
  if the clone wants pre-existing trails on some maps (the original's
  precomputed hash map presumably encoded this from terrain at load time;
  exact source data not recovered — see Open questions).

## In-memory world model

```cpp
namespace opente::world {

struct Region {
    std::string id;
    int width, height;
    std::vector<TerrainType> terrain;          // width*height
    std::vector<TileConnectivity> connectivity; // width*height
    std::vector<Decoration> decorations;
};

class World {
public:
    Region& region(std::string_view id);
    // Entities live in entities.md's EntityManager, not here — World owns
    // only static/semi-static map state.
};

} // namespace opente::world
```

Entities (buildings, transporters, merchants — anything with per-tick
behavior) are **not** part of `World`/`Region`; they're owned by the entity
manager described in [entities.md](entities.md) and reference tile
positions into the `World` by coordinate, not by embedding themselves in the
grid. This separation keeps the static map data trivially shareable/
read-only across systems (rendering, pathfinding) while entity state is
mutated by the simulation.

## Save format

A save captures: the `World` (terrain is immutable so only `connectivity`
+ `decorations` deltas are needed, though storing the full grid is simpler
and 96KB is negligible), all live entities (with full state per
[entities.md](entities.md)), per-player state (treasury, researched tech,
AI state), and the simulation clock (current tick, episode, etc.).

Recommended format: **the same JSON schema family as `game_data/`**, written
to a separate `saves/` directory (outside `game_data/`, since saves are
user data, not extracted/moddable game data). One JSON file per save,
pretty-printed is fine (saves are infrequent, not perf-critical) — this
makes saves human-readable/diffable/hand-editable, a nice property for
debugging and for players who want to hand-tweak a save.

```json
{
  "format_version": 1,
  "tick": 184320,
  "episode": "ep01",
  "players": [ { "id": 1, "treasury": 48200, "researched": ["ep01.pric"] } ],
  "regions": {
    "chin": {
      "connectivity": "...",
      "decorations": [ ... ],
      "entities": [ /* see entities.md */ ]
    }
  }
}
```

`format_version` follows the same "bump on breaking change, refuse to load
on mismatch" policy as `game_data/manifest.json` (see
[data-model.md](data-model.md)) — saves are **not** forward/backward
compatible across format versions in the spike; add a migration system only
once there's a real save-compatibility need (per CLAUDE.md guidance against
speculative compatibility shims).

The original's save format (`game.regi.<region>.enti`/`furn`/`ghos`/`dist`,
documented in `documentation/04-other-formats.md`) is **not** the clone's
save format — it's referenced here only as a source of *what state needs to
be captured* (entity types, market inventory, merchant order queues, etc.),
which directly shapes the entity schema in [entities.md](entities.md).

## Open questions / RE gaps

- **Terrain band -> clone terrain-type mapping**: the original's `mapp.terr`
  byte values cluster into bands (`2-8`, `33-40`, `64-70`, `96-102`) whose
  exact meaning (and the texture-selection table in `m_ui,u.{}`) wasn't
  cross-referenced. The extractor will need a reasonable
  band-to-terrain-type mapping; until precise, a coarse mapping (band index
  -> {water, plains, hills, mountains}) is sufficient to get a playable map,
  with finer terrain types added later without changing the map JSON schema
  (just refining the extractor's mapping table).
- **Pre-existing trails from terrain data**: whether the original
  pre-populates any `trail_extra` connectivity from terrain at map load, or
  whether all pathway connectivity starts empty and is purely
  player-constructed, is not confirmed. Default to "empty, fully
  player-constructed" for the clone unless a map clearly needs pre-existing
  trails to be playable.
- **Determinism for multiplayer/replay**: if lockstep multiplayer or
  deterministic replay is ever desired, float tile coordinates and
  non-deterministic floating-point summation order could cause desyncs.
  Out of scope for the spike; the original's 16.16 fixed-point choice was
  partly for this reason. Revisit in [implementation/](../implementation/)
  if multiplayer becomes a goal.
- **Decoration sprite mapping** (`culture` + `decoration_id` -> sprite) is
  not yet extracted — see `04-other-formats.md`'s `bldg.{}`/`flor.{}` notes.
