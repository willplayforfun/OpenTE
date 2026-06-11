# Implementation Roadmap

This document covers **implementation order, milestones, and
intermediate/temporary state** for OpenTE. It does not describe target
architecture or final behavior — that's the [`OpenTE/spec/`](../spec/)
directory. Update this doc as stages complete; it's the shared
session-to-session handoff for "what's next" on the OpenTE side (parallel
to `documentation/00-roadmap.md` for the RE side).

## Status

- **Phase 1 (toolchain spike) — DONE.** Verified end-to-end: extractor reads
  the user's Trade Empires install, extracts a sprite + the `comm` table to
  `game_data/`, and the SDL2 game loads `manifest.json`, renders the sprite,
  and responds to arrow keys/clicks/Escape. See
  `OpenTE/game/`, `OpenTE/tools/`, `.github/workflows/{ci,release}.yml`.
- **Phase 2 (full spec) — DONE.** [`OpenTE/spec/`](../spec/) covers
  rendering, audio, input, ui, data-model, world-and-maps, simulation,
  entities, opponent-ai, and modding.
- **Phase 3 (this doc) — IN PROGRESS.**
- **Phase 4 (multiplayer) — NOT STARTED, deliberately deferred.** A design
  sketch lives in [multiplayer-plans.md](multiplayer-plans.md), but
  multiplayer should only be addressed **after Stages 1-8 below are complete
  and the base single-player game is successfully cloned and playable**.
  Multiplayer adds a client/server split across most systems in the spec;
  building it against a still-changing single-player target would mean
  redoing that work repeatedly.

## Guiding principle: vertical slices

Each stage should produce a **playable (if minimal) end-to-end loop**, not a
horizontal layer (e.g. "all data loading" then "all rendering" then "all
sim"). This keeps every stage demoable and surfaces integration problems
(data schema gaps, performance, UX) early. Stages below are ordered so each
builds directly on the previous stage's vertical slice.

## Stage 1 — One region, static world, full extraction

**Goal**: walk around a real, fully-rendered map with no simulation yet.

- Extend the extractor (`tools/extractor/`) to:
  - Extract **all** `data.{}` tables per [data-model.md](../spec/data-model.md)
    (`commodities.json`, `buildings.json`, `transporters.json`, etc.) —
    most table extractors already exist as `documentation/scripts/te_*.py`
    and need porting (already done for `comm` in the spike; port `bldg`,
    `tran`, `band`, `guar`, `tech`/`epis.tech`, `abil`, `epis`, `even`).
  - Extract **one map** (`Maps/ep01 China.{}` is a good first target — it's
    the file already used to validate the `elem`/`mapp` decoders in
    `documentation/04-other-formats.md`) to
    `game_data/maps/ep01_china.json` per
    [world-and-maps.md](../spec/world-and-maps.md). Terrain-band ->
    terrain-type mapping can be a coarse placeholder (see spec Open
    questions) — a wrong-but-stable mapping is fine for this stage.
  - Extract building sprites for at least the buildings placed on that map
    (`bldg.{}`, per-culture-set lookup).
- Game side (`game/src/`):
  - `data::DataRegistry::load()` per [data-model.md](../spec/data-model.md)
    — typed structs + `from_json` for each table.
  - `world::World`/`Region` per [world-and-maps.md](../spec/world-and-maps.md),
    loading the one map.
  - Isometric rendering of terrain + decorations + placed buildings (static,
    no animation yet) per [rendering.md](../spec/rendering.md). Camera
    pan/zoom from [input.md](../spec/input.md).

**Exit criteria**: launch the game, see the China map's terrain and starting
buildings rendered isometrically, pan/zoom around it.

## Stage 2 — Building placement (no economy yet)

**Goal**: place buildings on the map with full legality checking.

- Implement [input.md](../spec/input.md)'s placement-legality checks
  (`PlacementError` enum) against Stage 1's static world — terrain check can
  initially be a stub (`return Ok` for any non-water tile) since
  `fcn.4640c0`'s internals weren't decoded; tighten later.
- Implement the placement-preview highlight overlay
  ([rendering.md](../spec/rendering.md)).
- Implement a minimal build menu ([ui.md](../spec/ui.md)) listing all
  buildings from `buildings.json` (no tech-gating yet — that's Stage 5).
- Placing a building spawns a static entity (no production/AI yet) and
  deducts a flat placeholder cost from a placeholder treasury value.

**Exit criteria**: open the build menu, hover the map to see
valid/invalid (blue/red) tile highlights update live, click to place a
building, see it deduct cost from a visible treasury number.

## Stage 3 — Pathways & connectivity

**Goal**: drag out roads/trails and see the connectivity grid update.

- Implement the connectivity grid + "can network N step here" rule
  ([world-and-maps.md](../spec/world-and-maps.md)).
- Implement pathway drag-out UX ([input.md](../spec/input.md)) with the
  simple L-path approach first.
- Visual: road/trail tiles render with a connection-dependent sprite
  variant (placeholder: even a simple "this tile has a road" flat overlay
  is enough to validate the connectivity logic before real road sprites are
  extracted).

**Exit criteria**: drag a road between two points, see it commit, see
adjacent tiles' connectivity update (verifiable via a debug overlay showing
each tile's 6 connectivity bytes, even if the sprite doesn't visually change
yet).

## Stage 4 — Economy simulation (single market)

**Goal**: prices drift over time per [simulation.md](../spec/simulation.md).

- Implement the fixed-tick simulation loop ([overview.md](../spec/overview.md)).
- Implement `MarketGood`/price-update/period-rollover/demand-tier per
  [simulation.md](../spec/simulation.md), with placeholder config constants
  (`PSCA`/`PSCT`/`PSFA`/`PSFT`/`grow`/`decl` — pick starting values, see
  spec Open questions, **document chosen values and the date they were last
  tuned** in `OpenTE/implementation/tuning-log.md` so balance changes are
  traceable).
- Implement production buildings' production-cycle logic.
- UI: market window ([ui.md](../spec/ui.md)) showing live prices/stock for
  one market.

**Exit criteria**: place a production building, watch its output
commodity's price/stock change over (accelerated) time in the market
window; verify the "snap back to base price" and demand-tier
growing/declining notifications fire under contrived conditions (e.g. a
debug command to force a large stock imbalance).

## Stage 5 — Merchants, transporters, pathfinding

**Goal**: a merchant moves goods between two markets along built pathways.

- Implement A* pathfinding over the connectivity grid
  ([entities.md](../spec/entities.md)).
- Implement `MerchantState`/`TradeOrder`/cargo, manually-issued orders first
  (player clicks a merchant, picks "go here, buy/sell X" — a debug/manual
  UI is fine before AI exists).
- Animation state machine ([rendering.md](../spec/rendering.md)) for
  walking/idle merchant sprites.

**Exit criteria**: manually order a merchant to travel to another market,
buy a commodity, return, sell it — observe the home market's stock/price
react.

## Stage 6 — Technology, episodes, events

- Tech research UI + unlock-gating of build menu
  ([simulation.md](../spec/simulation.md), [ui.md](../spec/ui.md)).
- Episode definition loading (`episodes.json`), episode-end condition.
- A small set of scripted/random events with real `EventEffect`
  implementations (start with 2-3, e.g. a price-shock event and a
  population-growth event — easy to validate, exercises the
  `EventEffectRegistry` extension point from
  [modding.md](../spec/modding.md)).

## Stage 7 — Opponent AI

- Stages 1-3 of [opponent-ai.md](../spec/opponent-ai.md) (AI players exist
  -> AI merchant trade loops -> AI strategic building/research decisions).
- This is the largest "clean design, no RE reference" stage — expect
  significant iteration/tuning. Track AI tuning constants in the same
  `tuning-log.md` as Stage 4's economy constants.

## Stage 8 — Save/load, audio, polish

- Save format ([world-and-maps.md](../spec/world-and-maps.md)).
- Audio extraction + playback ([audio.md](../spec/audio.md)).
- UI sprite extraction (`a_ui`/`d_ui`/`m_ui`) replacing flat-color
  placeholders ([ui.md](../spec/ui.md)).
- Sprite atlas packing for performance ([rendering.md](../spec/rendering.md)).
- Multi-region/multi-episode support (Stage 1 was single-region).

## Temporary/placeholder inventory

Track every "this is a stand-in, replace later" decision here so it doesn't
get lost. Update this list as placeholders are resolved (move resolved
items to a "Resolved" section with the stage that fixed them, or just
delete them — git history retains the record).

| Placeholder | Introduced in | Real implementation tracked by |
|---|---|---|
| Terrain band -> terrain-type mapping is coarse/approximate | Stage 1 | [world-and-maps.md](../spec/world-and-maps.md) Open questions |
| `fcn.4640c0` terrain-buildability check stubbed to "non-water" | Stage 2 | [input.md](../spec/input.md) Open questions |
| Flat placeholder building/pathway costs, no real treasury rules | Stage 2-3 | [simulation.md](../spec/simulation.md) |
| Road/trail tiles use flat debug overlay, not real connection-sprite variants | Stage 3 | [rendering.md](../spec/rendering.md) |
| Economy tuning constants (`PSCA` etc.) are first-guess values | Stage 4 | `tuning-log.md` (create when Stage 4 starts) |
| Merchant orders issued manually, no AI | Stage 5 | [opponent-ai.md](../spec/opponent-ai.md) |
| Starting buildings/resources/merchants for a region: extract real `enti` table from a matching save (`scripts/te_save.py`) where available; otherwise scatter `bres` per `epis.<ep>.regi.<id>.grou` quotas on non-water tiles | Stage 1 | `documentation/09-episode-population.md` (workstream H, in progress) |
| UI is flat-color placeholders; UI sprites (`a_ui`/`d_ui`/`m_ui`) are extracted but not wired into widgets | All UI stages | [ui.md](../spec/ui.md) Open questions (T0.4, extraction done, wiring pending) |
| Single-region only | Stages 1-7 | Stage 8 |
| Terrain is rendered flat (no per-tile elevation); edge skirts (`terrain.edge`) only drawn along the map's south/east border, not at internal height-difference edges | Stage 1 | `documentation/08-investigation-needed.md` B15 |

**Note (2026-06-11, see `documentation/08-investigation-needed.md` Tier 0):**
Stage 1's map extraction and Stage 4's economy/production placeholders can
now use real decoded data instead of guesses: `episodes.json.demand`
(per-building commodity demand), `episodes.json.movement_costs`
(per-pathway-type movement costs), and `transporters.json.by_episode.<ep>`
(per-episode transporter speed/capacity/network-access) are all resolved —
see [data-model.md](../spec/data-model.md)'s Table catalog. Terrain-band ->
terrain-type mapping (B7) and the economy tuning constants `PSCA`/etc (B2)
remain open and still need placeholder values for Stages 1/4.

## Cross-reference

For RE ground-truth backing any of the above (economy formula derivation,
placement-legality decode, connectivity mask semantics, etc.), see
`documentation/03-exe-analysis.md` Rounds 4-18 and
`documentation/04-other-formats.md`. The spec docs in `OpenTE/spec/`
already cite the relevant rounds inline — use those citations rather than
re-deriving from the raw RE notes.
