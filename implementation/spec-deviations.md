# Spec Deviations — Review Queue

This document tracks places in [`OpenTE/spec/`](../spec/) where the design
settles for "good enough"/"approximation"/"tune via playtesting" instead of
exactly replicating the original's behavior. Per `CLAUDE.md`, the clone
should match the original exactly **to start** — these hedges are exactly
the kind of thing that needs a deliberate decision (port more precisely now,
or consciously accept the deviation with a recorded reason), not a default
"sufficient" left unexamined.

For each item: what the spec currently says, what's known/unknown from RE,
and a status field to fill in once reviewed. Update this doc as items are
resolved — move resolved items to a "Resolved" section at the bottom with the
decision and rationale, rather than deleting them.

## How to use this doc

For each open item, decide one of:

- **Port more precisely** — go back to the RE notes / disassembly and nail
  down the original's actual algorithm/values, then update the spec.
- **Accept deviation, document why** — the difference is genuinely
  immaterial (or the original's approach is undesirable for the clone) and
  the spec should say so explicitly instead of "sufficient for now".
- **Defer with a concrete trigger** — acceptable for now, but record what
  would make it worth revisiting (not just "if it feels off in playtesting").

---

## Tier 1 — Gameplay rules with a known (or partially known) original algorithm

These are the highest-priority items: RE work already characterized what the
original does, but the spec opts for a looser stand-in anyway.

### 1. Blocked-neighbor placement rule (`TooManyBlockedNeighbors`)

- **Where**: [input.md](../spec/input.md) step 5 of the placement-legality
  algorithm, Open questions "OPEN — `TooManyBlockedNeighbors` reasons
  `0x10`/`0x11`"
- **Spec now says**: reason `0xe` ("at most one footprint-edge tile adjacent
  to a category-2 trail/road tile") is a **direct, confirmed port** (Round
  29). Reasons `0x10`/`0x11` (a second diagonal-blocked counter + per-tile
  corner flags) are flagged as an **open RE gap** — the original is confirmed
  *stricter* than the `0xe`-only check, but the exact thresholds aren't known.
- **What's known**: `documentation/03-exe-analysis.md` Rounds 18/29 decoded
  reason `0xe` precisely; `0x10`/`0x11` located but not arithmetically
  isolated.
- **Status**: Partially resolved (reason `0xe` ported) / **Open** for
  `0x10`/`0x11`. Tracked in `documentation/00-roadmap.md` Workstream I item 1.

### 2. Exclusion-radius shapes (`shape_id` -> diamond/radius tables)

- **Where**: [input.md](../spec/input.md) "Footprint 'shape' (exclusion
  radius)", Open questions "OPEN — exclusion-radius `shape_id` mapping"
- **Spec now says**: the original's per-row exclusion-radius shape tables
  (Round 29, `shape_id` 4/6/8/10 following `(-(r), r-1)` per row) are
  **concrete, real game data** — not an approximation. The clone's
  `exclusion_shape` field is explicitly framed as a **placeholder for data
  not yet recovered** (the building -> `shape_id` mapping), not an accepted
  deviation.
- **What's known**: the shape *data* already exists in extracted form — the
  missing piece is the building -> `shape_id` mapping (dispatcher
  `fcn.00466e40` located but not traced).
- **Status**: Open. Tracked in `documentation/00-roadmap.md` Workstream I
  item 2.

### 3. Pathway drag-to-build segment shape (two-segment L-path)

- **Where**: [input.md](../spec/input.md) "Drag-path computation", Open
  questions "OPEN — drag-path segment tie-break rule"
- **Spec now says**: `fcn.0049e5a0` confirms the original builds a
  **segment list** (not a general A* search) from the raw drag endpoints —
  this part is a **port-validated fact**, not an approximation. The clone's
  two-segment "whichever has fewer invalid tiles" L-path is explicitly
  flagged as an **unverified stand-in for the real tie-break rule**, which
  lives in undecoded `fcn.416750`/`fcn.4168c0`/`fcn.4a1880`/`fcn.46d180`.
- **What's known**: not-A*-confirmed; tie-break rule (axis order, L
  orientation, map-edge behavior) not decoded.
- **Why flagged**: the clone's heuristic could differ from the original's
  actual rule (e.g. "always horizontal-first") in visible ways during
  pathway construction.
- **Status**: Open. Tracked in `documentation/00-roadmap.md` Workstream I
  item 3.

### 4. `PSCA`/`PSCT` sign-selection branch (price-drift formula)

- **Where**: [simulation.md](../spec/simulation.md) "Per-tick price update"
  (`update_price()`), Open questions "OPEN — `MarketGood::trend`/`N_i`...
  and the price-update `RATIO`'s `arg` operand"
- **Spec now says**: **Resolved and ported (Round 21)** — the full
  `update_price()` formula, including the `s1==s2 -> PSCA, s1!=s2 -> PSCT`
  sign-selection branch (`s1=sign(P-BASE)`, `s2=sign(delta2)`), the
  `BASE/2` floor-snap, and the `trend`-driven decay branch (`PSFA`/`PSFT`),
  is now a direct port, not an approximation. **Residual gap**: what
  populates/decrements `g.trend`/`N_i` (the `Good+0x00` linked-list size)
  and what `RATIO`'s `arg` operand is — these are inputs *to* the
  now-correct formula, not the formula itself.
- **What's known**: branch logic, constants (`PSCA=0.04`/`PSCT=0.06`/
  `PSFA=0.04`/`PSFT=0.08`), and floor-snap all confirmed (Round 20/21) and
  ported. `trend`/`N_i`/`RATIO arg` producers not yet found.
- **Status**: Resolved for the formula itself; **Open** for the `trend`/`N_i`/
  `RATIO arg` producers. Tracked in `documentation/00-roadmap.md` Workstream I
  item 4.

### 5. Demand-tier ratio formula (`fcn.00475120`)

- **Where**: [simulation.md](../spec/simulation.md) "Demand-tier
  (growth/decline) tracking" (`compute_demand_ratio()`), Open questions
  "OPEN — `itra`/`ltra`/`mpri`/`popu` per-episode economy fields"
- **Spec now says**: **Resolved and ported (Round 21)** — `fcn.00475120` is
  an outlier-trimmed mean of `value_i = (BASE_i/P_i) * (min(sasf*N_i, sasm) +
  1.0)`, `sasf=0.2`/`sasm=1.0`, now implemented as `compute_demand_ratio()`/
  `update_demand_tier()`. This is no longer an approximation.
  **Residual gap**: four per-episode `0x48`/float fields (`itra`/`ltra`/
  `mpri`/`popu`) are decoded values with no identified consumer in this or
  any other formula — likely related to the same `N_i`/`RATIO` family as item
  4's residual.
- **Why flagged**: `demand_tier` drives the growing/declining indicators
  shown directly to the player and feeds AI building-priority decisions
  (opponent-ai.md) — getting the *formula* right (now done) matters, but the
  unconsumed `itra`/`ltra`/`mpri`/`popu` fields could still represent a
  missing modifier to that formula.
- **Status**: Resolved for the ratio formula; **Open** for
  `itra`/`ltra`/`mpri`/`popu` and the related `dmnd` consumption formula.
  Tracked in `documentation/00-roadmap.md` Workstream I item 5.

---

## Tier 2 — Placement/world rules where the spec designs around an RE gap

The original RE notes don't fully cover these, so the spec substitutes a
clone-side design decision rather than the original's rule. Lower priority
than Tier 1 (there's no known original algorithm being skipped), but still
worth a second look since some of these are core "can I build here?" checks.

### 6. Capacity check (`CapacityExceeded`)

- **Where**: [input.md](../spec/input.md) step 6 of the placement-legality
  algorithm, Open questions "OPEN — `CapacityExceeded` denominator"
- **Spec now says**: the numerator (`data.bldg.<episode>.<recipe_id>.cost`)
  is **resolved (Round 29)** — already-extracted, known field. The
  denominator (`this->+0x10.vtable[0]()`, a virtual call on an unidentified
  runtime object) remains unresolved, so **there is no original comparison to
  port** until it's found — not "likely a no-op," but "literally cannot be
  implemented as a port yet."
- **Status**: Open — needs dynamic analysis to identify the denominator's
  object before deciding the clone's behavior. Tracked in
  `documentation/00-roadmap.md` Workstream I item 6.

### 7. Special-type adjacency (`SpecialAdjacencyFailed`)

- **Where**: [input.md](../spec/input.md) step 8 of the placement-legality
  algorithm, Open questions "OPEN — `SpecialAdjacencyFailed` per-category
  rules"
- **Spec now says**: `fcn.473ae0(building_id)` -> `data.bldg.defa.<id>.type`
  ∈ {`bpro`,`bdem`,`bdep`,`bres`} is **resolved (Round 29)** for all 153
  buildings — every building has a category, confirmed real data. **What
  each category's adjacency rule actually checks** (the dispatch branches'
  effects, plus runtime-initialized globals `0x647734`/`0x644568`) is
  undecoded, so `requires_adjacent_type` has no rule to populate yet.
- **Why flagged**: this isn't an approximation — it's a known
  placement-legality check from the original with **no clone-side rule at
  all** yet, now scoped to a specific decoding target (`fcn.473ae0`'s 4
  category branches + 2 globals).
- **Status**: Open. Tracked in `documentation/00-roadmap.md` Workstream I
  item 7.

### 8. Terrain band -> clone terrain-type mapping

- **Where**: [world-and-maps.md](../spec/world-and-maps.md) Open questions
  "OPEN — Terrain band -> clone terrain-type mapping"
- **Spec now says**: the 4 `mapp.terr` bands (`2-8`/`33-40`/`64-70`/`96-102`)
  are **confirmed real groupings** in the raw data, but what each band
  represents and its correspondence to the `m_ui,u.{}` `terr/ts*`
  texture-selection table was never cross-referenced. The current
  `{water, plains, hills, mountains}` mapping is a **guess pending that
  cross-reference**, not a confirmed-good coarse result — a wrong
  band/type pairing could misclassify entire terrain categories (e.g.
  "desert" extracted as "plains"). The map JSON schema is unaffected either
  way (only the extractor's mapping table changes).
- **Status**: Open — tracked as a real open RE question, not deferred
  polish. Tracked in `documentation/00-roadmap.md` Workstream I item 8.

### 9. Pre-existing trail connectivity from terrain at map load

- **Where**: [world-and-maps.md](../spec/world-and-maps.md) Open questions
  "OPEN — Pre-existing trails from terrain data"
- **Spec now says**: whether the original pre-populates `trail_extra`
  connectivity from terrain at load time has **no RE finding either way** —
  the clone's "empty, fully player-constructed" default is an **unverified
  stand-in for an unanswered question**, not a confirmed match. If a map
  needs pre-existing trails to be playable (per workstream G/H starting-state
  extraction), that would indicate the original does pre-populate
  connectivity.
- **Status**: Open. Tracked in `documentation/00-roadmap.md` Workstream I
  item 9.

---

## Tier 3 — "Deliberate departure" calls that justify themselves inline

These are framed in the spec as confident architectural decisions, but the
justification is asserted rather than confirmed against RE evidence — the
kind of self-justifying language CLAUDE.md flags as a tell.

### 10. Pathfinding edge-cost model (`movement_costs` vs. uniform A* cost)

- **Where**: [entities.md](../spec/entities.md) "Pathfinding" -> "Cost — OPEN
  RE GAP"
- **Spec now says**: the original's actual A* (`DefaultCostCalculator`,
  Round 15) is **confirmed uniform-cost** (1000/tile, network-agnostic) —
  network choice doesn't bias the route search in the original.
  `episodes.json.movement_costs` (from `epis.<ep>.path`) is real,
  already-decoded data, but **what consumes it in the original is unidentified**
  (could be UI cost estimates, AI route scoring, or per-network speed). The
  clone's use of `movement_costs` as its A* edge cost is now framed as an
  **unconfirmed stand-in** pending identification of the real consumer — not
  a settled "deliberate improvement."
- **Why flagged**: this changes core trade-route routing behavior (e.g.
  "prefer existing roads") versus the original, where routing is uniform-cost
  and *only* movement speed varies by network/transporter. If the original
  really does route uniformly, AI merchants in the original may take routes
  the clone would never choose (and vice versa) — a gameplay-visible
  difference. Identifying `movement_costs`'s real consumer would resolve
  whether the clone's edge-cost choice is a fidelity bug or matches an
  original UI/AI-only usage.
- **Status**: Open. Tracked in `documentation/00-roadmap.md` Workstream I
  item 10.

### 11. "Any connectivity change invalidates all cached paths"

- **Where**: [entities.md](../spec/entities.md) "Path following" ->
  "Implementation notes" ("OPEN RE GAP" under path-cache invalidation)
- **Spec now says**: whether the original re-paths *every* merchant on any
  connectivity change, or only merchants whose route passes near the change
  (producing a visible all-merchants "hitch" or not), **has no RE finding
  either way** — this is now framed as an open question about whether the
  clone's behavior is even *correct* relative to the original's player-visible
  feel, independent of performance. It is **not** "acceptable for the spike,
  refine if profiling shows it matters" — that framing assumed the only thing
  at stake was performance.
- **Why flagged**: could cause visible merchant-pathing hitches the original
  may not exhibit. If profiling/playtesting suggests a visible difference,
  bounding-box invalidation is the candidate fix, but confirming the
  original's actual behavior should come first.
- **Status**: Open. Tracked in `documentation/00-roadmap.md` Workstream I
  item 11.

---

## Tier 4 — Tuning constants / formula simplifications (lower priority)

Minor coefficients or systems where the original's exact value likely isn't
recoverable, or where the simplification is small in scope. Included for
completeness but lower priority than Tiers 1-3.

### 12. Economy formula minor simplifications (`R=0.5`, stock-arbitrage, `SIM_HZ`, demand `weight` scaling)

- **Where**:
  - `R = 0.5` hardcode — [simulation.md](../spec/simulation.md) "Tunable
    constants"
  - Stock-arbitrage "smoothing" between connected markets —
    [simulation.md](../spec/simulation.md) "Stock arbitrage" / Open questions
    "OPEN — stock-arbitrage transfer formula"
  - `SIM_HZ` tick rate — [simulation.md](../spec/simulation.md) "Tick model"
  - Demand `weight` scaling formula (1 for staples, 2-15 for luxuries) —
    [simulation.md](../spec/simulation.md) "Demand-tier" and the `dmnd`
    consumption-formula Open question
- **Spec now says**: ~~`SIM_HZ` "tune empirically"~~ **Resolved (Round 24)**
  — `SIM_HZ = 2`, now stated as a confirmed value, not a tuning knob. The
  remaining three (`R`'s second term, stock-arbitrage transfer rule, `weight`
  scaling/`dmnd` consumption formula) are each restated as **clean-room
  guesses at the mechanic's shape**, explicitly flagged as pending
  disassembly — not "individually minor simplifications" left as accepted
  deviations.
- **Status**: `SIM_HZ` resolved. `R`/stock-arbitrage/`weight` **Open** —
  bundled since each is individually minor and may compound with items 4/5
  (same price/demand formula neighborhood, `fcn.0046fcd0`/`fcn.00475120`).
  Tracked in `documentation/00-roadmap.md` Workstream I item 12.

### 13. Opponent AI (`opponent-ai.md`) — partially-decoded dispatcher, clean-room design

- **Where**: [opponent-ai.md](../spec/opponent-ai.md), entire document — see
  the new "OPEN RE GAP" framing at the top and "OPEN — `Player::Update`
  strategy dispatcher" in Open questions.
- **Spec now says**: `Player::Update` (`0x4a42f0`) and `Character::Update`
  (`0x40c190`) are **partially decoded (Round 30)** — this is further along
  than "never disassembled." Decoded: the merchant arrival/order-completion
  state machine, the `data.game.mnor=1000`-tick idle-merchant nag. Located
  but undecoded: the `+0xb0` "economic score vs. 99999.0 ->
  `fcn.4a5ad0`/`fcn.4a56f0`, then always `fcn.4a7170`+`fcn.4a72d0`" dispatcher
  — the prime suspect for "what to build/research/trade next." This is now a
  **specific, bounded gap** (named functions + fields), not "the whole
  subsystem is unknown."
- **Design decision (per explicit project direction)**: the clone exposes a
  per-AI-player **`ai_mode`** setting:
  - `ai_mode: classic` — faithful port of the decoded `Player::Update`
    dispatcher, once the remaining functions are decoded. Falls back to
    `modern` (visibly, in the UI) until then.
  - `ai_mode: modern` — the clean-room design in this document, kept as a
    **permanent, independently-developed alternative**, not a stopgap.
  Both share world/economy state and placement/order infrastructure; `ai_mode`
  only changes the decision logic. See opponent-ai.md "AI modes: Classic vs.
  Modern".
- **Status**: Open — full RE of `Player::Update`'s strategy dispatcher is the
  blocker for `ai_mode: classic`; `ai_mode: modern` proceeds independently.
  Tracked in `documentation/00-roadmap.md` Workstream I item 13.

---

## Resolved

_(none yet)_
