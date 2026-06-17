#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>

#include "render/texture.h"
#include "ui/widget.h"

namespace opente::data { class DataRegistry; }

namespace opente::ui {

/// Always-visible HUD chrome: the top info bar (`bart`) and the bottom toolbar
/// (`barb`), rendered from the original game's own sprites.
///
/// Every rect below is taken verbatim from the disassembly of the layout
/// constructor `TSilkRoadView` @ 0x53e120 / 0x53e330 — see
/// `documentation/toolbar-re.md` and `extracted/toolbar_rects_raw.txt`. The
/// original game is authored against a fixed 1024×768 canvas, which is also
/// OpenTE's default window size, so the numbers are used as-is. On a resized
/// window the two bars stretch to the window width, left-anchored widgets stay
/// pinned to the left edge and right-anchored widgets to the right edge, so the
/// layout stays pixel-exact at 1024 and degrades gracefully elsewhere.
///
/// Sprites (all from `a_ui,6.{}`, manifest ids `ui.a_ui.<group>.<leaf>`):
///   bart            tool.topb   1024×20   top bar background
///   barb            tool.botb   1024×54   bottom bar background
///   8 mode buttons  tool.<id>   70×33     2-frame strip (35×33 per frame)
///   center strip    tool.butt   370×15    hotkey band decoration
///   coin            stdc.coin   12×12     gold icon (top-right)
///   spee/rena small stdc.down   44×18     2-frame arrow button (22×18/frame)
///
/// Bottom-bar button groups:
///   Toggle group (left 4): play/rout/cons/tech — exactly one can be active at
///     a time; clicking the active button deactivates it. Frame 1 = selected.
///   Action buttons (right 4): terr/regi/worl/game — clicking fires
///     on_mode_clicked but never changes the toggle state. Always frame 0.
class HudBars : public Widget {
public:
    HudBars(SDL_Renderer* renderer, const data::DataRegistry& registry);

    void layout(Rect bounds) override;
    void render(SDL_Renderer* renderer, FontCache& fonts) const override;
    bool handle_event(const SDL_Event& e) override;

    /// Invoked when any bottom-bar button is clicked. The argument is the
    /// button's 4-letter tag: play/rout/cons/tech/terr/regi/worl/game.
    std::function<void(const std::string&)> on_mode_clicked;

    /// Sets which toggle-group button (play/rout/cons/tech) shows as selected.
    /// Pass "" to deactivate all. Has no effect on action buttons.
    void set_active_mode(std::string tag) { active_mode_ = std::move(tag); }
    const std::string& active_mode() const { return active_mode_; }

    // Top-bar live-text setters.
    void set_treasury(std::string s)    { treasury_     = std::move(s); }
    void set_speed(std::string s)       { speed_        = std::move(s); }
    void set_region_name(std::string s) { region_name_  = std::move(s); }
    void set_date(std::string s)        { date_         = std::move(s); }

    /// Text shown in the bottom-bar center area when no button is hovered.
    /// Hovering a button overrides this with the button's own label.
    void set_tooltip(std::string s) { tooltip_ = std::move(s); }

    // RE'd native canvas + bar geometry (toolbar-re.md Phase 2).
    static constexpr int kCanvasW    = 1024;
    static constexpr int kTopBarH    = 20;
    static constexpr int kBottomBarH = 55;

private:
    struct Sprite {
        render::Texture tex;
        int w = 0;
        int h = 0;
        bool valid() const { return tex.valid(); }
    };

    struct ModeButton {
        std::string tag;        // 4-letter widget tag (also the click id)
        std::string sprite_id;  // manifest sprite id (note: worl→wmap)
        int x, y, w, h;         // rect in barb-local coords (widget = ½ sprite)
        bool right_group;       // pinned to the right edge on resize
        bool is_toggle;         // true = toggle group; false = stateless action
        const char* tooltip;    // bottom-bar center hint while hovered; an
                                // embedded '\n' splits it into the original's
                                // two-line (name / description) hot1+hot2 form
    };

    const Sprite* sprite(const std::string& id) const;

    // Canvas-x → screen-x, for left- and right-anchored widgets respectively.
    int sx_left(int cx)  const { return bounds_.x + cx; }
    int sx_right(int cx) const { return bounds_.x + bounds_.w - (kCanvasW - cx); }
    int top_y()    const { return bounds_.y; }
    int bottom_y() const { return bounds_.y + bounds_.h - kBottomBarH; }

    Rect top_bar_rect()    const { return {bounds_.x, top_y(),    bounds_.w, kTopBarH}; }
    Rect bottom_bar_rect() const { return {bounds_.x, bottom_y(), bounds_.w, kBottomBarH}; }
    Rect button_rect(const ModeButton& b) const;

    void blit(SDL_Renderer* r, const Sprite& s, int frame, int frame_w,
              const Rect& dst) const;
    void draw_label(SDL_Renderer* r, FontCache& fonts, const std::string& text,
                    const Rect& box, SDL_Color color, bool center_h = false) const;

    std::map<std::string, Sprite> sprites_;
    std::vector<ModeButton>       buttons_;

    std::string active_mode_;   // "" = none active

    int hovered_btn_ = -1;
    int pressed_btn_ = -1;

    std::string treasury_    = "0";
    std::string speed_       = "x1";
    std::string region_name_;
    std::string date_        = "1666 BC";
    std::string tooltip_;
};

}  // namespace opente::ui
