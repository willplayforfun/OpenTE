# Data Model

This document specifies the **`game_data/` directory** — the contract
between the extractor (`OpenTE/tools/extractor`) and the game
(`OpenTE/game`). It covers the manifest format, JSON table schemas, sprite
asset conventions, ID/reference conventions, and the loader design on the
C++ side. This is also the **modder's primary reference**: every file here
is plain JSON/PNG that a mod overlay can replace or extend (see
[modding.md](modding.md)).

## Directory layout

```
game_data/
  manifest.json
  sprites/
    <sprite-id>.png            # one PNG per extracted sprite (RGBA8888)
  tables/
    commodities.json
    buildings.json
    transporters.json
    bandits.json
    guards.json
    technologies.json
    abilities.json
    episodes.json
    events.json
    ... one file per top-level table (see "Table catalog" below)
  maps/
    <map-id>.json               # see world-and-maps.md
```

All paths inside JSON files are **relative to `game_data/`** (e.g.
`"sprite": "sprites/bldg.cher.cbaz.png"`), so the whole directory can be
relocated or layered with mod overlays without rewriting paths.

## Launch configuration: locating `game_data/`

The game looks for `game_data/` in this order:

1. **`OpenTE.ini`**, next to the game executable, if present:
   ```ini
   [paths]
   game_data = D:\Games\TradeEmpiresClone\game_data
   ```
   A relative path is resolved relative to the executable's directory.
2. Otherwise, `./game_data` next to the executable (the extractor's default
   output location).

A candidate directory is **valid** if it contains a `manifest.json` whose
`format_version` matches `GAME_DATA_FORMAT_VERSION` (the version the running
binary was built for). If neither candidate is valid, the game shows the
"`game_data/` not found" dialog described in
[ui.md](ui.md#startup-flow--dialogs), which lets the player pick a
directory or exit. A directory picked this way is validated the same way
and, if valid, written back to `OpenTE.ini` (creating the file if it doesn't
exist) so subsequent launches skip the dialog.

`OpenTE.ini` is plain INI (one `[paths]` section, one `game_data` key for
the spike) — no need for a full config system; if more launch-time settings
emerge later, add keys/sections to the same file rather than introducing a
second config file.

## `manifest.json`

```json
{
  "format_version": 1,
  "generated_by": "opente-extract 0.1.0",
  "source_game_version": "...",
  "sprites": [
    { "id": "bldg.cher.cbaz", "path": "sprites/bldg.cher.cbaz.png", "width": 121, "height": 78 }
  ],
  "tables": [
    { "id": "commodities", "path": "tables/commodities.json" }
  ]
}
```

- **`format_version`** — an integer, bumped on any breaking change to the
  `game_data/` schema. The game checks this at startup against the schema
  version it was built for and refuses to load (with a clear error message
  prompting a re-extraction) on mismatch. Loader code should be a single
  `assert manifest.format_version == GAME_DATA_FORMAT_VERSION` near the top
  of data loading — no silent fallback/migration logic for the spike; add a
  migration path only once there's a real installed base to support.
- **`sprites`** / **`tables`** are *indexes* — they let the game discover
  what's available without hardcoding filenames, and let a mod overlay add
  brand-new sprites/tables (e.g. a new commodity icon) that the base game
  doesn't know about by ID but generic systems (inventory icons, etc.) can
  still enumerate.
- The manifest does **not** need to enumerate every map; maps are discovered
  by scanning `maps/*.json` (see [world-and-maps.md](world-and-maps.md)).

## ID conventions

- IDs are lowercase strings using the original 4-letter codes, optionally
  dotted for namespacing: `comm.brsw`, `bldg.eur1.mill`, `epis.ep03.tech.pric`.
- IDs are **never** array indices. Tables are JSON objects keyed by ID
  (`{"brsw": {...}, "ambe": {...}}`), not arrays — this lets a mod add a new
  key without disturbing existing ones and makes "does X exist" an O(1)
  lookup.
- Cross-references between tables (e.g. a building's recipe input
  commodities) are stored as ID strings, resolved lazily by the loader (see
  "Loader design" below). A reference to a nonexistent ID is a load-time
  error (fail fast), except where the schema explicitly allows `null`/empty
  for "none" (e.g. `tech: null` = "no advance required").

## Table catalog

Each table below corresponds to one root table in the original
`Data/data.{}` (see `documentation/02-data-catalog.md` for the
reverse-engineered source). Field names are kept close to the originals
(also 4-letter codes) since they're already the modding vocabulary, but
given descriptive JSON keys where the 4-letter code would be opaque.

### `tables/commodities.json` (from `comm`, ~136 entries)

```json
{
  "brsw": {
    "name": "Bronze Sword",
    "base_price": 648,
    "flags": { "food": false, "luxury": false, "military": true, "medicine": false }
  }
}
```

- `base_price` — the reference price used by the economy simulation's
  "snap back to base" behavior (see [simulation.md](simulation.md)).
- `flags` — drive victory-condition and AI-demand logic (a "military"
  commodity matters for `band`/`guar` equipping, etc.).
- Per-episode production recipes, obsolescence (`supe`/`infe`/`remo`), and
  demand are **not** in this table — they live in `episodes.json` under
  `epis.<ep>.comm.<id>` (see below), since they vary per episode/era.

### `tables/buildings.json` (from `bldg`, organized by culture/era set)

```json
{
  "eur1.mill": {
    "name": "Windmill",
    "culture_set": "eur1",
    "build_cost": 1200,
    "footprint": { "width": 2, "height": 2 },
    "category": "production",
    "type": "fmar",
    "military": false,
    "religion": false,
    "terrain": { "requires_dirt": false, "requires_water_adjacent": false },
    "sprites": { "default": "bldg.eur1.mill.cbaz" }
  }
}
```

- `footprint` corresponds to the original `futx`/`futy` and drives both
  placement legality and rendering (see [world-and-maps.md](world-and-maps.md)
  and [input.md](input.md)).
- `category`/`type`/`military`/`religion`/`terrain` come from the `defa`
  set's shared field vocabulary (`cate`/`type`/`mili`/`reli`/`dirt`).
- Production rates/recipes are **not** here — see `episodes.json`'s
  `epis.<ep>.comm.<id>.inputs`/`.outputs`, keyed by the commodity produced,
  cross-referenced to the producing building via the commodity's
  `produced_by` field (clone-side denormalization computed by the
  extractor for convenience — see "Derived/denormalized data" below).

### `tables/transporters.json` (from `tran`, 33 entries)

```json
{
  "hors": {
    "name": "Horse",
    "depot_class": "cara",
    "is_boat": false,
    "by_episode": {
      "ep00": {
        "capacity": 3,
        "cost": 150,
        "path_type": "none",
        "tech": "tcvn",
        "speed": { "none": 12, "road": 25, "trai": 20, "rail": 0, "cana": 0, "deep": 0, "dese": 0 }
      },
      "ep01": {
        "capacity": 3,
        "cost": 150,
        "path_type": "none",
        "tech": "tcvn",
        "speed": { "none": 18, "road": 32, "trai": 26, "rail": 0, "cana": 0, "deep": 0, "dese": 0 }
      }
    }
  }
}
```

This was previously a placeholder (`speed_class` -> "small constants table,
clone picks its own numbers") and is now **resolved** — see
`documentation/08-investigation-needed.md` T0.1.
`epis.<ep>.tran` (`documentation/extracted/data_catalog_episode.md`, via
`scripts/te_episode.py`) gives, for every transporter, exact `capa` (cargo
capacity), `cost` (purchase price), `path` (network type the transporter
itself needs to be based on, e.g. `cara`/`dock`/`port`), `tech` (advance that
unlocks it), and a **per-network-type speed map**
(`none`/`road`/`trai`/`rail`/`cana`/`deep`/`dese`). A speed of `0` for a
network means "this transporter cannot use that network at all" — this also
directly answers `entities.md`'s "per-transporter network access" question
(e.g. `kele`/`quff`/`reed` in `epis.ep03` only have nonzero `cana` speed;
`whee`/`cart` only have nonzero `road`).

Values vary **per episode** (e.g. Horse's `road` speed is 25 in `ep00` but 32
in `ep01`/`ep02`), so `by_episode` is a per-episode map, like
`episodes.json`, not a flat global table — the extractor populates it
directly from `epis.<ep>.tran`. `depot_class` (`cara`/`barg`/`ship`/`engi`,
from the root `tran` table) stays a flat top-level field since it's
episode-invariant. The transporter chart (`Workstream E item 1`) is now
optional/low-priority — it would only add flavor-text corroboration, not new
gameplay numbers.

### `tables/bandits.json` (from `band`, 18 entries) and `tables/guards.json` (from `guar`, 17 entries)

```json
{
  "atta": 0, "defe": 0, "forc": 0, "rang": 0, "spee": 0, "amphibious": false,
  "name": "...", "desc": "..."
}
```

`guards.json` entries additionally have `produced_by_building` and
`required_weapon` (commodity ID, e.g. `irsw`).

### `tables/technologies.json` (joins `tech.<ep>.<id>` + `epis.<ep>.tech.<id>`)

```json
{
  "ep03.pric": {
    "name": "...",
    "flavor_text": "...",
    "episode": "ep03",
    "cost": 1200,
    "available_at_tick": 600,
    "unlocks": { "buildings": [], "transporters": [], "pathways": [] }
  }
}
```

`excl` (confirmed values: `0` or `600` — the catalog's earlier "0/1" note was
stale, see `documentation/08-investigation-needed.md` T0.6) is **not**
carried forward as-is — its purpose is still unconfirmed (Tier 1 item B8:
`0` consistently appears on the small set of "free at start" advances, `600`
on the rest, but at least one counter-example (`tinc`, `pric=0`) means it's
not simply "this advance costs money"). The extractor should pass it through
as an opaque `extra.excl` field until B8 confirms its meaning, rather than
mapping it to a named field that might be wrong.

### `tables/abilities.json` (from `abil`, 13 entries, indices 0-12)

```json
[
  { "id": 0, "name": "Pathfinding", "desc": "...", "bonus": 0, "rank": 0 }
]
```

This is the **one table that's an array, not an object** — the original
indexes it by small integer (`epis.<ep>.merc.<i>.abil`), and there's no
4-letter code to use as a key. Merchant roster entries reference abilities
by this integer index.

### `tables/episodes.json` (from `epis`, ~20 entries)

The largest/most structurally complex table. Per episode:

```json
{
  "ep03": {
    "name": "...",
    "regions": [ ... ],
    "commodities": {
      "brsw": {
        "inputs": [ { "commodity": "irsw", "amount": 1 } ],
        "outputs": [ { "commodity": "brsw", "amount": 1 } ],
        "produced_by_building": "eur1.weap",
        "obsolescence": { "superseded_by": null, "inferior_to": null, "removed": false }
      }
    },
    "merchants": [
      { "name": "...", "hire_cost": 0, "ability": 4 }
    ],
    "technologies": [ "ep03.pric" ],
    "demand": {
      "dwel": { "rice": { "weight": 1, "amount": 4 }, "slkc": { "weight": 1, "amount": 6 } }
    },
    "movement_costs": {
      "trai": { "none": 2, "road": 0, "trai": 0, "rail": 0, "cana": 50, "wate": 50 },
      "road": { "none": 5, "road": 0, "trai": 3, "rail": 0, "cana": 50, "wate": 50 },
      "rail": { "none": 0, "road": 0, "trai": 0, "rail": 0, "cana": 0, "wate": 0 },
      "cana": { "none": 20, "road": 50, "trai": 50, "rail": 0, "cana": 0, "wate": 0 }
    },
    "economy": {
      "tick": 25,
      "growth_threshold": 1.149999976158142,
      "decline_threshold": 0.699999988079071,
      "initial_trade_ratio": 0.20000000298023224,
      "last_trade_ratio": 0.30000001192092896,
      "min_price_ratio": 0.25,
      "population_growth_rate": 0.20000000298023224
    }
  }
}
```

- **`economy`** (new fields on `epis.<ep>`, `0x48`/float unless noted) —
  resolved (Round 20, see `documentation/03-exe-analysis.md`):
  `growth_threshold`/`decline_threshold` (original `grow`/`decl`) feed
  [simulation.md](simulation.md)'s demand-tier tracking and vary per
  episode (most are `~1.15`/`~0.7-0.8`; `ep00`/`ep01` are more lenient at
  `1.25`/`0.6`). `tick` (int) is the episode length in ticks. `
  initial_trade_ratio`/`last_trade_ratio`/`min_price_ratio` (original
  `itra`/`ltra`/`mpri`) and `population_growth_rate` (original `popu`) are
  further per-episode economy knobs whose exact usage in the simulation
  formula isn't traced yet — `ep00`/`ep01` again stand out (`itra=ltra=0.75`,
  `mpri=0.1` vs `~0.2-0.4`/`~0.2-0.25` for later episodes). Carried through
  verbatim for now; revisit if early-episode economy feel needs tuning.

- `obsolescence` corresponds to the original `supe`/`infe`/`remo` fields
  (per `documentation/01-container-format.md` gotcha 11 / the embedded
  player FAQ: "no one is going to pay for plain ceramics once dyed have been
  invented"). The simulation uses this to phase out demand for superseded
  goods — see [simulation.md](simulation.md).
- **`demand`** (original `epis.<ep>.dmnd`) is **resolved** (see
  `documentation/08-investigation-needed.md` T0.2): it's a
  `{building_type: {commodity: {weight, amount}}}` map — for each building
  type (e.g. `dwel`, `temp`, `zigg`), the set of commodities its
  population/operation demands per tick, each with an `amount` (per-tick
  consumption quantity, e.g. `dwel.rice.amount=4`) and a `weight` (1 for
  staples, 2-15 for luxuries — exact scaling formula not yet confirmed, but
  the shape is solid). See `extracted/data_catalog_episode.md` "Building
  commodity demand (`dmnd`)". This directly drives
  [simulation.md](simulation.md)'s production/consumption model — no more
  "raw bytes" placeholder.
- **`movement_costs`** (original `epis.<ep>.path`, renamed from
  `trade_paths` — it is **not** trade routes) is **resolved** (T0.3): a
  per-pathway-type movement-cost matrix. Each key (`trai`/`road`/`rail`/
  `cana`) is a pathway/network type a transporter can use; its value gives
  the cost of traversing a tile carrying that network type, indexed by
  *which* underlying network is present on the tile
  (`none`/`road`/`trai`/`rail`/`cana`/`wate`). E.g. `trai.none=2` (crossing
  open ground costs 2 for a Trail-network transporter) but `trai.road=0`
  (free if a road is already there); `cana.none=20`, `cana.road=50` (canal
  transporters pay heavily off-canal). See
  [world-and-maps.md](world-and-maps.md)/[entities.md](entities.md) for how
  this interacts with the original's separately-decoded (and uniform-cost)
  A* `DefaultCostCalculator`.

### `tables/events.json` (from `even`)

```json
{ "ambassadorial_visit": { "name": "Ambassadorial Visit", "desc": "..." } }
```

Event *triggers/effects* are not decoded from the original; the clone
should define its own simple event scripting (see
[simulation.md](simulation.md) "Episodes & events") rather than try to
replicate the original's exact mechanism.

### `tables/terrain.json`, `tables/misc.json`, `tables/particles.json`

Low priority — `terrain.json` mainly feeds tile texture selection (see
[world-and-maps.md](world-and-maps.md)); `misc.json`/`particles.json` are
carried through as opaque key/value blobs for now.

`game` (global engine constants, root `data.{}` table) is **not extracted
as a general-purpose table** — but its four economy tuning floats
(`psca`/`psct`/`psfa`/`psft`) **are** extracted into `config.json`'s
`economy` block (see "Tunable constants" below); the rest of `game`
(UI colors/timers under `busy`/`colo`/`dlev`/`tune`/`uber`/`vict`, plus
loose fields like `prof`/`forc`/`squa`/`tdep`/`vter`) is not modeled — the
clone defines its own UI/engine config for those.

## Sprites

Each sprite is extracted to a single RGBA8888 PNG named after its full
original path with `/` replaced by `.` (e.g. `bldg.cher.cbaz.png` from
`bldg/cher/cbaz`). The manifest's `sprites` array records each sprite's
pixel dimensions so the game doesn't need to query SDL for layout decisions
before the texture is loaded.

For the spike, sprites are loaded individually via `IMG_LoadTexture`. Once
the game needs many sprites, see [rendering.md](rendering.md) for the atlas
packing strategy — atlas packing is a **rendering-layer concern** and
doesn't change this directory's per-sprite PNG format (the atlas is built
at game startup from the individual PNGs, not by the extractor), keeping
the extractor simple and the PNGs independently inspectable/moddable.

## Loader design (C++)

```cpp
namespace opente::data {

struct Manifest { int format_version; std::vector<SpriteEntry> sprites; std::vector<TableEntry> tables; };

class DataRegistry {
public:
    // search_paths: base game_data first, then mod overlays in priority order
    static DataRegistry load(const std::vector<std::filesystem::path>& search_paths);

    const Commodity& commodity(std::string_view id) const;   // throws on unknown id
    const Building&  building(std::string_view id) const;
    // ... one accessor per table type
};

} // namespace opente::data
```

- **Overlay merging**: for each table, later search paths' entries are
  merged into earlier ones *by ID* (added or overridden field-by-field at
  the JSON-object level — a mod can override just `base_price` for one
  commodity without restating the whole record). Sprites are resolved by
  filename, last-search-path-wins.
- **Typed structs, generated or hand-written**: each table gets a plain C++
  struct (`Commodity`, `Building`, ...) populated via `nlohmann::json`'s
  `from_json` overloads. No reflection/codegen needed at this scale (~16
  tables); hand-written `from_json` per struct is fine and keeps errors
  readable.
- **Cross-reference resolution**: references are stored as `std::string`
  IDs in the structs (not resolved pointers) — resolve to a reference/index
  on demand via the registry's accessors. This avoids load-order
  dependencies between tables and keeps structs trivially copyable/
  serializable for save games.
- **Fail-fast validation**: after loading all tables, run a validation pass
  that resolves every cross-reference once and aborts with a descriptive
  error listing all broken references found (not just the first) — this
  catches mod typos early and in one shot.

## Derived/denormalized data

Some convenience fields (e.g. a commodity's `produced_by_building`,
computed by inverting `epis.<ep>.comm.<id>.outputs`) are **computed by the
extractor at extraction time** and written into the JSON, rather than
computed by the game at load time. Rule of thumb: if a derivation is
*pure* (depends only on the base tables, no game state) and would otherwise
require every consumer to repeat an O(n) scan, compute it once in the
extractor. Keep the extractor's derivation logic in one place
(`tools/extractor/tables/derive.py` or similar) and document each derived
field's source in a comment, so it's clear it's not ground truth from the
original format.

## Extractor startup flow

The extractor needs to know where the player's unpacked Trade Empires
installation lives (the directory containing `Trade Empires.exe`,
`Data/`, etc.) before it can run.

1. **Command-line argument**: `opente-extract <source_dir> [output_dir]`. If
   `source_dir` is given and valid (see below), the extractor proceeds
   without prompting — this is the path used for CI/scripted re-extraction.
2. **No argument, or an invalid argument**: show a native directory-picker
   dialog (`tkinter.filedialog.askdirectory` — stdlib, no new dependency for
   the Python extractor) asking the player to locate their Trade Empires
   installation.
3. **Validation**: a candidate directory is valid if it contains
   `Trade Empires.exe` and a `Data/` subdirectory with at least one `.{}`
   container file. If invalid, show an explanation (which file(s) are
   missing) and re-show the directory picker — repeat until a valid
   directory is chosen or the user cancels the picker (cancel = exit with a
   non-zero status, no partial `game_data/` written).
4. Once a valid `source_dir` is established, extraction proceeds as normal,
   writing to `output_dir` (default `./game_data` next to the extractor
   executable, matching the game's default search path in "Launch
   configuration" above).

This mirrors the game's own "`game_data/` not found" flow (see
[ui.md](ui.md#startup-flow--dialogs)) but is simpler: no ini persistence
(the extractor is normally run once per game version, and the `--source`
argument covers repeat runs), and no "Exit"/"Pick directory" choice — just
keep re-prompting until valid input or cancel.

## Tunable constants

The original's economy formula depends on four named config constants
(`PSCA`/`PSCT`/`PSFA`/`PSFT`, see [simulation.md](simulation.md)) — these
are real `0x48`/float fields on `data.{}`'s root `game` table
(`psca=0.04`/`psct=0.06`/`psfa=0.04`/`psft=0.08`, confirmed
`documentation/03-exe-analysis.md` Round 20) and the extractor reads them
directly from there, plus a few others (`futx`/`futy` defaults, restock
period, etc.) not yet wired up. The clone collects all such constants into a
single `tables/config.json` (or `game_data/config.json` at the top level)
with descriptive names, the RE-confirmed defaults above, and comments
(JSON5/JSONC if the parser supports it, otherwise a sibling `config.md`
documenting each key) — these are the first things a balance-focused mod
would want to tweak.

## Open questions / RE gaps

- **`tech.<ep>.<id>.excl`** (`0` or `600`) — purpose unknown; passed through
  opaquely (see above). Tier 1 item B8.
- **`game` table** (global engine constants like cursor timing/UI colors) —
  not modeled; the clone has its own UI/engine config.

(Resolved: `epis.<ep>.dmnd` -> `demand`, `epis.<ep>.path` -> `movement_costs`,
and transporter speed/capacity/network-access -> `transporters.json`'s
`by_episode.<ep>.speed` — see "Table catalog" above and
`documentation/08-investigation-needed.md` T0.1/T0.2/T0.3.)
