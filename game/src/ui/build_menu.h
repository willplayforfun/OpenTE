#pragma once

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "data/types.h"
#include "ui/panel.h"
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
/// The menu is a fixed-size panel anchored at the top-left corner of the
/// screen. Stage 8 will replace flat colours with a_ui/d_ui sprite skins.
///
/// Press B or Escape to close. Clicking a building entry invokes the
/// `on_select` callback (provided by App) and also closes the menu.
class BuildMenu : public Widget {
public:
    /// Called with the building id when the player selects an entry.
    using OnSelectFn = std::function<void(const std::string& building_id)>;

    BuildMenu(const std::map<std::string, data::Building>& buildings,
              OnSelectFn on_select);

    void layout(Rect window_bounds) override;
    void render(SDL_Renderer* renderer, TTF_Font* font) const override;
    bool handle_event(const SDL_Event& e) override;

    static constexpr int kMenuWidth  = 270;
    static constexpr int kMenuHeight = 520;

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
    static constexpr int kTitleHeight  = 32;
    static constexpr int kRowHeight    = 26;
    static constexpr int kHeaderHeight = 22;
    static constexpr int kPadding      = 6;
    static constexpr int kScrollStep   = 48;

    std::vector<RowEntry> entries_;  // pre-sorted by category
    OnSelectFn on_select_;

    // Laid-out state (populated by layout()).
    Rect menu_rect_;   // absolute menu bounds
    Rect list_rect_;   // absolute bounds of the scrollable list area
    std::vector<RowWidget> rows_;
    int content_height_ = 0;
    int scroll_offset_  = 0;  // pixels scrolled from top

    // Shared panels (background, title).
    std::unique_ptr<Panel> bg_panel_;
    std::unique_ptr<Label> title_label_;

    // Helpers.
    void clamp_scroll();
    int  max_scroll() const noexcept;
    Rect row_abs_rect(const RowWidget& row) const noexcept;
};

}  // namespace opente::ui
