# Modding

This document specifies OpenTE's modding strategy: how mods are structured,
discovered, loaded, and how they interact with the legally-required
extractor split (see [overview.md](overview.md)). The goal — per
`documentation/07-clone-architecture.md` — is for modders to add or change
commodities, buildings, recipes, tech trees, maps, and assets **by editing
data files and dropping in replacement assets, without recompiling the
game**.

## Principles

1. **Everything is data.** Game logic reads `Commodity`/`Building`/etc.
   structs and never special-cases a specific ID in code. If a rule needs
   to vary per building (e.g. "this building requires water adjacency"),
   it's a field in `buildings.json`, not an `if (id == "bldg.eur1.port")`
   in C++. (See [data-model.md](data-model.md) for what's already
   data-driven; if implementing a new feature requires hardcoding an ID,
   that's a signal the schema needs a new field instead.)
2. **IDs, never indices.** All cross-references are string IDs (see
   [data-model.md](data-model.md)). A mod can add `comm.zzzz` or
   `bldg.eur1.newmill` without touching existing IDs.
3. **Overlay, don't replace.** Mods are directories with the *same shape*
   as `game_data/`, merged on top at load time — a mod that only wants to
   change one commodity's price ships a `tables/commodities.json`
   containing just that one commodity's overridden fields, not a full copy
   of the table.
4. **No copyrighted assets in mods distributed via OpenTE's own channels.**
   Mods that modify *data* (prices, recipes, balance, new maps using the
   clone's own map format) are freely shareable. Mods that replace
   *extracted original assets* (sprites/audio) are the mod author's
   responsibility re: licensing — OpenTE's release channel should not host
   such mods, but the loader doesn't need to police this technically.

## Mod directory structure

```
mods/
  my_mod/
    mod.json                # metadata: name, version, author, load priority
    tables/
      commodities.json       # partial overrides, merged by ID + field
    sprites/
      bldg.eur1.newmill.png  # new sprite, or replacement for an existing ID
    maps/
      my_custom_map.json      # new map, additive (no ID collision possible
                               # in practice since map IDs are filenames)
```

`mod.json`:

```json
{
  "id": "my_mod",
  "name": "My Mod",
  "description": "Doubles the price of bronze swords.",
  "version": "1.0.0",
  "load_priority": 100
}
```

`name` and `description` are short, player-facing strings shown in the
mod-selection dialog ([ui.md](ui.md#startup-flow--dialogs)) — keep
`description` to a sentence or two, it's not a changelog.

## Mod selection & `mods_enabled.json`

At game startup, the [mod-selection dialog](ui.md#startup-flow--dialogs)
lists every subdirectory of `mods/` with a `mod.json` and lets the player
check/uncheck each one. The result is written to `mods_enabled.json`, next
to `game_data/`:

```json
[
  { "id": "my_mod", "enabled": true },
  { "id": "another_mod", "enabled": false }
]
```

- Array order is the **load order** (ascending — later entries override
  earlier ones and `game_data/` itself), seeded from `mod.json`'s
  `load_priority` the first time a mod is seen and preserved thereafter.
  Reordering mods (e.g. drag-to-reorder in a future mod-manager UI) is just
  reordering this array — out of scope for the initial dialog, which only
  toggles `enabled`.
- A mod present in `mods/` but absent from `mods_enabled.json` is appended
  (disabled by default) the next time the dialog runs; a mod listed in
  `mods_enabled.json` but no longer present in `mods/` is dropped silently.

## Loading order

`DataRegistry::load` (see [data-model.md](data-model.md)) takes an ordered
list of search paths built from `mods_enabled.json`:

```
[ game_data/, mods/<mod-a>/, mods/<mod-b>/, ... ]
```

— `game_data/` first, then every `enabled: true` entry from
`mods_enabled.json` in array order. For each table, later paths' entries are
merged **by ID, field-by-field** into earlier ones — a mod overriding only
`base_price` for `comm.brsw` doesn't need to restate `flags`/`name`. For
sprites, later paths simply win (filename match, no merging).

**New IDs** (a mod adding `comm.zzzz`) are appended to the merged table,
no different from an override — the merge is "upsert by ID" either way.

## Validation

After all overlays are merged, run [data-model.md](data-model.md)'s
fail-fast cross-reference validation pass over the **merged** result. A mod
that references a nonexistent ID (e.g. a new building whose recipe
references a commodity the mod forgot to add) fails to load with a clear
error naming the mod and the broken reference — this is far friendlier than
a crash mid-game.

## What a mod can change, by spec section

| Mod wants to... | Edit |
|---|---|
| Change a commodity's price/flags | `tables/commodities.json` (data-model.md) |
| Add/change a building, recipe, footprint | `tables/buildings.json` + `tables/episodes.json` (data-model.md) |
| Tune the economy formula | `config.json`'s `economy` block (simulation.md) |
| Add a new transporter | `tables/transporters.json` (data-model.md) — define `depot_class`/`speed_class`/network access (entities.md Open questions) |
| Add a new map/scenario | `maps/<id>.json` (world-and-maps.md) |
| Replace a sprite | `sprites/<id>.png`, same dimensions not required (atlas repacks) |
| Add a new event type | `tables/events.json` + register an `EventEffect` — **requires a code-side registration point**, see below |
| Add a new building-placement rule | Generally **not** possible without code changes — placement rules ([input.md](input.md)) are a fixed set of `PlacementError` checks. A mod can disable a check via a building-level flag if one is exposed, but can't add wholly new check types without recompiling. |

### Code-extension points

A few systems are inherently code (not pure data) but should expose a
**registration point** so mods that *do* ship native code (a future,
lower-priority "scripted mod" capability) or that the base game itself uses
to add content don't require touching core loops:

- **Event effects** ([simulation.md](simulation.md)): `EventEffect`
  implementations registered by `event_id` in a small registry —
  `EventEffectRegistry::register("blight", std::make_unique<BlightEffect>())`.
  New event types require a new `EventEffect` subclass (C++), but the
  *trigger conditions/probabilities* are data (`tables/events.json`).
- **AI building-priority heuristics** ([opponent-ai.md](opponent-ai.md)):
  the priority list itself is data; the scoring function is code. Sufficient
  for data-only mods to retune AI without code changes.

A full scripting layer (Lua/etc.) for mods is **out of scope** for the
initial spec — if demand emerges, it would sit alongside (not replace) this
data-overlay system, likely hooking the same extension points.

## Versioning & compatibility

- A mod targets a specific `format_version` (data-model.md). The loader
  warns (doesn't necessarily refuse) if a mod's declared `format_version`
  doesn't match the running game's, since most field-level overrides remain
  valid across minor schema additions — refuse only on major/breaking
  format changes (the same `format_version` integer used for
  `game_data/manifest.json`).
- No mod-dependency resolution system for the spike (a mod can't declare
  "requires mod X") — if this becomes necessary, it's an additive field in
  `mod.json`, not a redesign.

## Open questions

- **Mod manager reordering UI**: the startup dialog
  ([ui.md](ui.md#startup-flow--dialogs)) only toggles `enabled` checkboxes;
  drag-to-reorder (changing relative `load_priority`/load order) is not yet
  specified and would need its own widget — out of scope until a mod
  actually needs to be reordered relative to another.
- **Sprite atlas repacking cost** for large mods (many new sprites): the
  [rendering.md](rendering.md) atlas cache is keyed by a hash of
  `game_data/` + enabled mods' contents — invalidate and repack on any
  change. Acceptable for the spike; revisit only if repack time becomes
  noticeable with real mod content.
