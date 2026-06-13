# OpenTE Spec — Overview

This directory specifies **OpenTE**, a from-scratch, open-source clone of
*Trade Empires* (2001). It describes how the *clone* should be built — its
architecture, data formats, algorithms, and UX — using modern, maintainable
approaches. It is **not** a description of the original game's internals.

Where a design decision is directly informed by the original game's
behavior (e.g. an economy formula players expect to feel familiar, or a
building-placement rule), this spec states the *behavior* to replicate and,
where useful, notes that the behavior is confirmed by RE analysis. It
does not describe how the original
*implements* that behavior unless the implementation detail itself is worth
reusing.

## Document set

| File | Covers |
|---|---|
| [data-model.md](data-model.md) | Extractor output format, JSON table schemas, ID conventions, manifest/versioning, asset directory layout |
| [world-and-maps.md](world-and-maps.md) | Map file format, terrain grid, tile coordinates, regions, placed objects, pathway network model |
| [rendering.md](rendering.md) | Isometric projection, sprite atlases, camera, draw order, tile highlight overlays, animation playback |
| [simulation.md](simulation.md) | Tick/update loop, economy (price/stock) simulation, production, tech progression, episodes/events |
| [entities.md](entities.md) | Entity/component model, entity types, animation state machine, pathfinding |
| [input.md](input.md) | Event handling, camera controls, tile picking, building placement & pathway drag-out UX |
| [ui.md](ui.md) | Widget framework, HUD, dialogs (market, build menu, etc.) |
| [audio.md](audio.md) | Sound effects, music, sound-cue triggers |
| [opponent-ai.md](opponent-ai.md) | AI player behavior (merchants, building/economy decisions) |
| [modding.md](modding.md) | Data-driven modding, overlay directories, schema index |

Implementation order, milestones, and temporary/placeholder notes live in
[`OpenTE/implementation/`](../implementation/), not in this spec.

## Tech stack (from the toolchain spike)

- **Language**: C++20
- **Build**: CMake + CMakePresets.json, dependencies via vcpkg manifest mode
- **Rendering/input/windowing**: SDL2 + SDL2_image
- **Data**: JSON (nlohmann::json) for all extracted tables and the manifest
- **Extractor**: Python 3 (stdlib only for the core logic; PyInstaller for
  the distributed standalone executable)

## High-level architecture

```
                 +-------------------+
 Trade Empires   |   extractor (py)  |   game_data/   +------------------+
 (user's copy) ->|  containers/      |--------------->|  game (C++/SDL2) |
                 |  sprites/ tables/ |  manifest.json  |  core/render/    |
                 +-------------------+  sprites/*.png  |  world/sim/...   |
                                          tables/*.json +------------------+
```

The extractor is the **only** component that reads original Trade Empires
files. It never bundles or redistributes copyrighted assets — it runs
locally against the user's own legally-owned copy and writes a `game_data/`
directory next to (or specified relative to) the original install. The game
never reads original game files directly; it only consumes `game_data/`.

This split means:
- OpenTE's source code and releases contain zero original assets/data.
- The `game_data/` format (documented in [data-model.md](data-model.md)) is
  the single contract between the two halves, and is versioned via
  `manifest.json`'s `format_version`.
- Modding happens entirely on the `game_data/` side (see
  [modding.md](modding.md)) — the game never needs to know whether a file
  came from the extractor or a mod overlay.

## Engine main loop (clone design)

A conventional fixed-timestep simulation loop, decoupled from rendering:

```cpp
const double SIM_HZ = 20.0;            // simulation ticks per second
const double SIM_DT = 1.0 / SIM_HZ;

double accumulator = 0.0;
auto last = clock::now();
while (running) {
    poll_events();                     // input.md
    auto now = clock::now();
    accumulator += seconds(now - last);
    last = now;

    while (accumulator >= SIM_DT) {
        simulation.tick();              // simulation.md / entities.md
        accumulator -= SIM_DT;
    }

    double alpha = accumulator / SIM_DT; // for interpolated rendering
    renderer.render(world, alpha);      // rendering.md
}
```

The original ran its simulation at a fixed per-tick rate; the
clone reproduces "one simulation tick = one unit of game-rule time" (prices
drift per tick, animation scripts schedule re-entry by tick count, etc.) but
decouples it from frame rate using the accumulator pattern above. The exact
tick rate (ticks/second) is an open tuning question — see
[simulation.md](simulation.md) "Open questions".

## ID and naming conventions

The original format identifies almost everything by 4-letter lowercase
codes (`comm.brsw` = Bronze Sword, `bldg.mill`, `tran.cara`, etc.), often
namespaced by table and/or episode (`epis.<ep>.comm.<id>`). OpenTE keeps
these IDs as **opaque string identifiers** throughout the data model and
code — never reuse array indices as identity. This is what makes the
data-driven modding strategy in [modding.md](modding.md) viable: a mod can
add `comm.zzzz` without renumbering anything.

## Glossary

- **Episode** — a scenario/campaign chapter (`epis.<NN>`); each episode has
  its own overrides for commodities, buildings, tech availability, etc.
  layered on top of the global tables.
- **4cc** — a 4-character code used as a table/field/record tag throughout
  the original format (e.g. `comm`, `bldg`, `base`, `loca`).
- **Footprint** — a building's tile-space width/height (`futx`/`futy` in the
  original), used for placement and occupancy checks.
- **Pathway network** — one of: None, Trail, Road, Rail, Canal, Deep (water)
  — see [world-and-maps.md](world-and-maps.md) and [entities.md](entities.md).
