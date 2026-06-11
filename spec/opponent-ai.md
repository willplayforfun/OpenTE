# Opponent AI

This document specifies the AI for non-human players: merchant trade-route
behavior and strategic (building/research) decisions. **The original's
`Player::Update` (`0x4a42f0`) was never disassembled** — RE work stopped
after locating it via the entity-update dispatch table
(`documentation/03-exe-analysis.md` Round 8). This entire document is
therefore a **clean design**, informed only by the *data* the original
exposes that an AI would plausibly act on (commodity prices/demand tiers,
production recipes, tech costs) — not by any decoded original algorithm.

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

Implement as **multipliers on the above**, not separate code paths:
- Treasury starting bonus.
- Periodic-decision cadence (faster decisions = harder).
- Minimum-margin threshold for trades (lower = more aggressive trading).
- Building-priority scoring weight randomization (adds variety between AI
  players at the same difficulty).

## Open questions / RE gaps

- **Everything in this document is a clean design** — `Player::Update`
  (`0x4a42f0`) was never decoded, so there is no original algorithm to
  cross-check against. If `Player::Update` is decoded in a future RE
  session, treat it as a *reference for tuning feel*, not a spec to match
  exactly — the clone's AI architecture (data-oriented entity updates, see
  [entities.md](entities.md)) is intentionally different from whatever
  virtual-dispatch design the original used.
- **`char.orde`/`invo` decoding** ([world-and-maps.md](world-and-maps.md)/
  [entities.md](entities.md) Open questions) could, if completed, reveal the
  original's actual order-queue structure and provide a stronger reference
  for Stage 2's design — currently `TradeOrder` is a clean-room
  approximation of "what such a queue would need to contain".
