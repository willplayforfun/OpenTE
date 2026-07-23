# Spec Deviations — Where the Spec is Under-Specified

This document tracks places where the OpenTE spec (`OpenTE/spec/`) settles
for "good enough," "approximation," or "tune via playtesting" instead of
exactly replicating the original game's behavior. Each item describes what
the spec currently says, what information would be needed to close the gap,
and a resolution status.

The clone should match the original exactly **to start** — these hedges need
a deliberate decision (port more precisely, or consciously accept the
deviation with a recorded reason), not a default "sufficient" left
unexamined.

## How to use this doc

For each open item, decide one of:

- **Port more precisely** — nail down the original's actual algorithm/values
  and update the spec.
- **Accept deviation, document why** — the difference is genuinely
  immaterial and the spec should say so explicitly.
- **Defer with a concrete trigger** — acceptable for now, but record what
  would make it worth revisiting.

---

## Tier 1 — Gameplay rules with a known (or partially known) original algorithm

These are the highest-priority items: the original's behavior is partially
characterized, but the spec uses a looser stand-in.

### 1. Blocked-neighbor placement rule (additional diagonal checks)

- **Spec**: [input.md](../spec/input.md) step 5 of the placement-legality
  algorithm
- **What the spec says**: The "at most one footprint-edge tile adjacent to a
  category-2 trail/road tile" rule is a direct, confirmed port. However, the
  original has **two additional** diagonal-blocked-neighbor checks that make
  placement *stricter* than the spec's current rule. These additional checks
  use a second diagonal-blocked counter plus per-tile corner flags, with
  thresholds that haven't been arithmetically isolated.
- **What's needed**: The exact counter comparisons and threshold constants for
  the two additional rejection reasons. This would be a straightforward RE
  task (tracing two specific code paths).
- **Impact**: Some placements the clone allows would be rejected by the
  original. Visible during gameplay as "I could build here in OpenTE but not
  in the original" — a fidelity issue for tight map layouts.
- **Status**: **Open** — partially resolved (main rule ported), additional
  strictness pending.

### 2. Exclusion-radius shapes (building → shape mapping)

- **Spec**: [input.md](../spec/input.md) "Footprint 'shape' (exclusion
  radius)"
- **What the spec says**: The original's per-row exclusion-radius shape tables
  (diamond patterns with shape IDs 4/6/8/10) are concrete, recovered game
  data. The clone's `exclusion_shape` field is a **placeholder** because the
  mapping from building type to shape ID hasn't been recovered.
- **What's needed**: A complete `building_id` → `shape_id` mapping. The shape
  *data* exists; the missing piece is which building gets which shape.
- **Impact**: Without this, all buildings either share one exclusion shape or
  have none — mismatching the original's per-building exclusion zones.
- **Status**: **Open**.

### 3. Pathway drag-to-build segment tie-break

- **Spec**: [input.md](../spec/input.md) "Drag-path computation"
- **What the spec says**: The original builds a segment list (confirmed — not
  an A* search) from drag endpoints. The clone's two-segment "whichever L
  has fewer invalid tiles" heuristic is an **unverified stand-in** for the
  real tie-break rule (e.g., "always horizontal-first" or a more nuanced
  orientation rule).
- **What's needed**: The tie-break logic from the segment-shape helper
  functions. These are small helpers — straightforward to decode.
- **Impact**: Visible during pathway construction if the clone picks a
  different L-orientation than the original would.
- **Status**: **Open** — the clone's heuristic isn't contradicted by any
  finding, but isn't confirmed either.

### 4. Price-update formula inputs (`trend` / `N_i` / `RATIO`)

- **Spec**: [simulation.md](../spec/simulation.md) "Per-tick price update"
- **What the spec says**: The full `update_price()` formula is a direct port
  (sign-selection branch, constants, floor-snap — all confirmed). **Residual
  gap**: the formula takes three inputs whose *producers* are unidentified:
  - `trend` / `N_i` — a linked-list size that switches the formula between
    "normal tick" (random-walk + mean-reversion) and "decay toward BASE/2"
    modes. The clone defaults to `trend = 0` (always normal mode).
  - `RATIO`'s `arg` operand — passed alongside the tick counter.
- **What's needed**: Identification of what gameplay events
  populate/decrement `N_i` and what `arg` represents.
- **Impact**: Late-game economies may behave differently if `trend` should be
  nonzero in some conditions. The formula itself is correct; only its inputs
  are missing.
- **Status**: **Open** for the input producers.

### 5. Demand-tier formula inputs (`itra` / `ltra` / `mpri` / `popu`)

- **Spec**: [simulation.md](../spec/simulation.md) "Demand-tier
  (growth/decline) tracking"
- **What the spec says**: The demand-tier ratio formula
  (`compute_demand_ratio()` / `update_demand_tier()`) is a direct port. Four
  per-episode float fields (`itra`, `ltra`, `mpri`, `popu`) are decoded
  values with **no identified consumer** — they may be additional modifiers
  to the price/demand formulas that the clone is missing.
- **What's needed**: Identification of which game function reads these fields
  and how they influence the simulation. Even knowing which formula
  neighborhood they belong to would scope the work.
- **Impact**: `demand_tier` drives the growing/declining indicators shown to
  the player and feeds AI building-priority decisions. The formula is correct,
  but these orphan fields could represent a missing modifier.
- **Status**: **Open** — likely related to item 4's residual.

---

## Tier 2 — Placement/world rules where the spec designs around a gap

The original's full rules for these aren't recovered, so the spec substitutes
a clone-side design decision. Lower priority than Tier 1 (there's no known
algorithm being skipped), but still worth attention since some are core
"can I build here?" checks.

### 6. Capacity check denominator

- **Spec**: [input.md](../spec/input.md) step 6 of the placement-legality
  algorithm
- **What the spec says**: The numerator (a per-building `cost` field) is
  known and extracted. The denominator (a value returned by a virtual call on
  an unidentified runtime object) is **completely unresolved** — the capacity
  check literally cannot be ported yet.
- **What's needed**: Identification of the runtime object and what its
  virtual method returns (likely a max-capacity or budget value). Needs
  dynamic analysis (breakpoint in a save where the check fires).
- **Impact**: The clone currently skips this check entirely. This could allow
  placements the original would reject as over-capacity.
- **Status**: **Open**.

### 7. Special-type adjacency rules

- **Spec**: [input.md](../spec/input.md) step 8 of the placement-legality
  algorithm
- **What the spec says**: Every building has one of 4 categories (`bpro`,
  `bdem`, `bdep`, `bres`) — confirmed for all 153 buildings. **What each
  category's adjacency rule actually checks** is undecoded. The clone has
  no rule to populate the `requires_adjacent_type` field yet.
- **What's needed**: Decode each of the 4 category dispatch branches to
  understand what tile conditions they enforce. Two runtime-initialized
  globals also need identification (likely via dynamic analysis).
- **Impact**: A known placement-legality check from the original with no
  clone-side rule at all yet.
- **Status**: **Open**.

### 8. Terrain band → terrain-type mapping

- **Spec**: [world-and-maps.md](../spec/world-and-maps.md) terrain section
- **Resolved (water)**: Water vs land is now keyed off the `mapp.terr`
  texture page (low nibble) rather than the band: page 1 = `deep` →
  `DeepWater`, page 2 = `seas` → `ShallowWater`. This was cross-referenced
  against the `terr/sets` palette slot order (slot 1=deep, 2=seas) and
  altitude data (every page-2 tile sits at water level), and it agrees with
  the renderer's own `texture_index <= 2` water test. The extractor now emits
  `ShallowWater` (`tools/extractor/maps/region.py` `_terrain_type_for_value`).
  This also fixed the spurious map-edge coastline: band-shifted shallow-water
  bytes (e.g. `0x22`) were previously classified `Buildable`, so the shore
  overlay's `terrain_at` self-gate didn't skip them and drew coast against
  their seas neighbors.
- **Still open (land split)**: Among land pages (3-13), the buildable vs
  impassable split is still a coarse band placeholder — the top band
  (high_nibble=6, bytes 96-102) is treated as impassable, everything else
  buildable. This hasn't been cross-referenced against the placement code's
  buildability allowlist.
- **Impact**: A wrong land split misclassifies buildability for high-band
  tiles; water classification is now believed correct.
- **Status**: **Partially resolved** — water done; land buildable/impassable
  split still a guess.

### 9. Pre-existing trail connectivity at map load

- **Spec**: [world-and-maps.md](../spec/world-and-maps.md) trail connectivity
  section
- **What the spec says**: The original pre-populates connectivity from
  explicit per-tile `path`/`brid` arrays in the map data (map-editor-authored
  pre-built roads/trails/bridges with connectivity masks). Tiles with no
  `path`/`brid` record get defaults (land networks unbuildable until a player
  builds, deep water freely navigable). The map data format for these arrays
  (`{x, y, flags}` records) is decoded.
- **What the spec is still missing**: nothing — the spec's connectivity
  section was rewritten (2026-07-02) to the decoded ground truth and the
  implemented `connectivity` RLE-grid schema.
- **Impact**: none remaining for load-time data. The bridge *visual* (a
  separate sprite pass) is now implemented too — see
  [bridge-plan.md](bridge-plan.md) WP4/WP5. Construction auto-bridging
  (WP3/WP6) is still open.
- **Status**: **Resolved** — extractor emits the grid (`mapp.path`
  overwrite + `mapp.brid` bytes-4/5 overwrite, matching the EXE loader at
  `0x461c8e`/`0x461dad`), `Region` loads it, Stage-D renders from it.

---

## Tier 3 — Architectural decisions with unconfirmed fidelity

These are framed in the spec as architectural decisions, but the
justification is asserted rather than confirmed.

### 10. Pathfinding edge-cost model

- **Spec**: [entities.md](../spec/entities.md) "Pathfinding" → "Cost"
- **What the spec says**: The original's A* pathfinder uses uniform cost
  (1000/tile, network-agnostic) — network choice doesn't bias route search.
  The `movement_costs` table (from `epis.<ep>.path`) is real, decoded data,
  but **what consumes it in the original is unidentified**. The clone uses
  `movement_costs` as A* edge costs, which is framed as an unconfirmed
  stand-in.
- **What's needed**: Find what game system actually reads the movement-costs
  data. It could be UI cost estimates, AI route scoring, or per-network
  speed — not routing.
- **Impact**: If the original routes uniformly, the clone's cost-biased A*
  produces different trade routes — merchants would prefer existing roads in
  the clone but not in the original. This is a gameplay-visible difference.
- **Status**: **Open**.

### 11. Path-cache invalidation scope

- **Spec**: [entities.md](../spec/entities.md) "Path following" →
  "Implementation notes"
- **What the spec says**: Whether the original re-paths *every* merchant on
  any connectivity change, or only merchants whose route passes near the
  change, is unknown. The clone invalidates all cached paths on any change.
- **What's needed**: Find the invalidation call site near the pathway-commit
  code and check whether it iterates all merchants or uses a spatial filter.
- **Impact**: Could cause visible all-merchant pathing hitches the original
  may not exhibit. This is about player-visible feel, not just performance.
- **Status**: **Open**.

---

## Tier 4 — Tuning constants and formula simplifications

Minor coefficients where the simplification is small in scope. Included for
completeness but lower priority than Tiers 1-3.

### 12. Economy formula minor residuals

- **`R = 0.5` hardcode**: The price-update formula uses this as a flat
  constant. There may be a second additive/multiplicative term the clone is
  missing.
- **Stock-arbitrage transfer rule**: How the original balances stock between
  connected markets is unknown. The clone uses a placeholder "equal split."
- **Demand `weight` scaling**: How the per-commodity `weight` field
  (1 for staples, 2-15 for luxuries) factors into consumption accumulation
  is a clean-room guess.
- **What's needed**: All three are in the same code neighborhood as items
  4/5 and may share a root cause. Lowest priority — resolve after 4/5.
- **Status**: **Open** (except `SIM_HZ`, which is resolved as exactly 2).

### 13. Opponent AI decision logic

- **Spec**: [opponent-ai.md](../spec/opponent-ai.md), entire document
- **What the spec says**: The AI's main decision loop is partially decoded:
  the merchant arrival/order-completion state machine and a 1000-tick
  idle-merchant check are ported. The core "what to build/research/trade
  next" strategy dispatcher is located but undecoded — a specific, bounded
  gap (named functions and fields), not "the whole subsystem is unknown."
- **Design decision**: The clone exposes a per-AI-player `ai_mode` setting:
  - `ai_mode: classic` — faithful port, once the remaining functions are
    decoded. Falls back to `modern` until then.
  - `ai_mode: modern` — the clean-room design in
    [opponent-ai.md](../spec/opponent-ai.md), kept as a permanent,
    independently-developed alternative.
- **What's needed**: Full decode of the strategy dispatcher functions to
  make `ai_mode: classic` possible.
- **Impact**: `ai_mode: modern` proceeds independently regardless. This
  only blocks `classic` mode.
- **Status**: **Open**.

---

## Resolved

_(none yet — items will be moved here with their resolution rationale)_
