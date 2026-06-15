#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/types.h"
#include "ui/panel.h"
#include "ui/skin.h"
#include "ui/widget.h"

namespace opente::ui {

/// The build menu (Stage 2) — a floating panel listing all placeable
/// buildings grouped by category, with a scrollable list and click-to-select.
///
/// Derived from the original's TConstructionPanel / TConstructionWindow
/// (ctors at 0x42419c / 0x42475f) which loaded the same category groups via
/// the 'mark', 'depo', 'prod', 'dema', 'bldg', 'path' sprite tags (verified
/// from the push/call-0x415fc0 scan of TStructurePanelView @ 0x424940).
///
/// When a ConsSkin is provided the panel uses the original game's `cons.back`
/// (306×696) as the background frame, `cons.sele` (240×20) as the row hover
/// highlight, and the conf/canc buttons at their manifest-specified positions.
/// Without a skin (or if sprites fail to load) the panel falls back to flat
/// colours at kFallbackWidth×kFallbackHeight.
///
/// Press B or Escape to close.  Clicking a building entry invokes on_select.
class BuildMenu : public Widget {
public:
    using OnSelectFn = std::function<void(const std::string& building_id)>;

    BuildMenu(const std::map<std::string, data::Building>& buildings,
              OnSelectFn on_select,
              ConsSkin skin = {});

    void layout(Rect window_bounds) override;
    void render(SDL_Renderer* renderer, Font* font) const override;
    bool handle_event(const SDL_Event& e) override;

    // Fallback dimensions used when no skin is available.
    static constexpr int kFallbackWidth  = 270;
    static constexpr int kFallbackHeight = 520;

private:
    struct RowEntry {
        bool is_header;
        std::string label;
        std::string building_id;  // empty for category headers
        int cost = 0;
    };

    struct RowWidget {
        int local_y;   // y-position within the scrollable content, in pixels
        int local_h;
        std::unique_ptr<Widget> widget;
    };

    // --- layout constants ---
    static constexpr int kRowHeight    = 24;
    static constexpr int kHeaderHeight = 20;
    static constexpr int kPadding      = 6;
    static constexpr int kScrollStep   = 48;

    std::vector<RowEntry> entries_;  // pre-sorted by category
    OnSelectFn on_select_;
    ConsSkin skin_;

    // Laid-out state (populated by layout()).
    Rect menu_rect_;   // absolute menu bounds (screen space)
    Rect list_rect_;   // absolute bounds of the scrollable list area
    int dialog_ox_ = 0, dialog_oy_ = 0;  // dialog-origin in screen space
    std::vector<RowWidget> rows_;
    int content_height_ = 0;
    int scroll_offset_  = 0;

    // Hover row index (-1 = none); updated by handle_event for sele drawing.
    int hover_row_ = -1;

    // Fallback flat-colour panel (used when skin_ is invalid).
    std::unique_ptr<Panel> bg_panel_;
    std::unique_ptr<Label> title_label_;

    void   clamp_scroll();
    int    max_scroll() const noexcept;
    Rect   row_abs_rect(const RowWidget& row) const noexcept;
    void   render_scrollbar(SDL_Renderer* renderer) const;
};

}  // namespace opente::ui
