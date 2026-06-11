# UI

This document specifies the clone's UI: widget framework, HUD, and the
core dialogs (market, build menu, building info). The original implements
roughly one C++ class per dialog (`T*Window` classes,
`documentation/03-exe-analysis.md` RTTI inventory) backed by large UI
resource containers (`a_ui`/`d_ui`/`m_ui`, `documentation/04-other-formats.md`)
whose pixel-level layout wasn't decoded. The clone does **not** attempt to
replicate the original's pixel-exact UI — it specifies a small, modern,
data-driven widget framework and the *set of screens* a playable clone
needs, informed by what data the original exposes (market prices, building
status, build menus) rather than its exact chrome.

## Widget framework

A minimal immediate-mode-flavored widget layer is sufficient — no need for
a full retained-mode UI toolkit:

```cpp
namespace opente::ui {

struct Rect { int x, y, w, h; };

class Widget {
public:
    virtual ~Widget() = default;
    virtual void layout(Rect bounds) = 0;
    virtual void render(Renderer& r) const = 0;
    virtual bool handle_event(const SDL_Event& e) = 0; // true = consumed
};

class Panel : public Widget { /* container, holds children */ };
class Label : public Widget { /* text */ };
class Button : public Widget { /* clickable, on_click callback */ };
class Image : public Widget { /* sprite from SpriteAtlas */ };
class Checkbox : public Widget { /* boolean toggle, on_toggle callback */ };
class ScrollPanel : public Widget { /* clips + scrolls a child via mouse wheel/drag */ };

class UIManager {
public:
    void open(std::unique_ptr<Widget> dialog);  // pushes a modal/non-modal dialog
    void close(Widget* dialog);
    bool handle_event(const SDL_Event& e);      // dispatches to top-most widgets first
    void render(Renderer& r);
};

} // namespace opente::ui
```

- **Screen-space only**: UI widgets render in screen space (not affected by
  camera pan/zoom), per [rendering.md](rendering.md).
- **Text rendering**: use `SDL_ttf` (add to `vcpkg.json`) with a bundled
  open-license font for UI text — the original's custom bitmap font format
  (`Data/font.{}`, `04-other-formats.md`) is not worth reverse-engineering
  for the clone; a real font renderer is strictly better (scalable,
  localizable, no glyph-atlas decoding needed).
- **Sprite-skinning** (buttons, panel backgrounds, icons): drawn via
  `Image` widgets referencing `SpriteAtlas` entries from extracted UI
  sprite sheets (`a_ui`/`d_ui`/`m_ui` — **extraction itself is done**, see
  "Open questions / RE gaps" below; only extractor wiring + sprite-to-widget
  mapping remain). Until that wiring lands, use flat-colored `SDL_Rect`s as
  placeholders — this should not block building out the dialogs'
  *functionality*.

## Startup flow & dialogs

Before the main menu appears, the game runs two pre-flight checks. Both are
**modal and blocking** — nothing else renders until they're resolved. See
[data-model.md](data-model.md) "Launch configuration" for the `OpenTE.ini`
format and `game_data/` resolution rules, and
[modding.md](modding.md) for `mod.json`/`mods_enabled.json`.

### 1. `game_data/` location check

Runs first, since nothing else (including loading the bundled UI font) needs
`game_data/`, but the mod list in step 2 is read from alongside it.

1. Resolve the `game_data/` path per data-model.md (ini override, else
   `./game_data` next to the executable).
2. If the resolved path is missing or fails manifest validation
   (`manifest.json` absent or `format_version` mismatch), show a native
   message box (`SDL_ShowMessageBox`, since no renderer/font is guaranteed
   yet) explaining the problem (e.g. *"game_data was not found next to
   Trade Empires Clone.exe. Run the extractor first, or pick the folder
   where it was generated."*) with two buttons: **Exit** and **Pick
   directory**.
3. **Exit** quits immediately (return code != 0).
4. **Pick directory** opens a native folder-picker (`NFD_PickFolder` from
   `nativefiledialog-extended`, new vcpkg dependency). The chosen folder is
   re-validated against the same manifest check.
   - If valid: write it to `OpenTE.ini` (data-model.md) and continue to
     step 2.
   - If invalid: show the explanation dialog again (step 2), looping
     until the user either picks a valid directory or chooses **Exit**.

### 2. Mod selection dialog

Runs after `game_data/` is confirmed valid, using the in-engine widget
framework (so it can use `SDL_ttf` + the bundled font and the real
`UIManager`).

1. Scan `mods/` (next to the executable) for subdirectories containing a
   `mod.json` (modding.md). If none are found, skip this dialog entirely —
   proceed straight to the main menu with no overlays.
2. Otherwise, open a modal dialog: a `ScrollPanel` containing one row per
   discovered mod, each row a `Panel` with three children — a `Label` for
   `mod.json`'s `name`, a `Label` for its `description`, and a `Checkbox`
   reflecting whether the mod is currently enabled.
3. Initial checkbox state comes from `mods_enabled.json` (modding.md) next
   to `game_data/`; mods present in `mods/` but absent from
   `mods_enabled.json` default to **unchecked** (new mods are opt-in).
4. A **Continue** button closes the dialog, writes the current checkbox
   states back to `mods_enabled.json`, and proceeds to
   `DataRegistry::load` with the search path
   `[game_data/, ...enabled mods in mod.json load_priority order]`.

### Open questions

- **Folder-picker library**: `nativefiledialog-extended` is the leading
  candidate (actively maintained, vcpkg port, native dialogs on
  Windows/macOS/Linux) but hasn't been spiked yet — confirm it builds
  cleanly via vcpkg manifest mode alongside the existing SDL2/SDL2_image/
  nlohmann-json deps before relying on it.
- **Re-validation message wording**: when a picked directory is invalid,
  the dialog should say *why* (missing `manifest.json` vs. wrong
  `format_version` vs. not a directory) rather than a generic "invalid
  directory" — exact strings are a polish item for
  [implementation/](../implementation/).

## HUD (always visible)

- **Treasury display**: current player's coin balance (top of screen).
- **Date/clock**: current in-game date, derived from `tick` and a
  config-defined ticks-per-day (ties into [simulation.md](simulation.md)'s
  "period" length).
- **Minimap**: small top-down render of `terrain.data` +
  building/merchant positions, click-to-recenter camera. Can be a simple
  software-rendered texture updated periodically (every N ticks), not every
  frame.
- **Build menu toggle** and **selected-entity info panel** (bottom of
  screen, shows details for the currently-selected building/merchant).

## Dialogs

### Build menu

- List of available buildings/pathways for the current player, grouped by
  category (`buildings.json.category`), filtered by researched technology
  (`technologies.json[*].unlocks`).
- Clicking an entry enters `InputMode::PlacingBuilding`/`DraggingPathway`
  (see [input.md](input.md)).
- Disabled (grayed) entries show a tooltip with the unlock requirement
  (e.g. "Requires: <tech name>").

### Market window

Per market building ([entities.md](entities.md)'s `MarketGood` list):

- Table of tracked commodities: name, current price, stock, demand-tier
  indicator (growing/stable/declining, from
  [simulation.md](simulation.md)'s `demand_tier`).
- A simple price-history sparkline (last N periods) is a nice-to-have —
  store a small rolling buffer of `current_price` per `MarketGood` (e.g.
  last 50 periods) purely for this display; not part of the save format's
  required state (regenerable/lossy is fine).

### Building info panel

- Name, owner, production status (active/idle + reason if idle, e.g.
  "no <commodity> available"), treasury (for production buildings),
  current recipe.
- For markets: population, list of merchants currently based here.

### Merchant info panel

- Name, ability ([entities.md](entities.md)'s `MerchantState.ability`),
  current order/destination, cargo manifest.

### Placement-error tooltip

Maps each [input.md](input.md) `PlacementError` to a short player-facing
string (e.g. `TooCloseToSameType` -> "Too close to another building of this
type"). Keep this mapping in one table (`ui::placement_error_text(...)`) so
adding a new `PlacementError` value is a one-line addition.

### Tech/advance dialog

List of researchable technologies for the current episode
([data-model.md](data-model.md)'s `technologies.json`), filtered to those
with `available_at_tick <= current_tick` and not yet researched. Shows cost
and `unlocks` summary; clicking researches (deducts cost).

## Notifications / toasts

A small queue of transient on-screen messages (building complete,
demand growing/declining alerts, event triggers) — corresponds to the
original's "Building complete!" toast (`03-exe-analysis.md` Round 17) and
demand-tier alerts (Round 4-7). Implementation: `UIManager::push_toast(text,
duration_ms)`, rendered as a fading label stack in a corner of the screen.

## Open questions / RE gaps

- ~~**Original UI sprite extraction**~~ **Resolved** (T0.4 — see
  `documentation/08-investigation-needed.md`): `a_ui,6.{}` (181 leaves,
  RGB565), `d_ui,5.{}` (379 leaves, ARGB4444), and `m_ui,u.{}` (111 leaves:
  93 RGBA8888 terrain textures + 18 palette RGBA) all decode with zero
  fallbacks via `scripts/te_sprite.py` — see `01-container-format.md` "Pixel
  formats"/"Two color-depth variants". Extraction is done; what remains is
  **extractor wiring** (Stage 8, or pull forward if cheap) and mapping
  specific sprite IDs to specific widgets/dialogs (a UX/layout task, not
  RE). Until wired up, the clone's UI uses placeholder flat colors +
  `SDL_ttf` text; `Image` widgets already take a `SpriteAtlas` reference so
  swapping in real sprites doesn't change widget logic.
- **Original bitmap font format** (`Data/font.{}`): not decoded, and the
  clone deliberately doesn't need it (uses `SDL_ttf` instead) — listed here
  only so it's not mistaken for a blocking gap.
