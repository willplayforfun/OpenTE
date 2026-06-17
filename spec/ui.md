# UI

This document specifies the clone's UI: widget framework, HUD, and the
core dialogs (market, build menu, building info). The original implements
roughly one C++ class per dialog (`T*Window` classes from the RTTI
inventory) backed by large UI resource containers (`a_ui`/`d_ui`/`m_ui`)
whose pixel-level layout has been **partially** reverse-engineered. The
HUD bars (top bar `bart`, bottom toolbar `barb`, map viewport) have been
decoded to pixel-exact coordinates — see `documentation/toolbar-re.md`.
The clone uses those exact rects for the always-visible chrome; for dialog
panels, it specifies a small, modern widget framework informed by what data
the original exposes (market prices, building status, build menus) rather
than their exact pixel chrome.

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
- **Text rendering** (SUPERSEDES the original "use SDL_ttf" plan): the clone
  renders the original's **own bitmap fonts**, extracted from `Data/font.{}`
  to glyph atlases (`game_data/fonts/<face>_<pt>pt.{png,json}`) and drawn via
  `render::BitmapFont` / `render::FontCache` (`fonts.get("seri", 11)`). Four
  faces: `clea` (mixed-case), `seri`/`sans` (**small-caps faces** — lowercase
  codepoints are small-capital glyphs), `cour` (mono). This was chosen over
  `SDL_ttf` because the small-caps look and exact metrics are integral to the
  original's appearance and come *for free* from the real glyphs — there is no
  runtime case/scale transform. UI strings come from the extracted `strings`
  table (`Data/text.{}` → `DataRegistry::text("cons.labl.titl", fallback)`),
  with `[N …]` placeholder substitution. See `documentation/cons-panel-re.md`
  (fonts/small-caps), `documentation/04-other-formats.md` (`text.{}` strings).
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

**Pixel-exact layout is now fully RE'd — see `documentation/toolbar-re.md`.**

Screen is 1024×768. The fixed HUD occupies the top 20px and bottom 55px,
leaving a 951×681 map viewport at (10, 24).

### Top bar `bart` — (0, 0, 1024, 20), sprite `tool.topb`

Sub-views within the top bar (coords relative to bar's top-left):

| x, y | w, h | Content |
|------|------|---------|
| 8, 2 | 129, 18 | Text: treasury / player gold |
| 146, 1 | 22, 18 | `spee` small button (speed –) |
| 173, 1 | 161, 20 | `spee` speed display/selector |
| 429, 1 | 22, 18 | `rena` small button (confirm name?) |
| 458, 1 | 185, 20 | `rena` player name display / editable field |
| 760, 2 | 110, 18 | Text: in-game date / era |
| 884, 2 | 100, 18 | Text: score / ranking |
| 986, 3 | 12, 12 | Small button: options / game menu |

### Bottom toolbar `barb` — (0, 713, 1024, 55), sprite `tool.botb`

`tool.botb` (1024×54) is the bottom bar background — a plain stone bar with
**no button icons baked in**.  All 8 mode button sprites are rendered
dynamically every frame: frame 1 (right half) = unselected, frame 0 (left
half) = selected/active.  This is confirmed by the SetSprite call at
`0x53e51d` in the TSilkRoadView constructor and the per-button frame fields
`[sub+0x6c]=2` (frame count) and `[sub+0x70]=1` (initial frame = unselected).

Mode button positions within barb (left group y=14, right group y=12):

Each button has `frame_count=2`, `initial_frame=1` (right half = unselected).
On selection, render frame 0 (left half). Frame width = sprite_width / 2.

| Tag  | x, y (in barb) | w, h | Sprite | Frame 0 (selected) | Frame 1 (default) |
|------|----------------|------|--------|--------------------|--------------------|
| play | 28, 14 | 35, 33 | tool.play 70×33 | left 35px | right 35px |
| rout | 84, 14 | 35, 33 | tool.rout 70×33 | left 35px | right 35px |
| cons | 141, 14 | 35, 33 | tool.cons 70×33 | left 35px | right 35px |
| tech | 197, 14 | 35, 33 | tool.tech 70×33 | left 35px | right 35px |
| terr | 765, 12 | 35, 33 | tool.terr 70×33 | left 35px | right 35px |
| regi | 816, 12 | 36, 33 | tool.regi 72×33 | left 36px | right 36px |
| worl | 866, 12 | 35, 33 | tool.wmap 70×33 | left 35px | right 35px |
| game | 966, 12 | 35, 33 | tool.game 70×33 | left 35px | right 35px |

Center band (`hot1`/`hot2`/`hot3`): the **mode-button tooltip area**. Hovering
a button shows its name in `hot1` (287,12,450,14) and, for the 5 toggle/control
buttons, a `"Click to …"` description in `hot2` (287,26,450,14); the 3 map
buttons show the name only (single line, `hot3` 271,12,482,29). The strings are
EXE defaults loaded by the original via the `.ini` layout system — NOT the
`text.{}` `strings` table. See `documentation/toolbar-re.md` "Toolbar tooltips"
for the verbatim per-button strings; implemented in `game/src/ui/hud.cpp`.

### Map viewport

Rect: (10, 24, 951, 681) — not full-width; 10px left margin, 63px right gap
(for the side panel / construction panel when open).

## Dialogs

### Build menu

- List of available buildings/pathways for the current player, grouped by
  category (`buildings.json.category`), filtered by researched technology
  (`technologies.json[*].unlocks`).
- Clicking an entry enters `InputMode::PlacingBuilding`/`DraggingPathway`
  (see [input.md](input.md)).
- Disabled (grayed) entries show a tooltip with the unlock requirement
  (e.g. "Requires: <tech name>").
- **Fonts / labels (RE-exact, `documentation/cons-panel-re.md`)**: title +
  categories + name + conf/cancel labels = `seri`; list rows + description body
  = `sans` (both small-caps faces). All label text is the verbatim
  `cons.labl.*` string from the `strings` table — never uppercased/transformed.
  Full-caps headers (e.g. `MARKETS`, `C O N S T R U C T I O N`) are full caps
  because the *source string* is; the cancel button + building names are mixed
  case → render as small caps. List rows use the `stdc.sele` highlight
  (194×17), not the wider `cons.sele` category bar. Implemented in
  `game/src/ui/build_menu.cpp`.

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
original's "Building complete!" toast and demand-tier alerts.
Implementation: `UIManager::push_toast(text,
duration_ms)`, rendered as a fading label stack in a corner of the screen.

## Open questions / RE gaps

- ~~**Original UI sprite extraction**~~ **Resolved**: `a_ui,6.{}` (181
  leaves, RGB565), `d_ui,5.{}` (379 leaves, ARGB4444), and `m_ui,u.{}` (111
  leaves: 93 RGBA8888 terrain textures + 18 palette RGBA) all decode with
  zero fallbacks via the sprite extractor. Extraction is done; what remains is
  **extractor wiring** (Stage 8, or pull forward if cheap) and mapping
  specific sprite IDs to specific widgets/dialogs (a UX/layout task, not
  RE). Until wired up, the clone's UI uses placeholder flat colors +
  `SDL_ttf` text; `Image` widgets already take a `SpriteAtlas` reference so
  swapping in real sprites doesn't change widget logic.
- ~~**Original bitmap font format** (`Data/font.{}`)~~ **DECODED & USED**: the
  glyph-atlas format is reverse-engineered and the clone renders the real
  fonts (see the "Text rendering" bullet above and `cons-panel-re.md`). The
  earlier "use SDL_ttf, don't RE the font" plan is superseded.
