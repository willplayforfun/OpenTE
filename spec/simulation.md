# Simulation

This document specifies the clone's per-tick simulation: the economy
(price/stock) model, production/consumption, technology progression, and
the episode/event system. The economy formula is **deliberately ported**
from the original (`documentation/03-exe-analysis.md` "Round 4-7") because
it directly defines player-visible game feel (price drift, demand growth/
decline alerts) that veteran players expect — everything else here
(production scheduling, event scripting) is a clean modern design informed
by, but not bound to, the original's implementation.

## Tick model

- The simulation advances in discrete **ticks** at a fixed rate `SIM_HZ`
  (see [overview.md](overview.md) for the accumulator-based main loop).
  **`SIM_HZ = 2`** (one tick per 500ms of real time, with the engine capping
  catch-up at 2 ticks/frame under slow framerates) is the original's
  confirmed value (`03-exe-analysis.md` Round 24, closes B1) — this is a
  direct port, not a tunable starting guess. `SIM_HZ` remains a config
  constant for modding purposes ([modding.md](modding.md)), but the clone's
  *default* should be `2`, not chosen empirically.
- Each tick, in order:
  1. **Market/economy update** (per active commodity per market) — see
     below.
  2. **Production/consumption update** (per production building).
  3. **Entity updates** (movement, animation state) — see
     [entities.md](entities.md).
  4. **Episode/event queue** processing — see below.
  5. **Periodic (coarse) update**, every `N` ticks (config), for
     lower-frequency bookkeeping (autosave, score recompute, AI strategic
     decisions — see [opponent-ai.md](opponent-ai.md)).

This mirrors the structure of `SilkRoadGame::Tick`
(`03-exe-analysis.md` Round 8) at the level of "what categories of work
happen every tick vs. periodically", without copying its specific dispatch
mechanism (vtable-based entity iteration) — the clone uses a plain
`EntityManager::update_all(tick)` (see [entities.md](entities.md)).

## Economy: price & stock simulation

### Per-market, per-commodity state

```cpp
struct MarketGood {
    std::string commodity_id;   // -> data-model.md commodities.json
    int current_price;
    int stock;                  // carried-over stock from last period
    int production_this_period; // accumulated supply
    int consumption_this_period;// accumulated demand
    int trend;                  // size of the list at original Good+0x00;
                                 // >0 selects the "decay toward BASE/2" branch
                                 // and counts down each tick it's nonzero.
                                 // What populates/sizes this list in the
                                 // original is NOT KNOWN — see Open questions.
    int demand_tier;            // long-term "growing"/"declining" counter (signed)
};
```

This is a direct, renamed port of the original "Good" record fields
(`+4`=price, `+8`=stock, `+0xc`=production, `+0x10`=consumption, `+0x00`=
the `trend`/branch-selector list, `+0xb0`=demand tier —
`03-exe-analysis.md` "Round 4-7"/"Round 8"/"Round 21"). The names are made
explicit since the originals were only inferable from usage. Round 4-7
guessed `trend` was a self-contained "active correction" countdown the price
formula itself sets; Round 21 found it's actually the size of a linked list
at `Good+0x00` whose producer/consumer elsewhere in the engine was not
traced — the *consequence* (a countdown that drives the decay branch) is
confirmed, but *what real-game condition makes `trend > 0`* is still
unresolved (see Open questions).

### Tunable constants (`config.json`)

Four master tuning knobs, ported verbatim from the original (where `PSCx`
controls normal per-tick price drift and `PSFx` controls multi-tick
trend-driven decay; the `A`/`T` suffix splits each pair by the sign of the
price delta). **Values confirmed from the original's `data.{}` root `game`
table** (`documentation/03-exe-analysis.md` Round 20 — these are real
`0x48`/float fields on the `game` record, read at runtime via the
`Scope`/config lookup chain documented in Round 4-7):

```json
{
  "economy": {
    "price_scale_above": 0.04,  // PSCA  (data.{} game.psca)
    "price_scale_below": 0.06,  // PSCT  (data.{} game.psct)
    "trend_decay_above": 0.04,  // PSFA  (data.{} game.psfa)
    "trend_decay_below": 0.08,  // PSFT  (data.{} game.psft)
    "restock_period_ticks": 99999
  }
}
```

`growth_threshold`/`decline_threshold` (`grow`/`decl`) are **not** global —
they're per-episode fields on `epis.<ep>` (see "Demand-tier" below and
`episodes.json`'s `economy` block in
[data-model.md](data-model.md#tables-episodesjson-from-epis-20-entries)).

These are now real EXE-derived starting values, not "pick your own" — but
they remain a balance-tuning surface for [modding.md](modding.md) (a mod's
`config.json` can still override them).

### Per-tick price update (per active `MarketGood`)

Direct port of `fcn.0046fcd0`'s decoded formula. Round 4-7 produced a
first-pass approximation of this formula; **Round 21
(`03-exe-analysis.md` "Round 21") fully linearized all 298 instructions** and
corrected several details (the `PSCA`/`PSCT` sign rule, the floor-snap
target, and the shape of the random-walk term). The pseudocode below reflects
Round 21:

```cpp
void update_price(MarketGood& g, const Commodity& comm, const EconomyConfig& cfg) {
    int base = comm.base_price;
    int base_half = base / 2;

    if (g.trend < 0) return; // observed only as a theoretical case; no-op

    if (g.trend > 0) {
        // "decay toward BASE/2" branch
        double factor = (g.current_price > base) ? cfg.trend_decay_below   // PSFT
                                                   : cfg.trend_decay_above; // PSFA
        double delta = factor * (g.trend * 0.01);
        double new_price = g.current_price * (1.0 - delta);
        g.current_price = (new_price < base_half) ? base_half : (int)new_price;
        g.trend--;
        return;
    }

    // g.trend == 0: "normal tick" branch
    int sum = g.stock + g.production_this_period + g.consumption_this_period;
    if (sum <= 0) return; // no activity this period: price unchanged

    // RATIO = (T + arg) / (2*T) if T > 0, else 1.0, where T is a global
    // real-time tick counter (`game->+0x18->+0x9c`) and `arg` is this
    // function's single (untraced) stack argument. NOT YET KNOWN — see Open
    // questions. cfg.price_ratio_term is a placeholder for this term.
    double ratio = cfg.price_ratio_term;

    double rand_frac = (rng.next_u15() % 701) / 1000.0; // [0, 0.7) — see RNG note
    double delta2 = (base / (double)g.current_price - rand_frac)
                    * (sum / (double)g.current_price) * ratio * 0.01;

    // s1: sign of (current_price - base), treating "==" as "<"
    // s2: sign of delta2, treating "==0" as ">0"
    bool s1_negative = g.current_price <= base;
    bool s2_negative = delta2 < 0;
    double factor = (s1_negative == s2_negative) ? cfg.price_scale_above   // PSCA: dampens
                                                   : cfg.price_scale_below; // PSCT: corrects faster

    double delta3 = factor * delta2;
    double new_price = g.current_price * (1.0 + delta3);
    g.current_price = (delta3 < 0 && new_price < base_half) ? base_half : (int)new_price;
}
```

Notes:

- **`jitter`/`rand_frac`**: the original uses MSVC's classic `rand()` LCG
  (`seed = seed*214013 + 2531011; (seed>>16) & 0x7fff`), `% 701`. The clone
  should use a standard modern PRNG (`std::mt19937` or PCG) seeded per-game
  (or per-save, for reproducibility) — `% 701` -> uniform `[0,700]` is
  reproduced via `std::uniform_int_distribution<int>(0, 700)`. Bit-for-bit
  matching the original's RNG sequence is **not** a goal.
- **`PSCA`/`PSCT` selection and the `BASE_HALF` floor-snap are now fully
  decoded** (Round 21) — the pseudocode above is a direct port, not an
  approximation. This *resolves* the earlier "low-stakes placeholder" framing
  from Round 4-7.
- **Remaining gap — `g.trend` and `ratio`**: two upstream pieces of this
  formula are genuinely **not understood** and are stand-ins above
  (`cfg.price_ratio_term`):
  1. **What makes `g.trend` (the `Good+0x00` linked list) nonzero** — i.e.
     under what real-game condition the "decay toward `BASE/2`" branch runs
     instead of the normal random-walk branch. Until this is known, the
     clone cannot decide *which* of the two branches a given commodity/tick
     should take; the struct comment above documents the *mechanical* effect
     of `trend > 0` but not its trigger.
  2. **The `RATIO = (T + arg) / (2*T)` term**: `T` looks like the same
     real-time tick counter Round 24 found for `SIM_HZ` (`game->+0x18->+0x9c`),
     but the `arg` operand passed into `fcn.0046fcd0` was not traced to its
     source. Without `arg`, `RATIO` can't be computed, so `delta2`'s
     magnitude (and therefore the per-tick price-drift speed) is unknown.

  These are tracked as an open RE item (see Open questions below and the
  roadmap) — `cfg.price_ratio_term` should **not** be tuned by feel as a
  substitute; it's a placeholder for a specific, recoverable original value.

### Period rollover (per `MarketGood`, every "period")

Direct port of `fcn.0046fcc0`:

```cpp
void rollover_period(MarketGood& g) {
    g.stock = g.production_this_period + g.consumption_this_period + g.stock;
    g.production_this_period = 0;
    g.consumption_this_period = 0;
}
```

"Period" length is a config constant (e.g. once per simulated in-game day —
tie to whatever calendar/date system [ui.md](ui.md) displays).

### Demand-tier (growth/decline) tracking

Direct port of `fcn.00474fb0` (Round 4-7), driving the `ratio` it compares
against via `fcn.00475120`, **fully linearized in Round 21**
(`03-exe-analysis.md` "Round 21"). `growth_threshold`/`decline_threshold`
come from the **current episode's** `grow`/`decl` fields (`episodes.json`'s
`economy` block — confirmed real `0x48`/float fields on `epis.<ep>`, Round
20), not a global config:

```cpp
// fcn.00475120: ratio = outlier-trimmed mean of value_i over every active
// epis.<ep>.dmnd record i.
double compute_demand_ratio(const std::vector<DmndRecord>& active_dmnd,
                             const EconomyConfig& cfg) {
    // value_i = (BASE_i / P_i) * (min(sasf * N_i, sasm) + 1.0)
    //   BASE_i = record i's commodity base price
    //   P_i    = round(record i's current price)
    //   N_i    = size of the linked list at record_i+0x00 (same kind of
    //            list as MarketGood::trend above) -- semantics NOT KNOWN,
    //            see Open questions.
    //   sasf = 0.2, sasm = 1.0 (data.{} game.sasf/game.sasm, Round 21)
    std::vector<double> values;
    for (auto& r : active_dmnd) {
        double weight = std::min(cfg.sasf * r.n, cfg.sasm) + 1.0;
        values.push_back((r.base / (double)r.price) * weight);
    }

    // Outlier trimming:
    //  - the maximum value is replaced by the second-highest (or dropped
    //    entirely if there's only one record).
    //  - the minimum value is dropped entirely IF that record's commodity
    //    is non-food (comm.food == 0).
    // ratio = trimmed_sum / trimmed_count (0 if trimmed_count == 0)
    return trimmed_mean(values, active_dmnd);
}

void update_demand_tier(MarketGood& g, const Commodity& comm, const Episode& ep,
                         double ratio /* from compute_demand_ratio() above */) {
    if (ratio >= ep.economy.growth_threshold) {
        if (increment_capped(g.demand_tier)) fire_event(MarketEvent::DemandGrowing, g);
    } else if (ratio < ep.economy.decline_threshold) {
        if (decrement_capped(g.demand_tier)) fire_event(MarketEvent::DemandDeclining, g);
    }
}
```

`demand_tier` corresponds to the `bres`/`bdem`/`bpro` "growing"/"declining"
status surfaced in the UI ([ui.md](ui.md)) and feeds AI building-priority
decisions ([opponent-ai.md](opponent-ai.md)). The `value_i`/outlier-trimming
formula and `sasf`/`sasm` constants above are a direct port (Round 21
resolved the earlier "approximate as `current_price/base_price`" placeholder
from Round 4-7).

**Remaining gap**: `N_i` — the size of the per-`dmnd`-record linked list at
`record+0x00` — is the same unresolved quantity as `MarketGood::trend` in the
price-update formula above (same list shape, same "Round 21 confirmed the
mechanical role but not what populates it" situation). Until `N_i`'s real-game
meaning is known, `value_i`'s `min(sasf * N_i, sasm)` weight term can't be
computed from first principles; `compute_demand_ratio()` above is otherwise a
complete port. See Open questions / roadmap.

`grow`/`decl` vary by episode — most are `grow≈1.15`/`decl≈0.7-0.8`, but the
two starter episodes (`ep00`/`ep01`) are more lenient (`grow=1.25`,
`decl=0.6`). The same `epis.<ep>` record also has `tick` (episode length in
ticks — already used for `late`/episode-end per Round 8), and three more
`0x48`/float fields not yet wired into the spec: `itra`/`ltra` ("initial"/
"last" trade ratio?, `0.75` for `ep00`/`ep01` vs `0.2-0.4` for later
episodes), `mpri` (min price ratio?, `0.1` for `ep00`/`ep01` vs
`0.2-0.25` later), and `popu` (population growth rate, flat `0.2`
everywhere). These look like further per-episode economy tuning knobs —
worth revisiting if `ep00`/`ep01`'s economy feels off relative to later
episodes (their `itra`/`ltra`/`mpri` values are notably different from the
rest).

### Stock arbitrage between connected markets

`03-exe-analysis.md`'s "two-market stock-balancing block" identified that the
original equalizes stock between two "connected" markets (e.g. a depot and
its attached market) based on relative price-to-stock ratios, but **the exact
pairing granularity and per-tick/per-commodity transfer formula were not
linearized** — only that such a block exists. The pseudo-algorithm below
("for each pair of markets connected by a completed trade route with the same
commodity tracked, if `price/stock` ratios differ, transfer
`min(available, |difference|)` units from the higher-ratio market's stock to
the lower-ratio market's, once per tick per connected pair") is a **clean-room
guess at the mechanic's shape**, not a port — it has not been checked against
the original's actual instructions. Treat this as a placeholder pending
disassembly of the stock-balancing block; see Open questions / roadmap.

## Production & consumption

Each production building (per [data-model.md](data-model.md)'s
`episodes.json` recipes — `inputs`/`outputs`) ticks as follows:

1. If the building has enough input-commodity stock (from its attached
   market/depot) for one production cycle, consume the inputs and add the
   outputs to `production_this_period` for the relevant `MarketGood`s, and
   decrement the building's "cycle timer".
2. The cycle timer resets based on a per-building `production_rate` (config/
   data-driven, not hardcoded) when production occurs.
3. If inputs are unavailable, the building is idle this tick (no
   production, no consumption) — surfaced in the UI as a status indicator.

Consumption-side (markets/population): each market accumulates
`consumption_this_period` for commodities its population/buildings demand,
driven by `episodes.json`'s `demand` table (original `epis.<ep>.dmnd`,
resolved per `documentation/08-investigation-needed.md` T0.2 — see
[data-model.md](data-model.md)). For each building present in/around a
market, and each `(commodity, {weight, amount})` entry in
`demand[building_type]`, the market accumulates `amount * weight *
building_count` units of `consumption_this_period` for that commodity each
period — `amount` is the per-tick/per-population base consumption quantity
(e.g. `dwel.rice.amount=4`) and `weight` (1 for staples, 2-15 for luxuries)
is read directly from `epis.<ep>.dmnd`. **Gap**: `amount * weight *
building_count` is a clean-room guess at how the original combines these two
authored fields into a per-tick consumption quantity — the actual consumer of
`dmnd`'s `(weight, amount)` pairs inside the simulation loop was not located/
disassembled, so this formula's *shape* (not just its tuning) is unconfirmed.
Note `weight` may instead (or also) feed the `sasf * N_i` term in the
demand-tier `ratio` formula above, which uses a structurally similar
"per-building-type weight" concept (`N_i`) whose source is also unresolved —
these two opens may turn out to be the same gap. See Open questions /
roadmap.

## Obsolescence (`supe`/`infe`/`remo`)

When a commodity is marked `superseded_by` another (per
[data-model.md](data-model.md)'s `episodes.json.commodities[id].obsolescence`),
its consumption demand should taper toward zero as the superseding
commodity becomes available — matching the original FAQ's stated design
("no one is going to pay for plain ceramics once dyed have been invented").
Implementation: once the superseding commodity is unlocked/available in an
episode, linearly ramp the obsolete commodity's demand multiplier from `1.0`
to `0.0` over a config-defined number of ticks. `remo` (fully removed)
commodities have demand multiplier `0.0` immediately once the trigger
condition is met.

## Technology progression

- Each player has a set of `researched` technology IDs (see
  [world-and-maps.md](world-and-maps.md) save format).
- A technology becomes **available to research** once
  `tick >= available_at_tick` (from `technologies.json`) for the current
  episode — this models the original's `dela` field (tech offered at a
  given game-tick).
- Researching costs `cost` coins, deducted from the player's treasury
  immediately on research (no original "build queue" complexity needed for
  techs).
- On research, `unlocks.buildings`/`.transporters`/`.pathways` become
  available in the relevant menus ([ui.md](ui.md)) for that player.

## Episodes & events

- An **episode** (`epis.<id>`) defines the active map, available
  commodities/buildings/techs, starting conditions, and a `late` tick
  (episode end / scoring trigger — corresponds to the original's
  `data.epis.<n>.late` field referenced in `SilkRoadGame::Tick`,
  `03-exe-analysis.md` Round 8).
- **Events** (`events.json`) are *not* given the original's exact trigger
  mechanism (not decoded). The clone defines its own simple **timed/
  conditional event queue**:

```cpp
struct ScheduledEvent {
    int fire_at_tick;
    std::string event_id;       // -> events.json
    // optional: condition callback for conditional (non-timed) events
};
```

Each tick, pop and fire any `ScheduledEvent`s whose `fire_at_tick <= tick`.
Random events (per `events.json`'s entries like "Barbarian Invasion",
"Blight") are scheduled probabilistically each periodic-update interval
(config-defined probability per event type per episode) rather than at
fixed ticks. Event *effects* (what "Civil Unrest" actually does to the
simulation) are **not specified by the original's data** and should be
designed fresh per event type — start with a small, easy-to-extend
`EventEffect` interface (one small class/function per event type,
registered by `event_id`) so new events are additive.

## Open questions / RE gaps

- ~~**`SIM_HZ` (tick rate)**~~ **Resolved (2026-06-11)** — `SIM_HZ = 2`
  (one tick per 500ms, 2-ticks/frame catch-up cap), see "Tick model" above.
  (Workstream B item B1, `03-exe-analysis.md` Round 24.)
- ~~**`PSCA`/`PSCT`/`PSFA`/`PSFT`/`grow`/`decl` numeric values**~~
  **Resolved (2026-06-11)** — all are real `data.{}` fields: `PSCA=0.04`/
  `PSCT=0.06`/`PSFA=0.04`/`PSFT=0.08` on the root `game` table, `grow`/`decl`
  per-episode on `epis.<ep>` (see "Tunable constants"/"Demand-tier" above).
  (Tier 1 item B2, `03-exe-analysis.md` Round 20.)
- ~~**Exact `PSCA`/`PSCT` sign-selection branch**~~ **Resolved (2026-06-11)**
  — `s1 == s2 -> PSCA`, `s1 != s2 -> PSCT` (`s1=sign(P-BASE)`,
  `s2=sign(delta2)`), floor-snaps to `BASE/2` not `BASE`. See "Per-tick price
  update" above. (Tier 1 item B3, `03-exe-analysis.md` Round 21.)
- ~~**`fcn.00475120`'s demand-tier ratio formula**~~ **Resolved (2026-06-11)**
  — outlier-trimmed mean of `value_i = (BASE_i/P_i) * (min(sasf*N_i, sasm) +
  1.0)`, `sasf=0.2`/`sasm=1.0`. See "Demand-tier" above. (Tier 1 item B3,
  `03-exe-analysis.md` Round 21.)
- **OPEN — `MarketGood::trend`/`N_i` (the `Good+0x00`/`dmnd-record+0x00`
  linked-list size) and the price-update `RATIO`'s `arg` operand**: Round 21
  fully linearized *how* these values are used (branch selection, decay rate,
  demand-tier weighting) but not *what sets them* in the live simulation. This
  is the actual remaining gap behind both the price-drift formula and the
  demand-tier ratio — see the "Remaining gap" notes under "Per-tick price
  update" and "Demand-tier" above. Tracked in
  `documentation/00-roadmap.md` (spec-fidelity workstream) and
  [`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  items 4/5.
- **OPEN — stock-arbitrage transfer formula**: the original's two-market
  stock-balancing block exists but its exact pairing/transfer rule wasn't
  linearized; the "Stock arbitrage" section above is a clean-room guess at
  the mechanic's shape pending disassembly. Tracked in
  [`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  item 12.
- **OPEN — `dmnd` consumption-accumulation formula** (`amount * weight *
  building_count`): a clean-room guess at how `epis.<ep>.dmnd`'s
  `(weight, amount)` pairs feed `consumption_this_period`; the actual
  consumer wasn't located. May be related to the `N_i`/`sasf` gap above.
  Tracked in
  [`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  item 12.
- **OPEN — `itra`/`ltra`/`mpri`/`popu` per-episode economy fields**: real
  `0x48`/float fields on `epis.<ep>` (decoded values known, see "Demand-tier"
  above) but no consumer in the simulation formula was identified — likely
  related to the same `RATIO`/`N`/`weight` family of gaps above. Tracked in
  [`implementation/spec-deviations.md`](../implementation/spec-deviations.md)
  item 5.
