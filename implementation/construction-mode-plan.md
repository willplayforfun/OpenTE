# Construction Mode + Build Menu Refactor — Implementation Plan

**Scope**: Stage 2 feature — building placement with footprint/exclusion preview,
trail placement, and a clean build-menu presenter. Self-contained; an agent can
execute this end-to-end.

**Reading order for context**:
- [`OpenTE/spec/input.md`](../spec/input.md) — placement rules
- [`OpenTE/spec/rendering.md`](../spec/rendering.md) — overlay rendering
- [`OpenTE/spec/ui.md`](../spec/ui.md) — build menu spec

---

## Background: RE findings (Phase 0 complete)

### Market building exclusion zones (RE-confirmed, constant across all episodes)

The original game's placement checker (`fcn.0x472560`/`0x475440`) reads a
`shape_id` from `this+0xac`. The setter (`fcn.0x4722a0`, vtable slot 22) looks
up the shape_id from `data.epis.<ep>.<bldg_type>` — a scalar field on the
episode record. The values are constant across all 19 episodes:

| Building ID | Name | `bldg.type` | `bldg.category` | `shape_id` |
|---|---|---|---|---|
| `trad` | Trading Post | `"mark"` | `"1"` | 4 |
| `mark` | Market | `"mark"` | `"2"` | 6 |
| `baza` | Bazaar | `"mark"` | `"3"` | 8 |
| `empo` | Emporium | `"mark"` | `"4"` | 10 |

All other building types (`bres`, `bpro`, `bdem`, `bdep`, `sipo`) have no
exclusion zone (`shape_id = 0`).

Formula: `shape_id = std::stoi(building.category) * 2 + 2` when
`building.type == "mark"`.

### Diamond exclusion shape extents (RE-confirmed from VA 0x632ab4..0x632bd4)

Each shape is a diamond of isometric tiles. `shape_id` N has 2N rows, with the
row index `r` in [0, 2N) corresponding to `dy = r - N` (where dy is the signed
vertical tile offset from the market center). The shape is slightly asymmetric:
dy spans [−N, N−1].

```
// shape_id 4 (Trading Post) — 8 rows, dy in [-4, +3]
{-2,+1}, {-3,+2}, {-4,+3}, {-4,+3}, {-4,+3}, {-4,+3}, {-3,+2}, {-2,+1}

// shape_id 6 (Market) — 12 rows, dy in [-6, +5]
{-2,+1}, {-4,+3}, {-5,+4}, {-5,+4}, {-6,+5}, {-6,+5}, {-6,+5}, {-6,+5}, {-5,+4}, {-5,+4}, {-4,+3}, {-2,+1}

// shape_id 8 (Bazaar) — 16 rows, dy in [-8, +7]
{-3,+2}, {-5,+4}, {-6,+5}, {-7,+6}, {-7,+6}, {-8,+7}, {-8,+7}, {-8,+7},
{-8,+7}, {-8,+7}, {-8,+7}, {-7,+6}, {-7,+6}, {-6,+5}, {-5,+4}, {-3,+2}

// shape_id 10 (Emporium) — 20 rows, dy in [-10, +9]
{-3,+2}, {-5,+4}, {-7,+6}, {-8,+7}, {-8,+7}, {-9,+8}, {-9,+8}, {-10,+9},
{-10,+9}, {-10,+9}, {-10,+9}, {-10,+9}, {-10,+9}, {-9,+8}, {-9,+8}, {-8,+7},
{-8,+7}, {-7,+6}, {-5,+4}, {-3,+2}
```

Each `{min_dx, max_dx}` pair gives the inclusive horizontal tile range at that
row. A point `(qx, qy)` is inside the exclusion zone of a market at `(cx, cy)`
iff:
```
dy = qy - cy;   row = dy + shape_id;
0 <= row < 2*shape_id  &&  extents[row].min_dx <= (qx - cx) <= extents[row].max_dx
```

### `Building` struct (existing, `data/types.h`)

The existing struct already has `type` (string) and `category` (string). No
extractor changes are needed; `exclusion_shape_id` is computed at game-load time
in `from_json`.

---

## Files to create

```
OpenTE/game/src/render/area_overlay.h
OpenTE/game/src/render/area_overlay.cpp
OpenTE/game/src/gameplay/construction_mode.h
OpenTE/game/src/gameplay/construction_mode.cpp
```

## Files to modify

```
OpenTE/game/src/data/types.h                   (add exclusion_shape_id to Building)
OpenTE/game/src/ui/build_menu.h                (refactor to pure presenter)
OpenTE/game/src/ui/build_menu.cpp              (matching implementation)
OpenTE/game/src/gameplay/gameplay_scene.h      (add construction_mode_, overlay_renderer_, etc.)
OpenTE/game/src/gameplay/gameplay_scene.cpp    (wire everything together)
OpenTE/game/CMakeLists.txt                     (add new source files)
```

---

## Phase 1 — Add `exclusion_shape_id` to `Building`

### `data/types.h` — `Building` struct

Add one field **after** `default_sprite`:

```cpp
int exclusion_shape_id = 0;  // 0 = no exclusion zone; 4/6/8/10 for market tiers
```

Add derivation to the **existing** `from_json(const nlohmann::json& j, Building& b)`:

```cpp
// After all existing field reads:
if (b.type == "mark") {
    try {
        int tier = std::stoi(b.category);        // "1"→1, "2"→2, "3"→3, "4"→4
        b.exclusion_shape_id = tier * 2 + 2;    // 4, 6, 8, 10
    } catch (...) {
        b.exclusion_shape_id = 0;
    }
}
```

No other changes needed — the extractor already exports `type` and `category`.

---

## Phase 2 — `AreaOverlayRenderer`

### `render/area_overlay.h`

```cpp
#pragma once

#include <SDL.h>
#include <vector>

#include "render/camera.h"
#include "render/terrain_renderer.h"

namespace opente::render {

/// One colored tile-set to draw. Caller builds a list of these per frame;
/// AreaOverlayRenderer draws all of them in a single pass.
struct OverlayTileSet {
    int center_tx = 0;          // market tile X
    int center_ty = 0;          // market tile Y
    int footprint_w = 1;        // footprint width  (tiles)
    int footprint_h = 1;        // footprint height (tiles)
    int exclusion_shape_id = 0; // 0 = footprint only; 4/6/8/10 = add exclusion diamond
    SDL_Color exclusion_color;  // color for the exclusion diamond tiles
    SDL_Color footprint_color;  // color for the footprint tile(s) drawn on top
};

/// Draws colored semi-transparent tile overlays on the terrain surface.
///
/// Usage: call render() once per frame after terrain but before buildings/UI.
/// Pass multiple OverlayTileSets for the same call — e.g. all existing
/// markets' exclusion zones (red) plus the current-placement footprint (yellow).
class AreaOverlayRenderer {
public:
    void render(SDL_Renderer* renderer,
                const Camera& camera,
                const TerrainRenderer& terrain,
                const std::vector<OverlayTileSet>& overlays);

private:
    struct DiamondExtent { int min_dx, max_dx; };

    // RE-confirmed shape tables (VA 0x632ab4..0x632bd4).
    // Index by shape_id_index = (shape_id - 4) / 2 in [0,4).
    static const DiamondExtent kShapeExtents[4][20];
    static const int           kShapeHalfHeights[4]; // {4, 6, 8, 10}

    static int shape_index(int shape_id);  // returns -1 if not a valid shape_id

    void render_tile_quad(SDL_Renderer* renderer,
                          const Camera& camera,
                          const TerrainRenderer& terrain,
                          int tx, int ty,
                          SDL_Color color);
};

}  // namespace opente::render
```

### `render/area_overlay.cpp`

```cpp
#include "render/area_overlay.h"

#include "render/iso.h"

namespace opente::render {

// ---------------------------------------------------------------------------
// RE-confirmed shape tables. kShapeExtents[i][r] = {min_dx, max_dx} for
// shape index i (shape_id = i*2+4) at row r (dy = r - kShapeHalfHeights[i]).
// ---------------------------------------------------------------------------
const AreaOverlayRenderer::DiamondExtent AreaOverlayRenderer::kShapeExtents[4][20] = {
    // shape_id 4 — 8 rows, dy in [-4, +3]
    {{-2,+1},{-3,+2},{-4,+3},{-4,+3},{-4,+3},{-4,+3},{-3,+2},{-2,+1}},
    // shape_id 6 — 12 rows, dy in [-6, +5]
    {{-2,+1},{-4,+3},{-5,+4},{-5,+4},{-6,+5},{-6,+5},{-6,+5},{-6,+5},{-5,+4},{-5,+4},{-4,+3},{-2,+1}},
    // shape_id 8 — 16 rows, dy in [-8, +7]
    {{-3,+2},{-5,+4},{-6,+5},{-7,+6},{-7,+6},{-8,+7},{-8,+7},{-8,+7},
     {-8,+7},{-8,+7},{-8,+7},{-7,+6},{-7,+6},{-6,+5},{-5,+4},{-3,+2}},
    // shape_id 10 — 20 rows, dy in [-10, +9]
    {{-3,+2},{-5,+4},{-7,+6},{-8,+7},{-8,+7},{-9,+8},{-9,+8},{-10,+9},
     {-10,+9},{-10,+9},{-10,+9},{-10,+9},{-10,+9},{-9,+8},{-9,+8},{-8,+7},
     {-8,+7},{-7,+6},{-5,+4},{-3,+2}},
};

const int AreaOverlayRenderer::kShapeHalfHeights[4] = {4, 6, 8, 10};

int AreaOverlayRenderer::shape_index(int shape_id) {
    if (shape_id == 4)  return 0;
    if (shape_id == 6)  return 1;
    if (shape_id == 8)  return 2;
    if (shape_id == 10) return 3;
    return -1;
}

// ---------------------------------------------------------------------------
// render_tile_quad — draws one isometric tile quad on the terrain surface.
// Matches the same height-displacement logic as TerrainRenderer.
// ---------------------------------------------------------------------------
void AreaOverlayRenderer::render_tile_quad(SDL_Renderer* renderer,
                                           const Camera& camera,
                                           const TerrainRenderer& terrain,
                                           int tx, int ty,
                                           SDL_Color color) {
    // Four corners of the tile in world space (top-left, top-right, bottom-right, bottom-left).
    // Each corner vertex in isometric grid: the four vertices of tile (tx, ty) are:
    //   top-left  = (tx,   ty)
    //   top-right = (tx+1, ty)
    //   bot-right = (tx+1, ty+1)
    //   bot-left  = (tx,   ty+1)
    // We height-displace each vertex by sampling terrain height at its grid position.
    struct Vertex { float x, y; };
    const int corners[4][2] = {{tx,ty},{tx+1,ty},{tx+1,ty+1},{tx,ty+1}};
    SDL_FPoint pts[4];
    for (int i = 0; i < 4; ++i) {
        Vec2 wp = tile_to_world(static_cast<float>(corners[i][0]),
                                static_cast<float>(corners[i][1]));
        float h = terrain.sample_height(corners[i][0], corners[i][1]);
        wp.y -= h * kPixelsPerAltiUnit;
        Vec2 sp = camera.world_to_screen(wp);
        pts[i] = {sp.x, sp.y};
    }

    // Build two triangles (0,1,2) and (0,2,3).
    SDL_Vertex verts[4];
    for (int i = 0; i < 4; ++i) {
        verts[i] = {pts[i], {color.r, color.g, color.b, color.a}, {0,0}};
    }
    const int indices[6] = {0,1,2, 0,2,3};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
void AreaOverlayRenderer::render(SDL_Renderer* renderer,
                                 const Camera& camera,
                                 const TerrainRenderer& terrain,
                                 const std::vector<OverlayTileSet>& overlays) {
    for (const OverlayTileSet& ots : overlays) {
        // 1. Exclusion diamond (drawn first, below footprint).
        int si = shape_index(ots.exclusion_shape_id);
        if (si >= 0) {
            int half = kShapeHalfHeights[si];
            int n_rows = half * 2;
            for (int r = 0; r < n_rows; ++r) {
                int dy = r - half;
                int ty = ots.center_ty + dy;
                const DiamondExtent& ext = kShapeExtents[si][r];
                for (int dx = ext.min_dx; dx <= ext.max_dx; ++dx) {
                    render_tile_quad(renderer, camera, terrain,
                                     ots.center_tx + dx, ty,
                                     ots.exclusion_color);
                }
            }
        }

        // 2. Footprint rectangle on top (typically 1×1 for market buildings).
        // Top-left of footprint centred on the building tile.
        int fp_left = ots.center_tx - (ots.footprint_w - 1) / 2;
        int fp_top  = ots.center_ty - (ots.footprint_h - 1) / 2;
        for (int dfy = 0; dfy < ots.footprint_h; ++dfy) {
            for (int dfx = 0; dfx < ots.footprint_w; ++dfx) {
                render_tile_quad(renderer, camera, terrain,
                                 fp_left + dfx, fp_top + dfy,
                                 ots.footprint_color);
            }
        }
    }
}

}  // namespace opente::render
```

**Colors to use** (caller's responsibility, but these are the intended values):
- Existing market exclusion zone: `{220, 60, 60, 120}` (red, semi-transparent)
- Aiming/pinned exclusion zone:   `{80, 200, 80, 100}` (green, semi-transparent)
- Aiming/pinned footprint:        `{255, 230, 60, 180}` (yellow, more opaque)
- Trail segment preview:          `{80, 180, 255, 140}` (blue)

---

## Phase 3 — `BuildMenu` refactor (pure presenter)

### Goals

- Remove `DataRegistry` dependency from `BuildMenu`.
- Move data assembly to `GameplayScene`.
- Add state setters (`set_construction_mode_active`, `set_confirm_visible`).
- Split the single `on_select` callback into `on_item_selected`, `on_confirm_clicked`, `on_exit_clicked`.
- **Keep all layout constants and rendering logic unchanged** — this is purely a
  data/callback restructuring.

### `ui/build_menu.h` — new public interface

Replace the current constructor and `OnSelectFn` with:

```cpp
// Data for one entry in a tab's list.
struct BuildMenuEntry {
    std::string id;
    std::string label;
    std::string desc;
    int         cost       = 0;
    bool        is_pathway = false;
};

// Pre-built data for all 5 tabs. Caller populates once and calls set_data().
struct BuildMenuData {
    std::vector<BuildMenuEntry> tabs[kNumTabs];
};

// New constructor — no registry argument.
BuildMenu(ConsSkin skin = {});

// Replace the data inside the menu (call after construction or on tab change).
void set_data(const BuildMenuData& data);

// Show/hide the "Exit Construction Mode" button (replaces the passive cancel).
// When true the cancel button label reads "EXIT CONSTRUCTION MODE" and fires
// on_exit_clicked when pressed.
void set_construction_mode_active(bool active);

// Show/hide the "CONFIRM" button. When false it is greyed out / unclickable.
// (The button already exists in the layout; this just enables it.)
void set_confirm_visible(bool visible);

// Callbacks — all optional (check before invoking).
std::function<void(const std::string& id)> on_item_selected;  // item clicked in list
std::function<void()>                      on_confirm_clicked;
std::function<void()>                      on_exit_clicked;
```

The `ListEntry` private struct becomes the same as `BuildMenuEntry` (or is
replaced by it). The `tab_entries_` array is still indexed the same way, just
now populated from `BuildMenuData` instead of from the registry.

The old `on_select_` callback is removed. The old `pressed_btn_` logic:
- Confirm button fires `on_confirm_clicked()` (only if `confirm_visible_` is true).
- Cancel button fires `on_exit_clicked()` (if `construction_mode_active_` is true)
  or just closes itself (the `GameplayScene` closes it via `ui_manager_.close()`
  — see Phase 4).

### `gameplay/gameplay_scene.cpp` — `rebuild_build_menu_data()`

Add a new private method that populates `BuildMenuData` from `registry_->buildings()`:

```cpp
void GameplayScene::rebuild_build_menu_data() {
    build_menu_data_ = {};
    // Tab indices: 0=path, 1=mark, 2=depo, 3=prod, 4=dema
    // (must match BuildMenu::kTabs order)
    static const char* kTypeToTab[] = {"path","mark","depo","prod","dema"};

    // Pathways tab (index 0): hardcoded 4 entries (same as before).
    build_menu_data_.tabs[0] = {
        {"trai","Trail",   "",0,true},
        {"road","Road",    "",0,true},
        {"rail","Railway", "",0,true},
        {"cana","Canal",   "",0,true},
    };

    // Building tabs (indices 1-4): filter registry buildings by type.
    // "depo" in the tab filter covers both "bdep" and "ware" types.
    for (const auto& [id, bldg] : registry_->buildings()) {
        if (bldg.culture_set != "defa") continue; // default culture only
        int tab = -1;
        if (bldg.type == "mark") tab = 1;
        else if (bldg.type == "bdep" || bldg.type == "ware") tab = 2;
        else if (bldg.type == "bpro") tab = 3;
        else if (bldg.type == "bdem") tab = 4;
        if (tab < 0) continue;
        build_menu_data_.tabs[tab].push_back({id, bldg.name, bldg.desc, bldg.build_cost, false});
    }
    // Sort each building tab alphabetically by label.
    for (int t = 1; t < 5; ++t) {
        std::sort(build_menu_data_.tabs[t].begin(), build_menu_data_.tabs[t].end(),
                  [](const ui::BuildMenuEntry& a, const ui::BuildMenuEntry& b){
                      return a.label < b.label;
                  });
    }
}
```

---

## Phase 4 — `ConstructionMode` state machine

### `gameplay/construction_mode.h`

```cpp
#pragma once

#include <string>
#include <vector>

namespace opente::gameplay {

enum class ConstructionPhase {
    None,
    BuildingAiming,   // footprint follows mouse; no confirm yet
    BuildingPinned,   // footprint locked; confirm button shown
    TrailPlacing,     // waypoint-by-waypoint trail
};

struct TrailMarker {
    int tx, ty;
};

class ConstructionMode {
public:
    ConstructionPhase phase() const { return phase_; }

    // The building ID or pathway ID selected ("trad", "road", etc.)
    const std::string& selected_id() const { return selected_id_; }

    // Current cursor tile (updated every mouse-move event).
    int cursor_tx() const { return cursor_tx_; }
    int cursor_ty() const { return cursor_ty_; }

    // Pinned tile (valid only in BuildingPinned).
    int pinned_tx() const { return pinned_tx_; }
    int pinned_ty() const { return pinned_ty_; }

    // Trail markers placed so far (valid in TrailPlacing).
    const std::vector<TrailMarker>& trail_markers() const { return trail_markers_; }

    // --- State transitions -----------------------------------------------

    // Enter building-placement mode for the given building id.
    // Phase transitions: None → BuildingAiming.
    void enter_building(const std::string& building_id);

    // Enter trail-placement mode for the given path type ("trai","road","rail","cana").
    // Phase transitions: None → TrailPlacing.
    void enter_trail(const std::string& path_id);

    // Update cursor tile position (call on SDL_MOUSEMOTION).
    void on_mouse_move(int new_tx, int new_ty);

    // Left click:
    //   BuildingAiming  → BuildingPinned (pins preview at cursor)
    //   BuildingPinned  → None (confirms placement, caller handles spawn)
    //   TrailPlacing    → adds a waypoint marker
    // Returns true if a placement was confirmed (BuildingPinned only).
    bool on_left_click();

    // Right click:
    //   BuildingAiming  → None (exit construction mode)
    //   BuildingPinned  → BuildingAiming (unpin, return to aiming)
    //   TrailPlacing    → remove last marker; if no markers remain → None
    void on_right_click();

    // Hard exit (ESC or "Exit" button).
    void exit();

    // True when simulation should be paused.
    bool is_active() const { return phase_ != ConstructionPhase::None; }

private:
    ConstructionPhase     phase_       = ConstructionPhase::None;
    std::string           selected_id_;
    int                   cursor_tx_   = 0;
    int                   cursor_ty_   = 0;
    int                   pinned_tx_   = 0;
    int                   pinned_ty_   = 0;
    std::vector<TrailMarker> trail_markers_;
};

}  // namespace opente::gameplay
```

### `gameplay/construction_mode.cpp`

```cpp
#include "gameplay/construction_mode.h"

namespace opente::gameplay {

void ConstructionMode::enter_building(const std::string& building_id) {
    selected_id_   = building_id;
    trail_markers_.clear();
    phase_         = ConstructionPhase::BuildingAiming;
}

void ConstructionMode::enter_trail(const std::string& path_id) {
    selected_id_   = path_id;
    trail_markers_.clear();
    phase_         = ConstructionPhase::TrailPlacing;
}

void ConstructionMode::on_mouse_move(int new_tx, int new_ty) {
    cursor_tx_ = new_tx;
    cursor_ty_ = new_ty;
}

bool ConstructionMode::on_left_click() {
    switch (phase_) {
        case ConstructionPhase::BuildingAiming:
            pinned_tx_ = cursor_tx_;
            pinned_ty_ = cursor_ty_;
            phase_     = ConstructionPhase::BuildingPinned;
            return false;
        case ConstructionPhase::BuildingPinned:
            phase_ = ConstructionPhase::None;
            selected_id_.clear();
            return true;  // placement confirmed
        case ConstructionPhase::TrailPlacing:
            trail_markers_.push_back({cursor_tx_, cursor_ty_});
            return false;
        default:
            return false;
    }
}

void ConstructionMode::on_right_click() {
    switch (phase_) {
        case ConstructionPhase::BuildingAiming:
            phase_ = ConstructionPhase::None;
            selected_id_.clear();
            break;
        case ConstructionPhase::BuildingPinned:
            phase_ = ConstructionPhase::BuildingAiming;
            break;
        case ConstructionPhase::TrailPlacing:
            if (!trail_markers_.empty()) {
                trail_markers_.pop_back();
            }
            if (trail_markers_.empty()) {
                phase_ = ConstructionPhase::None;
                selected_id_.clear();
            }
            break;
        default:
            break;
    }
}

void ConstructionMode::exit() {
    phase_ = ConstructionPhase::None;
    selected_id_.clear();
    trail_markers_.clear();
}

}  // namespace opente::gameplay
```

---

## Phase 5 — `GameplayScene` wiring

### `gameplay/gameplay_scene.h` — new members

Add to the `private:` section:

```cpp
#include "gameplay/construction_mode.h"
#include "render/area_overlay.h"
// (ui/build_menu.h already included transitively)

// Construction mode state machine.
ConstructionMode construction_mode_;

// Overlay renderer for footprint + exclusion diamonds.
render::AreaOverlayRenderer overlay_renderer_;

// Pre-built build menu data (rebuilt once from registry on first open).
ui::BuildMenuData build_menu_data_;
bool              build_menu_data_built_ = false;

// Simulation pause flag (true while construction mode is active).
bool sim_paused_ = false;
```

Also add these private methods:

```cpp
// Converts a screen pixel position to an isometric tile coordinate pair.
// Returns false if no world is loaded.
bool pick_tile_from_mouse(int screen_x, int screen_y, int& out_tx, int& out_ty);

// Builds OverlayTileSets for the current frame and calls overlay_renderer_.
void render_construction_overlays();

// Draws "Paused: Construction Mode" text in the bottom-right corner.
void render_hud_overlay();

// Assembles build_menu_data_ from the registry (called once, lazily).
void rebuild_build_menu_data();

// Handles what to do when a BuildMenu item is selected.
void on_build_menu_item_selected(const std::string& id);
```

### `gameplay/gameplay_scene.cpp` — changes

#### `pick_tile_from_mouse`

```cpp
bool GameplayScene::pick_tile_from_mouse(int screen_x, int screen_y,
                                          int& out_tx, int& out_ty) {
    if (!world_) return false;
    // Screen → world (inverse of camera.world_to_screen).
    float wx = screen_x / camera_.zoom + camera_.world_pixel_offset.x;
    float wy = screen_y / camera_.zoom + camera_.world_pixel_offset.y;
    render::Vec2 tile = render::world_to_tile(wx, wy);
    out_tx = static_cast<int>(std::floor(tile.x));
    out_ty = static_cast<int>(std::floor(tile.y));
    return true;
}
```

Note: this does not account for height displacement (complex to invert). It
gives a good-enough tile under the cursor for all practical map heights.

#### `render_construction_overlays`

```cpp
void GameplayScene::render_construction_overlays() {
    using Phase = ConstructionPhase;
    const Phase ph = construction_mode_.phase();
    if (ph == Phase::None) return;

    std::vector<render::OverlayTileSet> overlays;

    if (ph == Phase::BuildingAiming || ph == Phase::BuildingPinned) {
        const std::string& bid = construction_mode_.selected_id();
        const auto it = registry_->buildings().find(bid);
        if (it == registry_->buildings().end()) return;
        const data::Building& bldg = it->second;

        // Existing placed markets of the same type → red exclusion zones.
        // (Stage 2 stub: no placed buildings yet, so this loop is empty.)
        // When the placement/entity system exists, iterate placed_buildings here.

        // Current placement preview tile.
        int ptx = (ph == Phase::BuildingPinned) ? construction_mode_.pinned_tx()
                                                : construction_mode_.cursor_tx();
        int pty = (ph == Phase::BuildingPinned) ? construction_mode_.pinned_ty()
                                                : construction_mode_.cursor_ty();

        render::OverlayTileSet ots;
        ots.center_tx          = ptx;
        ots.center_ty          = pty;
        ots.footprint_w        = bldg.footprint.width;
        ots.footprint_h        = bldg.footprint.height;
        ots.exclusion_shape_id = bldg.exclusion_shape_id;
        ots.exclusion_color    = {80,  200, 80,  100};  // green
        ots.footprint_color    = {255, 230, 60,  180};  // yellow
        overlays.push_back(ots);

    } else if (ph == Phase::TrailPlacing) {
        // Draw a preview tile at the cursor position for trail placement.
        render::OverlayTileSet ots;
        ots.center_tx       = construction_mode_.cursor_tx();
        ots.center_ty       = construction_mode_.cursor_ty();
        ots.footprint_w     = 1;
        ots.footprint_h     = 1;
        ots.exclusion_shape_id = 0;
        ots.exclusion_color = {};
        ots.footprint_color = {80, 180, 255, 140};  // blue
        overlays.push_back(ots);

        // Also draw each committed trail marker.
        for (const TrailMarker& m : construction_mode_.trail_markers()) {
            render::OverlayTileSet ms;
            ms.center_tx       = m.tx;
            ms.center_ty       = m.ty;
            ms.footprint_w     = 1;
            ms.footprint_h     = 1;
            ms.exclusion_shape_id = 0;
            ms.exclusion_color = {};
            ms.footprint_color = {60, 140, 255, 200};  // brighter blue
            overlays.push_back(ms);
        }
    }

    overlay_renderer_.render(renderer_, camera_, *terrain_renderer_, overlays);
}
```

#### `render_hud_overlay`

```cpp
void GameplayScene::render_hud_overlay() {
    if (!sim_paused_) return;
    render::BitmapFont* font = ui_manager_.font();
    if (!font) return;

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);
    const char* msg = "Paused: Construction Mode";
    const int tw = font->measure_text(msg);
    const int x  = win_w - tw - 12;
    const int y  = win_h - font->ascender() - font->descender() - 12;
    const SDL_Color shadow{0, 0, 0, 255};
    const SDL_Color color{255, 220, 60, 255};
    font->draw_text_shadowed(renderer_, msg, x, y, color, shadow);
}
```

#### `rebuild_build_menu_data` (already drafted in Phase 3, move to .cpp)

#### `on_build_menu_item_selected`

```cpp
void GameplayScene::on_build_menu_item_selected(const std::string& id) {
    if (id.empty()) return;
    // Is it a pathway? Check if id is one of trai/road/rail/cana.
    static const std::array<const char*,4> kPathIds = {"trai","road","rail","cana"};
    bool is_path = std::any_of(kPathIds.begin(), kPathIds.end(),
                               [&](const char* p){ return id == p; });
    if (is_path) {
        construction_mode_.enter_trail(id);
    } else {
        construction_mode_.enter_building(id);
    }
    sim_paused_ = true;
    // Update menu state.
    if (build_menu_ptr_) {
        auto* menu = static_cast<ui::BuildMenu*>(build_menu_ptr_);
        menu->set_construction_mode_active(true);
        menu->set_confirm_visible(false);  // will become true on pin
    }
}
```

#### `toggle_build_menu` — new version

Replace the existing `toggle_build_menu()`:

```cpp
void GameplayScene::toggle_build_menu() {
    if (build_menu_ptr_) {
        // B key while open: if in construction mode, exit it but keep menu open.
        // (Original game: B only opens; Escape or exit button closes.)
        return;
    }

    if (!build_menu_data_built_) {
        rebuild_build_menu_data();
        build_menu_data_built_ = true;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);

    auto menu = std::make_unique<ui::BuildMenu>(cons_skin_);
    menu->set_data(build_menu_data_);
    menu->set_construction_mode_active(construction_mode_.is_active());
    menu->set_confirm_visible(
        construction_mode_.phase() == ConstructionPhase::BuildingPinned);

    menu->on_item_selected = [this](const std::string& id) {
        on_build_menu_item_selected(id);
    };
    menu->on_confirm_clicked = [this]() {
        // Confirm = same as left-click placement (already pinned).
        construction_mode_.on_left_click();
        sim_paused_ = construction_mode_.is_active();
        if (build_menu_ptr_) {
            auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
            m->set_construction_mode_active(false);
            m->set_confirm_visible(false);
        }
    };
    menu->on_exit_clicked = [this]() {
        construction_mode_.exit();
        sim_paused_ = false;
        // Keep build menu open (match original game).
        if (build_menu_ptr_) {
            auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
            m->set_construction_mode_active(false);
            m->set_confirm_visible(false);
        }
    };

    build_menu_ptr_ = menu.get();
    ui_manager_.open(std::move(menu), win_w, win_h);
}
```

#### `handle_event` — add mouse events for construction mode

In the `SDL_KEYDOWN` switch, add after the existing SDLK_b case:

```cpp
case SDLK_ESCAPE:
    if (down) {
        if (construction_mode_.is_active()) {
            construction_mode_.exit();
            sim_paused_ = false;
            if (build_menu_ptr_) {
                auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
                m->set_construction_mode_active(false);
                m->set_confirm_visible(false);
            }
        } else if (ui_manager_.has_open_dialogs()) {
            if (build_menu_ptr_) {
                ui_manager_.close(build_menu_ptr_);
                build_menu_ptr_ = nullptr;
            }
        } else {
            wants_quit_ = true;
        }
    }
    return true;
```

Add a new case in the main event switch (parallel to `SDL_MOUSEWHEEL`):

```cpp
case SDL_MOUSEMOTION: {
    if (construction_mode_.is_active()) {
        int tx, ty;
        if (pick_tile_from_mouse(event.motion.x, event.motion.y, tx, ty)) {
            construction_mode_.on_mouse_move(tx, ty);
            // Update confirm visibility if just pinned.
        }
    }
    return false;  // don't consume — let camera hover work
}

case SDL_MOUSEBUTTONDOWN: {
    // Don't fire if UI captured the event (ui_manager_.handle_event already ran).
    if (construction_mode_.is_active()) {
        int tx, ty;
        if (!pick_tile_from_mouse(event.button.x, event.button.y, tx, ty)) break;
        if (event.button.button == SDL_BUTTON_LEFT) {
            construction_mode_.on_mouse_move(tx, ty);
            bool confirmed = construction_mode_.on_left_click();
            sim_paused_ = construction_mode_.is_active();
            if (build_menu_ptr_) {
                auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
                m->set_construction_mode_active(construction_mode_.is_active());
                bool pinned = construction_mode_.phase() == ConstructionPhase::BuildingPinned;
                m->set_confirm_visible(pinned);
            }
            if (confirmed) {
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Placement confirmed at tile (%d, %d) for '%s'",
                            construction_mode_.cursor_tx(),
                            construction_mode_.cursor_ty(),
                            construction_mode_.selected_id().c_str());
                // TODO Stage 2: spawn placed building entity here.
            }
        } else if (event.button.button == SDL_BUTTON_RIGHT) {
            construction_mode_.on_right_click();
            sim_paused_ = construction_mode_.is_active();
            if (build_menu_ptr_) {
                auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
                m->set_construction_mode_active(construction_mode_.is_active());
                m->set_confirm_visible(
                    construction_mode_.phase() == ConstructionPhase::BuildingPinned);
            }
        }
        return true;
    }
    break;
}
```

**Important**: The existing `SDL_KEYDOWN SDLK_ESCAPE` case must be replaced
(don't duplicate it). The `handle_event` method processes `SDL_KEYDOWN`
cases in a switch-on-`event.key.keysym.sym` — the existing ESCAPE case closes
dialogs/quits; the new ESCAPE case described above replaces it.

#### `render()` — insert overlay pass

```cpp
void GameplayScene::render() {
    SDL_SetRenderDrawColor(renderer_, 10, 15, 25, 255);
    SDL_RenderClear(renderer_);

    if (world_) {
        terrain_renderer_->render(camera_);
        render_construction_overlays();   // ← insert after terrain, before buildings
        render_decorations();
        render_buildings();
    }

    render_hud_overlay();    // ← after world, before UI widgets
    ui_manager_.render();

    if (show_font_test_)       render_font_test();
    if (show_dev_gui_)         render_dev_gui();
    if (show_lighting_window_) render_lighting_window();
}
```

---

## Phase 6 — CMakeLists.txt

In `OpenTE/game/CMakeLists.txt`, add the two new source files to the
`target_sources` or equivalent list:

```cmake
src/render/area_overlay.cpp
src/gameplay/construction_mode.cpp
```

The header-only files (`area_overlay.h`, `construction_mode.h`) don't need
listing if they're in an already-included include directory.

---

## Testing checklist

Run `opente.exe` after each phase and verify:

**Phase 2** (overlay renderer alone — test with a temporary hard-coded call in `render()`):
- [ ] A colored diamond appears at a fixed tile, correctly height-displaced
- [ ] The footprint tile is visually distinct (different color, on top)

**Phase 3** (build menu refactor):
- [ ] `B` opens the menu as before
- [ ] All 5 tabs show the correct entries
- [ ] Selecting an item triggers `on_item_selected` (log line visible)
- [ ] Confirm and Exit buttons are present and respond to clicks

**Phase 4 + 5** (construction mode wired):
- [ ] Opening menu → clicking a market item → footprint appears following mouse
- [ ] Left click → footprint pins; "Confirm" button appears on menu
- [ ] Right click while pinned → returns to aiming, "Confirm" disappears
- [ ] Right click while aiming → exits construction mode, "Exit" button disappears
- [ ] "EXIT CONSTRUCTION MODE" button on menu → exits mode, menu stays open
- [ ] "Paused: Construction Mode" text appears bottom-right while in mode
- [ ] Opening menu → clicking a pathway item → blue tile follows mouse
- [ ] Left click → places a waypoint marker (stays drawn as brighter blue)
- [ ] Additional left clicks → additional markers
- [ ] Right click → removes last marker
- [ ] Right click with 0 markers → exits trail mode
- [ ] ESC while in construction mode → exits mode without closing menu
- [ ] ESC while not in construction mode, menu open → closes menu
- [ ] ESC while not in construction mode, menu closed → quits

---

## Deferred / out of scope

- **Actual building spawn**: when placement is confirmed, log it (done above);
  the real entity-spawn call belongs to the entity/placement system (Stage 2/3).
- **Legality check**: terrain type, adjacency, market-exclusion collision with
  placed buildings — deferred until there's a placed-buildings store to query.
- **Trail segment rendering**: draw actual road/trail tiles along the
  committed waypoints (not just dot markers). Deferred to Stage 3.
- **`mcap` enforcement**: max-markets-per-player check — deferred to simulation
  stage when player context exists.
- **Multi-region / episode-specific shape overrides**: the shape_id values are
  constant in the original game (confirmed across all 19 episodes). No override
  mechanism needed.
