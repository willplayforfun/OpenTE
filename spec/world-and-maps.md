# World & Maps

This document specifies the clone's map/world representation: the on-disk
map format under `game_data/maps/`, the in-memory world model, coordinate
systems, terrain, and the pathway/connectivity network used by both
pathfinding ([entities.md](entities.md)) and building placement
([input.md](input.md)).

## Coordinate system

- The world is a **128x128 tile grid** per region (matches every original
  map observed). The clone keeps this
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
  "heightmap": {
    "encoding": "raw-base64",
    "data": "..."
  },
  "texture_index": {
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
- **`heightmap.data`** is the original's `mapp.alti` byte grid, carried
  through verbatim: a 1-byte-per-tile, row-major grid (`encoding:
  "raw-base64"` = the raw bytes, base64-encoded, no RLE). `alti` is a
  genuine per-tile heightfield (not rendering noise), confirmed by RE
  analysis. The byte<->world-height conversion is
  `height_world_units = alti_byte * 10.0 / 256.0` (range `0.0..9.96`);
  `alti_byte >= 13` (`>= 0.5` units) is the engine's "elevated tile" cutoff
  for walkability only — it has no renderer-geometry consumer (there is no
  skirt/edge-cliff
  rendering pass in the original or the clone). A per-map "sea level"/
  water-plane height should be derived from **that map's own `alti`
  minimum** — it varies widely per map, so don't derive it from `terr`-band
  membership.

  The `heightmap` field/schema and the `world::Region::height_at`/
  `sea_level` accessors (`OpenTE/game/src/world/region.h`/`.cpp`) are
  implemented and final. **The terrain renderer implements the per-vertex
  height-displaced mesh** described in
  [rendering.md](rendering.md#terrain-rendering): a `(width+1) x (height+1)`
  grid of shared corner vertices, averaged from surrounding tiles' `alti`
  bytes (water/off-map tiles substitute `sea_level()`), displaced and
  slope-shaded per `render::kPixelsPerAltiUnit`/`kLightDir`/`kAmbient` in
  `render/iso.h`. The renderer also implements the original's multi-pass
  texture-edge blending and shore overlays (`terrain-blending-plan.md`
  Stages A-C/E) — see
  [rendering.md](rendering.md#texture-edge-blending-and-shore-overlays).
- **`texture_index.data`** is a 1-byte-per-tile, row-major grid (same
  "base64-rle" encoding as `terrain.data`) of **texture-page indices**
  (`1-13`), the low nibble of the original's `mapp.terr` byte. It indexes
  `tables/terrain_textures.json`'s 13-entry `pages` array (each entry a
  sprite id for one 64x32 texture page, resolved from a `terr/sets/<N>`
  palette — see `terrain-blending-plan.md` Stage A). `Region::
  texture_index_at(tx, ty)` returns this value (clamped to `[1, 13]`; older
  maps without this field default to `1` everywhere). Texture-page `<= 2` is
  "water-class" (`seas`/`deep`), `> 2` is "land-class" — used by the
  edge-blend and shore-overlay passes to decide whether two adjacent tiles
  should blend directly or via a shore overlay.
- **`decorations`** are static, non-interactive scatter objects (trees,
  rocks). They have no gameplay effect and are rendered behind/below
  entities. `culture`/`decoration_id` select which sprite from the
  extracted decoration set to use — the exact `decoration_id -> sprite`
  mapping is part of `tables/decorations.json` (extracted from the
  per-culture decoration sprite sheets).
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
2. A **connectivity mask** — which movement networks (trail, road, rail,
   canal) connect out of this tile and in which directions, plus a bridge
   marker. This is **mutable at runtime**: building a road/rail/canal
   segment updates the mask for the affected tiles, exactly mirroring the
   original's incremental "connectivity hash map" update on path-segment
   commit.

### Representation

Rather than the original's sparse hash-map-of-6-byte-records (a 2001
memory-footprint optimization), the clone stores connectivity as a **dense
2D array of small structs**, one per tile — `width*height`, row-major,
indexed `[ty*width + tx]` (128x128 x 6 bytes = 96 KB per region, trivial).

The 6 bytes match the original's mask record exactly (this is the
implemented `world::TileConnectivity`; byte semantics per
`documentation/extracted/exe_trail_re_findings.md` §0 corrections):

```cpp
struct TileConnectivity {
    uint8_t trail  = 0;    // byte 0: trail connections
    uint8_t road   = 0;    // byte 1: road connections (per-direction upgrade over trail)
    uint8_t rail   = 0;    // byte 2: rail connections (cardinal-only, like trail/road)
    uint8_t canal  = 0;    // byte 3: canal connections (the only 8-directional network)
    uint8_t bridge = 0xff; // byte 4: 0xff = no bridge; anything else marks a bridge tile AND is
                           //         the crossing's averaged water DEPTH (= the deck's elevation,
                           //         treated as an alti byte). 0 is a valid depth.
    uint8_t bridge_aux = 0; // byte 5: the bridge's DIRECTION, `0x10 << cardinal_index`
                           //         (0x10=N / 0x20=E / 0x40=S / 0x80=W)
};
```

**Direction bit encoding**, clockwise from NW: `0x01=NW, 0x02=N, 0x04=NE,
0x08=E, 0x10=SE, 0x20=S, 0x40=SW, 0x80=W`. Trail, road, and rail are
cardinal-only networks (odd bits) — a "diagonal" route is a staircase of
cardinal segments (confirmed against authored `mapp.path` data, which never
contains a diagonal bit). Canal is 8-directional: a diagonal canal
connection is encoded as its diagonal bit PLUS both flanking cardinal bits
(NE connection = `0x04|0x02|0x08` = `0x0e`, SE = `0x38`, SW = `0xe0`,
NW = `0x83`) — decoded from the 33 valid canal-decal keys, see
`documentation/extracted/exe_trail_re_findings.md` §0 correction 12.

### "Can network N step from tile A to tile B?" rule

This is the rule the original's `RoadFinderIterator`/`TrailFinderIterator`
etc. implement, reproduced here for the clone:

- **Trail**: `(trail | road) & dir_bit` must be set on tile A (trails may
  use roads).
- **Road**: `road & dir_bit` must be set on tile A.
- **Rail**: `rail & dir_bit` must be set on tile A.
- **Canal**: `canal & dir_bit` must be set on tile A (and the
  building-placement gate additionally requires water adjacency for canal
  *buildings* — see [input.md](input.md)). CAUTION for a faithful port:
  the original's boat pathfinding (`CanalFinderIterator::step @ 0x4a08e0`)
  does NOT read the connectivity mask at all — it steps all 8 directions
  over TERRAIN bytes (water nibble == 2, or flag bit 0x40 = "canal dug
  here"; network writes also OR terrain bit 0x10). The mask's canal byte
  drives the *decal*; boat *movement* is terrain-driven. See
  `documentation/extracted/exe_trail_re_findings.md` §0 correction 13.
- **Deep water**: open-ocean movement is not stored in the connectivity
  mask; boats navigate by terrain type (deep-water tiles are freely
  navigable).

A pathfinder step from A to B (B = A + delta(dir)) is legal iff the rule
above holds **and** B is in bounds. (The original also checks a reciprocal
mask on B in some cases — not confirmed; the clone can start with the
single-direction check above and revisit if pathfinding looks one-way where
it shouldn't.)

### Building/extending a pathway segment

When a player commits a pathway segment (Trail/Road/Rail/Canal) between two
adjacent tiles in direction `dir`:

1. Set the corresponding network's bit for direction `dir` on the source
   tile, **and** the bit for the opposite direction on the destination tile
   — connectivity is stored per-tile-per-outgoing-direction, so both ends
   need updating for a bidirectional edge.
2. (Original behavior, optional for clone) clear any decoration on the
   affected tiles ("remove the tree now under the road").

The Stage-D decal renderer reads connectivity live each frame, so no
explicit dirty-marking is needed for the visuals.

### Initial population from map data

At map load the grid starts all-default (`{0,0,0,0,0xff,0}` — the original
hash map's default-on-miss record) and is seeded from the map's authored
records, in this order:

1. **`mapp.path`** entries `{x:int16, y:int16, flags:uint32}` *overwrite*
   the tile's mask: `trail = flags>>24`, `road = (flags>>16)&0xff`,
   `rail = (flags>>8)&0xff`, rest default.
2. **`mapp.brid`** entries (same shape) *overwrite bytes 4/5 only*,
   preserving path data: `bridge = flags&0xff`,
   `bridge_aux = (flags>>8)&0xff`.

A bridge tile (`bridge != 0xff`) suppresses the tile's network decal — the
bridge visual is a separate sprite pass, implemented in
`GameplayScene::render_bridges()` (deck variant = the suppression value;
deck elevation = byte 4's depth, so a span stays level). See
[rendering.md](rendering.md) "Bridge decks" and
`OpenTE/implementation/bridge-plan.md`.

In the extracted `maps/<id>.json` this grid is stored as the `connectivity`
field, RLE-compressed ("base64-rle6": `(6-byte tile, uint16 LE run length)`
pairs) — see `tools/extractor/maps/region.py`.

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

The original's save format (`game.regi.<region>.enti`/`furn`/`ghos`/`dist`)
is **not** the clone's save format — it's referenced here only as a source
of *what state needs to be captured* (entity types, market inventory,
merchant order queues, etc.), which directly shapes the entity schema in
[entities.md](entities.md).

## Open questions / RE gaps

- **OPEN — Terrain band -> clone terrain-type mapping** (RE gap,
  [`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  item 8): the original's `mapp.terr` byte values cluster into 4 bands
  (`2-8`, `33-40`, `64-70`, `96-102`), confirmed real groupings in the raw
  data, but **what each band actually represents** (which of the clone's
  `TerrainType` values, if any band maps to more than one) and the
  `m_ui,u.{}` `terr/ts*` texture-selection table's correspondence to those
  bands were never cross-referenced — e.g. whether the original has a
  literal terrain-id -> texture-name lookup table is unknown. The current
  4-band -> `{water, plains, hills, mountains}` mapping the extractor uses
  is a **guess pending that cross-reference**, not a confirmed-good "coarse"
  result with low-risk refinement later — a wrong band/type pairing could
  mean e.g. "desert" tiles are extracted as "plains". The map JSON schema is
  unaffected either way (only the extractor's mapping table changes), but
  this is tracked as a real open RE question, not a deferred polish item.
- ~~**Pre-existing trails from terrain data**~~ **Resolved** — the original
  pre-populates connectivity at map load from explicit per-tile
  `mapp.path`/`mapp.brid` arrays (map-editor-authored pre-built
  roads/trails/bridges with explicit connectivity masks), NOT from a generic
  terrain-type scan. Maps with no pre-built infrastructure have empty arrays.
  See "Initial population from map data" above.
- **Determinism for multiplayer/replay**: if lockstep multiplayer or
  deterministic replay is ever desired, float tile coordinates and
  non-deterministic floating-point summation order could cause desyncs.
  Out of scope for the spike; the original's 16.16 fixed-point choice was
  partly for this reason. Revisit in [implementation/](../implementation/)
  if multiplayer becomes a goal.
- **Decoration sprite mapping** (`culture` + `decoration_id` -> sprite) is
  not yet extracted.
