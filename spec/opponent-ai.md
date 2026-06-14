# Opponent AI

This document specifies the AI for non-human players: merchant trade-route
behavior and strategic (building/research) decisions.

**OPEN RE GAP** (`implementation/spec-deviations.md` item 13): RE analysis
partially disassembled the original's `Player::Update` and
`Character::Update` functions — this is **further along than "never
disassembled"**, but still far from a portable algorithm:

- **Decoded**: the merchant arrival/order-completion state machine (did the
  merchant arrive at its order's pickup/delivery market?
  complete-and-pop the order, or replan); a 1000-tick (`data.game.mnor`,
  confirmed) periodic scan that posts a "Merchant is not assigned to any
  route" UI nag for AI players' idle merchants; and a once-near-game-start
  (999999-tick default) periodic upkeep-cost payment over a character's
  abilities list.
- **Located but undecoded**: `Player::Update` contains a strategy dispatcher
  that computes an economic score (a rolling average of up to 10 entries
  from a per-player list) vs. a `99999.0` threshold, then branches to one of
  two strategy functions, followed by two always-run evaluation functions —
  the strongest candidate for "what to build/research/trade next" AI strategy
  logic, but none of those functions (nor the order-replan function, nor
  three "advisor" objects) have been decoded.

Until those functions are decoded, **there is no original strategy algorithm
to port** — but unlike the old framing, this is a *specific, bounded* gap
(a handful of identified functions and their data fields), not "the whole
subsystem is unknown." See "AI modes" below for how this gap is handled in
the clone's design, and "Open questions / RE gaps" for the tracked
investigation items.

## Scope for the spike vs. later

The toolchain spike does not require AI. This spec describes the design so
that [implementation/](../implementation/) can schedule it, but a minimal
playable build can ship with **zero AI players** (only the human player,
plus neutral/environment entities like special-resource sites). Add AI
incrementally:

1. **Stage 1**: AI players exist and own starting buildings/treasury but
   take no actions (effectively "decoration"). Validates that
   multi-player-owner state (treasury, ownership) works.
2. **Stage 2**: AI merchants run simple trade loops (below).
3. **Stage 3**: AI players make building/research decisions (below).
4. **Stage 4** (optional, low priority): AI difficulty tuning, "personality"
   variation between AI players.

## Merchant trade-route AI (Stage 2)

Each AI-owned merchant ([entities.md](entities.md)'s `MerchantState`) runs a
simple per-tick (or per-periodic-update, for less CPU work) decision loop
when its `orders` queue is empty:

```
1. Look at the merchant's home market's MarketGood list.
2. Find the commodity with the best "sell here, buy elsewhere" price
   spread reachable within the merchant's effective range (a config-
   defined max path length, scaled by the merchant's speed/capacity).
3. If a profitable spread exists above a minimum-margin threshold:
   enqueue orders: [BuyAndDeliver(commodity, qty, target=cheap_market),
                    Sell(commodity, qty, target=home_market)]
   (i.e. buy low at the target market, bring it home, sell high — or the
   reverse direction, whichever is profitable).
4. If no profitable spread exists: idle for a config-defined cooldown
   before re-evaluating (avoid re-scanning every tick when nothing's
   changed).
```

- **"Reachable within range"**: limit candidate markets to those within a
  config-defined tile-distance (or precomputed path length) of home, to
  bound the search. A simple approach: maintain a per-region list of
  markets, sorted by distance from each market (computed once at map load),
  and only consider the nearest K.
- **Order fulfillment**: when a merchant arrives at a `BuyAndDeliver`
  order's target market, transfer `quantity` units of `commodity_id` from
  the market's `MarketGood.stock` to the merchant's `cargo` (if available;
  partial fulfillment if the market has less than `quantity`), pay the
  market `quantity * current_price` from... — see "Economics of AI trades"
  below. On `Sell`, the reverse: transfer cargo to the market's stock,
  credit the merchant's owning player's treasury.
- **Capacity**: `quantity` is bounded by the transporter's cargo capacity
  (`transporters.json`, see [data-model.md](data-model.md) Open questions
  re: capacity numbers).

### Economics of AI trades

AI merchant trades should flow through the **same `MarketGood` price/stock
state** as the human player's actions ([simulation.md](simulation.md)) —
there's no separate "AI economy". This means AI trading naturally affects
prices (buying drives price up, selling drives it down via the
`production_this_period`/`consumption_this_period` accumulators), which is
the intended emergent behavior (AI and human players compete for the same
markets).

## Strategic AI (Stage 3)

Each AI player, on a **periodic** (not per-tick) cadence (config-defined,
e.g. every N simulated days):

1. **Research decisions**: if the player has enough treasury and an
   available (per [simulation.md](simulation.md)'s `available_at_tick`)
   unresearched technology exists, research the cheapest one that unlocks a
   building/transporter the player doesn't yet have. (Simple greedy rule —
   refine only if it produces clearly bad AI behavior in playtesting.)
2. **Building decisions**: if treasury exceeds a building's cost plus a
   safety margin, and a valid placement exists near the player's existing
   buildings (search outward from existing footprint, using
   [input.md](input.md)'s `check_placement`), build the
   highest-priority building per a simple scoring heuristic:
   - Production buildings whose output commodity has a **high
     `demand_tier`** (per [simulation.md](simulation.md)) score higher —
     i.e. AI reacts to the same growth/decline signals the UI shows the
     player.
   - Otherwise, fall back to a fixed priority order per culture/episode
     (data-driven: `episodes.json` could carry an
     `ai_building_priority: [building_id, ...]` list, extractor-derived or
     hand-authored — this is new clone data, not from the original).
3. **New merchant decisions**: if treasury allows and the player has fewer
   than a config-defined number of active merchants, spawn a new merchant
   (cost = transporter's build cost) at the player's depot.

## Difficulty levels

Implement as **multipliers on the above**, not separate code paths (this
applies to `modern` mode; `classic` mode's difficulty knobs, once portable,
are whatever the original exposed — likely the same `data.game.mnor`-style
tunables identified during RE analysis):
- Treasury starting bonus.
- Periodic-decision cadence (faster decisions = harder).
- Minimum-margin threshold for trades (lower = more aggressive trading).
- Building-priority scoring weight randomization (adds variety between AI
  players at the same difficulty).

## AI modes: Classic (faithful port) vs. Modern (reinterpretation)

Per the project's RE-fidelity goals, the original's AI is **not** something
to approximate-and-move-on from — it should eventually be fully reverse
engineered and faithfully ported. At the same time, the user has explicitly
asked for a **modern reinterpretation as a first-class, independently
valuable option**, not merely a stopgap for the missing original algorithm.
The clone therefore exposes a per-AI-player **`ai_mode`** setting (selectable
at game setup; a match can mix modes across AI players):

- **`ai_mode: classic`** — runs the original's strategic-AI dispatcher as
  decoded from `Player::Update` (see the gap analysis above): the
  economic-score-vs-threshold check, the strategy branch, the always-run
  evaluation steps, plus the 1000-tick idle-merchant nag and the
  `Character::Update` arrival/order-completion state machine (already
  decoded and usable now). **Until the strategy and evaluation functions
  are decoded, `classic` has no strategy logic to run** — it falls back to
  `modern` and the UI should say so (e.g. "Classic AI is still being
  researched; using Modern AI for this player"), rather than silently
  presenting a "classic" experience that's secretly the modern one.
- **`ai_mode: modern`** — the clean-room trade-route/strategic design in the
  "Merchant trade-route AI" and "Strategic AI" sections above. This is kept
  and developed as a **permanent alternative**, not retired once `classic`
  becomes portable — the goal is two genuinely different, selectable AI
  experiences.

Both modes operate through the same world/economy state, the same
`check_placement` ([input.md](input.md)) and order-queue infrastructure
([entities.md](entities.md)), and the same difficulty multipliers where
applicable — `ai_mode` only changes *which decision logic* picks moves, not
the rules those moves must obey or the state they act on.

## Open questions / RE gaps

- **OPEN — `Player::Update` strategy dispatcher** (spec-deviations item 13):
  RE analysis located the strategy dispatcher — the prime suspect for "what
  to build/research/trade next" — but none of the strategy functions, the
  order-replan function, the three "advisor" objects, nor the economic-score
  computation's data fields have been decoded. This is the blocker for a
  faithful `classic` `ai_mode` (see "AI modes" above).
- ~~**`char.orde`/`invo` decoding**~~ **Resolved** — `char.orde` is a
  small (2-8 slot) ring-buffer of order records. Each slot has a `type`
  discriminator: `type=1` (travel), `type=2` (trade: `mark` + `tran` +
  commodity-qty), `type=3` (terminal sentinel marking end of active route),
  `type=4` (return-to-depot). The real data validates `entities.md`'s
  clean-room `TradeOrder{kind, commodity_id, quantity, target_market}`
  design — the original's structure matches that shape. `char.invl` uses
  the same ring format but is always empty in observed saves (population
  trigger unknown, low priority).
