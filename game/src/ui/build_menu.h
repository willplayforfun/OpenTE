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

/// Build/construction menu — replicates TConstructionPanel / TStructurePanel.
///
/// RE-confirmed structure (TStructurePanel slot[1] init @ 0x004263c0):
///   5 category tabs shown as VERTICAL stacked buttons (path first):
///     PATHWAYS, MARKETS, DEPOTS, PRODUCTION BUILDINGS, DEMAND BUILDINGS
///   Each tab has its own scrollable building list.
///   The 'path' tab shows trai/road/rail/cana transport type choices.
///   Below the list: building sprite preview, then info box (name/cost/desc).
///   Confirm (at +0x540) disabled until item selected; shows cost in label.
///   Cancel always enabled; text = "EXIT CONSTRUCTION MODE".
///
/// Building type filtering (Building::type field):
///   mark  → MARKETS tab
///   bdep/ware → DEPOTS tab
///   bpro  → PRODUCTION BUILDINGS tab
///   bdem  → DEMAND BUILDINGS tab
///
/// on_select("") = cancel/close; on_select("<id>") = building or pathway chosen.
class BuildMenu : public Widget {
public:
    using OnSelectFn = std::function<void(const std::string& id)>;

    BuildMenu(const std::map<std::string, data::Building>& buildings,
              OnSelectFn on_select,
              ConsSkin skin = {});

    void layout(Rect window_bounds) override;
    void render(SDL_Renderer* renderer, FontCache& fonts) const override;
    bool handle_event(const SDL_Event& e) override;

    static constexpr int kFallbackWidth  = 306;
    static constexpr int kFallbackHeight = 696;

private:
    // ---------------------------------------------------------------------------
    // Category definitions (RE-confirmed: path first, mark/depo/prod/dema after)
    // ---------------------------------------------------------------------------
    static constexpr int kNumTabs = 5;
    struct TabDef {
        const char* type_filter; // Building::type value(s) to match — or "path"
        const char* label;       // Display text (all-caps matches original)
    };
    static constexpr TabDef kTabs[kNumTabs] = {
        {"path", "PATHWAYS"},
        {"mark", "MARKETS"},
        {"depo", "DEPOTS"},
        {"prod", "PRODUCTION BUILDINGS"},
        {"dema", "DEMAND BUILDINGS"},
    };

    // Pathway types shown in the PATHWAYS tab (integers 0-3, confirmed by RE).
    struct PathwayEntry {
        const char* id;
        const char* label;
        int type_id;
    };
    static constexpr PathwayEntry kPathways[4] = {
        {"trai", "Trail",    0},
        {"road", "Road",     1},
        {"rail", "Railway",  2},
        {"cana", "Canal",    3},
    };

    // ---------------------------------------------------------------------------
    // List entry
    // ---------------------------------------------------------------------------
    struct ListEntry {
        std::string id;
        std::string label;
        std::string desc;
        int  cost       = 0;
        bool is_pathway = false;
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

    static constexpr int kRowHeight  = 20;   // building-list row height (= sele h)
    static constexpr int kScrollStep = kRowHeight * 2;

    // ---------------------------------------------------------------------------
    // State
    // ---------------------------------------------------------------------------
    OnSelectFn on_select_;
    ConsSkin   skin_;

    std::vector<ListEntry> tab_entries_[kNumTabs];

    int         active_tab_    = 1;  // default to MARKETS (index 1)
    std::string selected_id_;
    int         selected_row_  = -1;
    int         hover_row_     = -1;
    int         scroll_offset_ = 0;

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

    // Fallback flat background (when no skin).
    std::unique_ptr<Panel> bg_panel_;

    // ---------------------------------------------------------------------------
    // Helpers
    // ---------------------------------------------------------------------------
    const std::vector<ListEntry>& active_entries() const noexcept {
        return tab_entries_[active_tab_];
    }
    const ListEntry* selected_entry() const noexcept;
    int  max_scroll() const noexcept;
    void clamp_scroll();
    int  row_screen_y(int i) const noexcept;
    void switch_tab(int tab);

    void render_scrollbar(SDL_Renderer* r) const;
    void render_list(SDL_Renderer* r, Font* font) const;
    void render_preview(SDL_Renderer* r) const;
    void render_info(SDL_Renderer* r, Font* font) const;
    void render_bottom_buttons(SDL_Renderer* r, Font* font) const;

    static void blit_skin(SDL_Renderer* r, const SkinSprite& s, int ox, int oy);
};

}  // namespace opente::ui
