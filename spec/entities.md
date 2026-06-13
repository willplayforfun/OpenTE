# Entities

This document specifies the clone's entity model: entity types, the update
loop, pathfinding, and merchant/market state. The original's entity system
is a deep C++ class hierarchy (`SilkRoadEntity` -> `Entity`, with
`Market`/`Character`/`Player`/`Signpost`/buildings as concrete types, each
with a per-tick `Update` virtual — `documentation/03-exe-analysis.md` Round
8). The clone reproduces the **set of entity kinds and their per-tick
behaviors** (since that's what defines gameplay) using a simpler,
data-oriented design suited to C++20 rather than a deep virtual hierarchy.

## Design: data-oriented, not deep inheritance

Rather than a `SilkRoadEntity` base class with virtual `Update`, use a
small **tagged-union / component-style** model:

```cpp
enum class EntityKind { Building, Transporter, Merchant, Signpost, Beacon, SpecialResource };

struct EntityId { uint32_t value; }; // opaque handle, never reused while alive

struct Entity {
    EntityId id;
    EntityKind kind;
    int owner_player;       // 0 = neutral/AI-environment
    glm::vec2 position;     // tile-space, see world-and-maps.md
    int rotation;           // 0-3
    std::string type_id;    // -> data-model.md (e.g. "bldg.eur1.mill", "tran.cara")
    std::string culture_set;

    AnimationState anim;    // see rendering.md

    // Kind-specific state, accessed via EntityManager's typed stores:
    // BuildingState, TransporterState/MerchantState, etc.
};
```

`EntityManager` owns a `std::vector<Entity>` plus **separate, kind-specific
state stores** (`std::unordered_map<EntityId, BuildingState>`, etc.) — this
avoids a giant variant/union in the hot `Entity` struct and lets each
subsystem (economy, pathfinding, AI) iterate only the entities it cares
about. This is a deliberate departure from the original's per-class virtual
dispatch: the original needed runtime polymorphism because `Update` was
called generically through 5 distinct implementations
(`03-exe-analysis.md` Round 8's table); the clone's fixed-tick simulation
can instead run **5 separate typed update passes** per tick:

```cpp
void Simulation::tick(int tick_number) {
    update_markets(tick_number);       // simulation.md economy
    update_production(tick_number);    // simulation.md production
    update_transporters(tick_number);  // movement along paths
    update_merchants(tick_number);     // trade-order AI, see opponent-ai.md
    update_players(tick_number);       // per-player AI strategy ticks
    process_events(tick_number);       // simulation.md episodes/events
}
```

This mirrors the original's "5 distinct Update functions across 23 classes"
finding (Market/Character/Player/Signpost/default) at the *behavioral*
level without needing virtual dispatch — adding a new entity kind means
adding a new typed store + update pass, not slotting into a class
hierarchy.

## Entity kinds

### Buildings

```cpp
struct BuildingState {
    std::string building_id;      // -> data-model.md buildings.json
    int treasury;                 // production buildings only ('cash')
    int production_cycle_ticks;   // countdown to next production cycle
    bool active;                  // false if recipe inputs unavailable
    std::vector<MarketGood> goods; // for market/depot buildings, see simulation.md
};
```

- **Production buildings** (`bres`/`bpro`/`bdem` per the original's `bldg`
  type taxonomy, carried into `buildings.json.category`): run the
  production-cycle logic from [simulation.md](simulation.md).
- **Markets/depots** (`mark`/`bdep`): own one `MarketGood` per tracked
  commodity (the original's `mark.inve` table — 11 commodities observed in
  one save) and serve as the economy's per-region price/stock anchors. A
  market additionally has `population` (drives consumption demand, see
  simulation.md) and a display name (city name, from
  [world-and-maps.md](world-and-maps.md)'s `cities`).
- **Footprint occupancy**: a building occupies the tile rect
  `{x, y, x+footprint.width, y+footprint.height}` (origin = `position`,
  rotation currently does not swap width/height — if the original supports
  rotated non-square footprints, that's an Open question).

### Transporters & Merchants

The original distinguishes the *vehicle type* (`tran.<id>` — Camel, Wagon,
Galley, ...) from the *merchant* riding it (`char` entities with a `spec`
referencing a `tran` id, a `name` from the episode's merchant roster, and an
`abil` ability index). The clone keeps this as one entity
(`EntityKind::Merchant`) with both fields, since they're always 1:1 at
runtime (a merchant entity *is* its vehicle):

```cpp
struct MerchantState {
    std::string transporter_id;   // -> transporters.json (speed/capacity class)
    std::string name;             // from episode merchant roster, or "" for generic
    int ability;                  // index into abilities.json, or -1 for none
    EntityId home_market;         // -> BuildingState (market), original 'gara'
    EntityId home_depot;          // -> BuildingState (depot), original 'depo'

    std::vector<TradeOrder> orders; // queue, original 'char.orde'
    std::vector<CommodityStack> cargo; // current inventory, original 'char.inve'/'invl'

    // Path-following state:
    std::vector<glm::ivec2> path;  // tile waypoints, computed by pathfinding
    size_t path_index = 0;
    glm::vec2 render_position;     // interpolated for smooth rendering between ticks
};

struct TradeOrder {
    enum class Kind { BuyAndDeliver, Sell } kind;
    std::string commodity_id;
    int quantity;
    EntityId target_market;
};

struct CommodityStack { std::string commodity_id; int quantity; };
```

`TradeOrder`/`cargo` are a clean-room design informed by the original's
`char.orde`/`invo`/`inve` sub-tables (`documentation/04-other-formats.md`,
"most promising sub-table for reconstructing AI trade-route logic") — the
original's exact field encoding is not needed, just the *concept* (a
merchant has a queue of buy/deliver orders and carries cargo toward
fulfilling them). See [opponent-ai.md](opponent-ai.md) for how orders are
generated/consumed.

### Signposts, Beacons, Special Resources

- **Signposts** (`Signpost` in the original) mark waypoints/decision points
  on the pathway network for AI route-finding — in the clone, these can be
  implicit (any tile junction in the connectivity graph) rather than
  separate entities, **unless** playtesting shows AI routing needs explicit
  named waypoints for performance/legibility. Start without a separate
  `Signpost` entity kind; revisit if needed.
- **Beacons** (city-name labels) are part of [world-and-maps.md](world-and-maps.md)'s
  `cities` and don't need a separate runtime entity beyond the market
  building they label.
- **Special resources** (`sipo`/`ghos` in the original — unique
  per-scenario production sites like a Lapis Lazuli deposit) are
  `EntityKind::SpecialResource`: a fixed-position entity that produces a
  specific commodity at a fixed rate, optionally gated on a researched
  technology (`tech` field, `0`/absent = always available).

## Pathfinding

Building on [world-and-maps.md](world-and-maps.md)'s connectivity grid:

### Algorithm

Standard **A\*** over the tile grid, one search per (network type, start,
goal) request:

- **Neighbors**: for tile `(tx,ty)`, the up-to-8 neighbors per the 8-direction
  table, filtered by the "can network N step from A to B" rule
  (world-and-maps.md).
- **Cost — OPEN RE GAP**
  ([`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  item 10): the original's actual A* `DefaultCostCalculator` (`03-exe-
  analysis.md` Round 15) used a flat per-tile cost of 1000 for every network
  type — i.e. for the *vehicle pathfinder itself*, network choice doesn't
  bias the route search; routing is uniform-cost. Separately,
  `epis.<ep>.path` (now decoded as `episodes.json.movement_costs`, see
  [data-model.md](data-model.md) and `documentation/08-investigation-needed.md`
  T0.3) gives real, per-pathway-type, per-underlying-network costs (e.g. a
  Trail-network transporter pays 2 to cross open ground but 0 if a road is
  already there; a Canal-network transporter pays 20-50 off-canal but 0
  on-canal). These are **two different systems**, and **`movement_costs`'s
  real consumer in the original was never identified** — it could be UI cost
  estimates, AI route scoring, or per-network speed rather than A* cost.

  The clone currently uses `movement_costs` as its A* edge-cost table (keyed
  by the transporter's `path_type` and the destination tile's network). **This
  is a stand-in pending identification of `movement_costs`'s real consumer,
  not a confirmed-deliberate departure from the original.** If the original
  really does route uniformly and only varies *speed* by network/transporter,
  then AI merchants and the player's pathfinder in the original take routes
  the clone's `movement_costs`-weighted A* would never choose (e.g. the
  original might happily route across open ground where the clone insists on
  detouring via an existing road) — a player-visible difference in trade-route
  behavior, not just an internal implementation detail. Until
  `movement_costs`'s consumer is found, do not treat the clone's
  richer-routing choice as settled; see Open questions and
  `documentation/00-roadmap.md`'s spec-fidelity workstream. Per-network
  *speed* (tiles/tick, from `transporters.json`'s `by_episode.<ep>.speed`,
  T0.1) remains a separate *transporter* property controlling how fast a
  given path is traversed regardless of how this gap resolves. Diagonal steps
  cost `sqrt(2) * base_cost` as usual.
- **Heuristic**: Chebyshev or octile distance to the goal (admissible for
  8-directional uniform-cost grids).
- **Multi-network paths**: a merchant may need to traverse Trail then Road
  then Canal segments in one trip. Run pathfinding over the **union** of all
  networks the transporter type can use (from `transporters.json`'s
  `depot_class`/future network-access field — see Open questions), with the
  cost model above naturally handling mixed-network paths since cost is
  uniform per tile regardless of network.

### Implementation notes

- Use a standard binary-heap open-set; for a 128x128 grid this is fast
  enough without spatial partitioning.
- Cache/reuse paths: if a merchant's destination market hasn't changed and
  the connectivity grid hasn't changed since the last computation, don't
  recompute. Invalidate cached paths for all merchants when a pathway
  segment is built/removed anywhere on the map.

  **OPEN RE GAP**
  ([`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  item 11): this "any connectivity change invalidates all paths" rule is a
  clone-side implementation detail with no original counterpart investigated
  — whether the original re-routes every merchant on the map when *any*
  player builds a pathway segment anywhere (producing a visible
  all-merchants-re-path "hitch"), or only merchants whose route passes near
  the change, is **not known**. This isn't framed as "acceptable for the
  spike, refine if profiling shows it matters" — it's an open question about
  whether the clone's behavior is even *correct* relative to the original's
  player-visible feel, independent of performance. If profiling or
  playtesting suggests a visible difference, narrowing to bounding-box
  invalidation is the candidate fix, but confirming the original's actual
  behavior (if recoverable) should come first. See
  `documentation/00-roadmap.md`'s spec-fidelity workstream.
- Path-following: each tick, move a merchant `speed * dt` (tile-space)
  toward `path[path_index]`; on arrival within epsilon, advance
  `path_index`. On reaching the final waypoint, the merchant's current
  `TradeOrder` is fulfilled (cargo transferred to/from the target market,
  see [opponent-ai.md](opponent-ai.md)).

## Open questions / RE gaps

- **Rotated non-square footprints**: whether a building's footprint
  `(width, height)` swaps under rotation isn't confirmed from RE. Default
  to "footprint dimensions are as-stored regardless of rotation" (i.e.
  rotation is purely visual) until shown otherwise.
- ~~**Per-transporter network access**~~ **Resolved** (T0.1): each
  transporter's `by_episode.<ep>.speed` map in `transporters.json` gives a
  speed per network type, with `0` meaning "cannot use this network" — e.g.
  Camel (`came`) has nonzero `none`/`road`/`trai`/`dese` but `0` for
  `cana`/`deep`/`rail`; Galley (`galy`) has nonzero `cana`/`deep` only. No
  `depot_class`-based heuristic is needed; use the per-episode speed map
  directly.
- **`char.abil` value 132** (out of the documented 0-12 ability range) —
  RE notes suggest this might be a bitmask of multiple granted abilities
  rather than a single index. The clone's `MerchantState.ability` as a
  single `int` may need to become a bitset/vector if multi-ability
  merchants turn out to matter.
