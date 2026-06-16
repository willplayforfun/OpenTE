#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/panel.h"
#include "ui/skin.h"
#include "ui/widget.h"

namespace opente::ui {

/// Build/construction menu — replicates TConstructionPanel / TStructurePanel.
///
/// RE-confirmed structure (TStructurePanel slot[1] init @ 0x004263c0):
///   5 category tabs shown as VERTICAL stacked buttons (path first):
///     PATHWAYS, MARKETS, DEPOTS, PRODUCTION BUILDINGS, DEMAND BUILDINGS
///   Each tab has its own scrollable building list.
///   The 'path' tab shows trai/road/rail/cana transport type choices.
///   Below the list: building sprite preview, then info box (name/cost/desc).
///   Confirm (at +0x540) enabled when set_confirm_visible(true); shows cost.
///   Cancel always enabled; text = "EXIT CONSTRUCTION MODE".

// --- Data types for caller-supplied tab content ------------------------------

struct BuildMenuEntry {
    std::string id;
    std::string label;
    std::string desc;
    int  cost       = 0;
    bool is_pathway = false;
};

struct BuildMenuData {
    static constexpr int kNumTabs = 5;
    std::vector<BuildMenuEntry> tabs[kNumTabs];
};

// --- Widget ------------------------------------------------------------------

class BuildMenu : public Widget {
public:
    explicit BuildMenu(ConsSkin skin = {});

    void layout(Rect window_bounds) override;
    void render(SDL_Renderer* renderer, FontCache& fonts) const override;
    bool handle_event(const SDL_Event& e) override;

    // Populate all 5 tabs from pre-built data (call after construction or
    // whenever the available-buildings set changes).
    void set_data(const BuildMenuData& data);

    // Show/grey the CONFIRM button. Pass true when a building is pinned.
    void set_confirm_visible(bool visible);

    // When true the EXIT button label/behaviour reflects "you are in
    // construction mode". (Currently cosmetic — always labelled EXIT.)
    void set_construction_mode_active(bool active);

    // Callbacks — all optional; set by caller after constructing.
    std::function<void(const std::string& id)> on_item_selected;  // list row clicked
    std::function<void()>                      on_confirm_clicked;
    std::function<void()>                      on_exit_clicked;

    static constexpr int kFallbackWidth  = 306;
    static constexpr int kFallbackHeight = 696;

private:
    // ---------------------------------------------------------------------------
    // Category definitions (RE-confirmed: path first, mark/depo/prod/dema after)
    // ---------------------------------------------------------------------------
    static constexpr int kNumTabs = 5;
    struct TabDef {
        const char* type_filter;
        const char* label;
    };
    static constexpr TabDef kTabs[kNumTabs] = {
        {"path", "PATHWAYS"},
        {"mark", "MARKETS"},
        {"depo", "DEPOTS"},
        {"prod", "PRODUCTION BUILDINGS"},
        {"dema", "DEMAND BUILDINGS"},
    };

    // ---------------------------------------------------------------------------
    // EXACT widget geometry — from the original layout fn 0x424940 (see
    // documentation/cons-panel-re.md, Phase 7). All rects are in background-
    // sprite pixel space; the panel sub-view is itself (0,0,306,696), so a
    // widget's screen position is simply menu_rect_.{x,y} + (x,y) (the
    // background blits with its top-left at the menu origin).
    // ---------------------------------------------------------------------------
    static constexpr int kTitleX = -4, kTitleY = 15, kTitleW = 277, kTitleH = 25;

    // Five category rows, stacked, x=14 w=240 h=20. y per row below.
    // kCatY index order matches kTabs: {path, mark, depo, prod, dema}.
    static constexpr int kCatX = 14, kCatW = 240, kCatH = 20;
    static constexpr int kCatY[kNumTabs] = {66, 88, 110, 132, 154};

    // Building list, preview panel, description box.
    static constexpr int kListX = 38,  kListY = 186, kListW = 255, kListH = 121;
    static constexpr int kPrevX = 0,   kPrevY = 316, kPrevW = 272, kPrevH = 116;
    static constexpr int kDLblX = 22,  kDLblY = 461, kDLblW = 225, kDLblH = 20;
    static constexpr int kDescX = 22,  kDescY = 486, kDescW = 272, kDescH = 109;
    // Body text wraps at the 'text' child widget's width (+0x9c0 = 225), not
    // the 272 outer frame — this is why the original text column is narrower.
    static constexpr int kDescContentW = 225;

    // Bottom buttons: small icon (x=22) + wide label strip (x=61, w=204).
    // We make the whole icon+label strip clickable.
    static constexpr int kBtnX = 22, kBtnW = 243, kBtnH = 29;
    static constexpr int kConfY = 618;
    static constexpr int kCancY = 650;

    static constexpr int kRowHeight  = 20;
    static constexpr int kScrollStep = kRowHeight * 2;

    // Scrollbar (cons.vscr) slice constants — see documentation/cons-scrollbar-re.md.
    static constexpr int kScrollW   = 16;
    static constexpr int kArrowH    = 16;
    static constexpr int kThumbMinH = 8;
    static constexpr int kSliceH    = 4;
    static constexpr int kSlidTop   = 32;
    static constexpr int kSlidMid   = 36;
    static constexpr int kSlidBot   = 40;
    static constexpr int kTrackTile = 44;
    static constexpr int kVscrUp    = 0;

    // ---------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------
    ConsSkin skin_;

    std::vector<BuildMenuEntry> tab_entries_[kNumTabs];

    bool confirm_visible_          = false;
    bool construction_mode_active_ = false;

    int         active_tab_    = 1;  // default to MARKETS (index 1)
    std::string selected_id_;
    int         selected_row_  = -1;
    int         hover_row_     = -1;
    int         scroll_offset_ = 0;
    int         pressed_btn_   = 0;  // 0=none, 1=confirm, 2=cancel (held down)
    int         pressed_arrow_ = 0;  // 0=none, 1=up, 2=down (scrollbar arrow held)
    bool        dragging_thumb_ = false;
    int         drag_dy_       = 0;

    // ---------------------------------------------------------------------------
    // Laid-out geometry (set by layout())
    // ---------------------------------------------------------------------------
    Rect menu_rect_;
    Rect tab_rects_[kNumTabs] = {};
    Rect list_rect_;
    Rect preview_rect_;
    Rect info_rect_;
    Rect conf_rect_;
    Rect canc_rect_;
    int  dialog_ox_ = 0, dialog_oy_ = 0;

    std::unique_ptr<Panel> bg_panel_;

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------
    const std::vector<BuildMenuEntry>& active_entries() const noexcept {
        return tab_entries_[active_tab_];
    }
    const BuildMenuEntry* selected_entry() const noexcept;
    int  max_scroll() const noexcept;
    void clamp_scroll();
    int  row_screen_y(int i) const noexcept;
    void switch_tab(int tab);

    bool scrollbar_geom(Rect& up, Rect& down, Rect& track, Rect& thumb) const;

    void render_scrollbar(SDL_Renderer* r) const;
    void render_list(SDL_Renderer* r, Font* font) const;
    void render_preview(SDL_Renderer* r) const;
    void render_info(SDL_Renderer* r, Font* font) const;
    void render_bottom_buttons(SDL_Renderer* r, Font* font) const;

    static void blit_skin(SDL_Renderer* r, const SkinSprite& s, int ox, int oy,
                          int frame = 0, int nframes = 1);
};

}  // namespace opente::ui
