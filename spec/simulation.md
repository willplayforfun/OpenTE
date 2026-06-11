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
  `SIM_HZ` is a config constant — start at a value that feels close to the
  original (e.g. somewhere in the 10-20 Hz range; tune empirically, the
  exact original tick rate wasn't recovered — see Open questions) and adjust
  during playtesting.
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
    int trend;                  // 0 = none; >0 = active correction trend, ticks remaining
    int demand_tier;            // long-term "growing"/"declining" counter (signed)
};
```

This is a direct, renamed port of the original "Good" record fields
(`+4`=price, `+8`=stock, `+0xc`=production, `+0x10`=consumption, `+0xb0`=
demand tier — `03-exe-analysis.md` "Round 4-7"/"Round 8"). The names are
made explicit since the originals were only inferable from usage.

### Tunable constants (`config.json`)

Four master tuning knobs, ported verbatim from the original (where `PSCx`
controls normal per-tick price drift and `PSFx` controls multi-tick
trend-driven decay; the `A`/`T` suffix splits each pair by the sign of the
price delta):

```json
{
  "economy": {
    "price_scale_above": 0.0,   // PSCA
    "price_scale_below": 0.0,   // PSCT
    "trend_decay_above": 0.0,   // PSFA
    "trend_decay_below": 0.0,   // PSFT
    "growth_threshold": 0.0,    // 'grow'
    "decline_threshold": 0.0,   // 'decl'
    "restock_period_ticks": 99999
  }
}
```

The original's exact numeric values for these were not extracted (they live
in an embedded config-text blob, not `data.{}` — `03-exe-analysis.md` Round
4-7 found the *names* and *call sites* but not the *values*). **The clone
must choose its own starting values and tune by playtesting** — this is
explicitly a balance-tuning surface for [modding.md](modding.md), not a
"decode this number" RE task. See Open questions.

### Per-tick price update (per active `MarketGood`)

Direct port of `fcn.0046fcd0`'s decoded formula
(`03-exe-analysis.md` "Round 4-7"):

```cpp
void update_price(MarketGood& g, const Commodity& comm, const EconomyConfig& cfg) {
    int base = comm.base_price;
    int base_half = base / 2;
    int target = base;

    if (g.trend > 0) {
        double factor = (g.current_price <= target) ? cfg.trend_decay_above
                                                      : cfg.trend_decay_below;
        double decay = factor * (g.trend * 0.01);
        double candidate = g.current_price * (1.0 - decay);
        g.current_price = (candidate < base_half) ? target
                                                    : (int)candidate;
        g.trend--;
        return;
    }

    int total = g.production_this_period + g.consumption_this_period + g.stock;
    if (total <= 0) return; // no activity this period: price unchanged

    double R = 0.5; // see "period_len" note below
    double price_ratio = (double)base / g.current_price;
    double stock_ratio = total / 1000.0;
    double jitter = (rng.next_u15() % 701) / (double)g.current_price; // see RNG note
    double delta = (price_ratio - stock_ratio) * jitter * R * 0.01;
    delta *= (delta < 0) ? cfg.price_scale_above : cfg.price_scale_below; // see sign-rule note

    double new_price = g.current_price * (1.0 + delta);
    g.current_price = (delta < 0 && new_price < base_half) ? target : (int)new_price;
}
```

Notes / deviations from a literal port:

- **`R = 0.5`**: the original computes `R = (period_len + X) / (period_len *
  2)` with an unidentified second term `X` (likely `0` in the common case,
  giving `R ≈ 0.5`). The clone hardcodes `0.5` and exposes it as
  `cfg.tick_smoothing` if tuning later proves it needs to vary.
- **`jitter`**: the original uses MSVC's classic `rand()` LCG (`seed =
  seed*214013 + 2531011; (seed>>16) & 0x7fff`), `% 701`. The clone should
  use a standard modern PRNG (`std::mt19937` or PCG) seeded per-game (or
  per-save, for reproducibility) — `% 701` -> uniform `[0,700]` is
  reproduced via `std::uniform_int_distribution<int>(0, 700)`. Bit-for-bit
  matching the original's RNG sequence is **not** a goal.
- **`PSCA`/`PSCT` selection**: the original's exact sign-comparison branch
  logic for choosing `PSCA` vs `PSCT` wasn't fully linearized (RE notes:
  "net effect is PSCA or PSCT is chosen depending on the sign of delta and
  how it compares to target"). The pseudocode above uses "negative delta ->
  `price_scale_above`" as a placeholder approximation matching the
  `PSFA`/`PSFT` trend-branch's `current_price <= target` convention; verify
  against playtested feel and adjust — this is a low-stakes tuning detail,
  not a correctness issue.

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

Direct port of `fcn.00474fb0` (Round 4-7):

```cpp
void update_demand_tier(MarketGood& g, const Commodity& comm, const EconomyConfig& cfg) {
    double ratio = (double)g.current_price / comm.base_price; // approximation, see note
    if (ratio >= cfg.growth_threshold) {
        if (increment_capped(g.demand_tier)) fire_event(MarketEvent::DemandGrowing, g);
    } else if (ratio < cfg.decline_threshold) {
        if (decrement_capped(g.demand_tier)) fire_event(MarketEvent::DemandDeclining, g);
    }
}
```

`demand_tier` corresponds to the `bres`/`bdem`/`bpro` "growing"/"declining"
status surfaced in the UI ([ui.md](ui.md)) and feeds AI building-priority
decisions ([opponent-ai.md](opponent-ai.md)). The exact `ratio` formula
(`fcn.00475120`) wasn't fully decoded; `current_price/base_price` is a
reasonable approximation — refine if demand-tier behavior feels off in
playtesting.

### Stock arbitrage between connected markets

The original equalizes stock between two "connected" markets (e.g. a depot
and its attached market) based on relative price-to-stock ratios
(`03-exe-analysis.md` "two-market stock-balancing block"). The clone
reproduces this as: for each pair of markets connected by a completed trade
route with the same commodity tracked, if `price/stock` ratios differ,
transfer `min(available, |difference|)` units from the higher-ratio market's
stock to the lower-ratio market's. This runs once per tick per connected
pair, not per commodity-pair — keep it simple, this is "smoothing", not a
core mechanic.

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
scales how strongly that commodity's price/demand-tier reacts relative to
staples. This replaces the earlier "flat per-population-unit demand for
food-flagged commodities" placeholder; refine the `weight` scaling formula
during playtesting if demand-tier behavior feels off.

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

- **`SIM_HZ` (tick rate)**: the original's real-time tick rate (ticks per
  second) wasn't pinned down (`03-exe-analysis.md` Round 8 found the
  dispatch chain but not its driving frequency). Choose empirically.
- **`PSCA`/`PSCT`/`PSFA`/`PSFT`/`grow`/`decl` numeric values**: not
  extracted (embedded config, not `data.{}`). Clone defines its own,
  documented in `config.json`. (Tier 1 item B2 — see
  `documentation/08-investigation-needed.md`.)
- **Exact `PSCA`/`PSCT` sign-selection branch**: approximated, see formula
  notes above. (Tier 1 item B3.)
- **`fcn.00475120`'s demand-tier ratio formula**: approximated as
  `price/base_price`.
