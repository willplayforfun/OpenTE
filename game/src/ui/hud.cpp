#include "ui/hud.h"

#include <array>

#include "data/registry.h"
#include "render/font.h"
#include "render/font_cache.h"

namespace opente::ui {

namespace {

// Warm off-white used for all top-bar text (matches the original's ink tone).
constexpr SDL_Color kText = {224, 210, 178, 255};

}  // namespace

HudBars::HudBars(SDL_Renderer* renderer, const data::DataRegistry& registry) {
    static const std::array<const char*, 16> ids = {
        "ui.a_ui.tool.topb",  "ui.a_ui.tool.botb",
        "ui.a_ui.tool.play",  "ui.a_ui.tool.rout",  "ui.a_ui.tool.cons",
        "ui.a_ui.tool.tech",  "ui.a_ui.tool.terr",  "ui.a_ui.tool.regi",
        "ui.a_ui.tool.wmap",  "ui.a_ui.tool.game",
        "ui.a_ui.stdc.coin",  "ui.a_ui.stdc.down",
        // Dropdown sprites (dropdown-menus-re.md §5):
        "ui.a_ui.tool.regm",  // dropdown panel frame
        "ui.a_ui.stdc.sele",  // list-item highlight row (194×17)
        "ui.a_ui.tool.rebo",  // scroll trough / divider
        "ui.a_ui.stdc.vscr",  // scrollbar thumb
    };
    const std::filesystem::path& dir = registry.game_data_dir();
    for (const data::SpriteEntry& s : registry.manifest().sprites) {
        for (const char* id : ids) {
            if (s.id == id) {
                Sprite sp;
                sp.tex = render::Texture::load(renderer, dir / s.file);
                sp.w   = s.width;
                sp.h   = s.height;
                sprites_.emplace(s.id, std::move(sp));
                break;
            }
        }
    }

    // tag, sprite_id, x, y, w, h, right_group, is_toggle, tooltip
    // (barb-local rects, RE-verified).
    //
    // Tooltips are the original game's verbatim hint strings. They are NOT in
    // text.{} (the `strings` table) — they're the EXE-default (name, hint) pairs
    // the toolbar setup loads into the hot1/hot2 center-band sub-views (the .ini
    // `*Button_Text`/`*Button_Hint` keys are dev overrides; retail uses these).
    // The toggle/control buttons show TWO lines (name + "Click to…" hint); the
    // three map buttons show only the name. We encode the second line with '\n'.
    buttons_ = {
        {"play", "ui.a_ui.tool.play", 28,  14, 35, 33, false, true,  "Merchants Button\nClick to display or hide Roster&Info panel"},
        {"rout", "ui.a_ui.tool.rout", 84,  14, 35, 33, false, true,  "Routes Button\nClick to display or hide Route panel"},
        {"cons", "ui.a_ui.tool.cons", 141, 14, 35, 33, false, true,  "Construction Button\nClick to display or hide Construction panel"},
        {"tech", "ui.a_ui.tool.tech", 197, 14, 35, 33, false, true,  "Advances Button\nClick to display or hide Advances panel"},
        {"terr", "ui.a_ui.tool.terr", 765, 12, 35, 33, true,  false, "Terrain Map Button"},
        {"regi", "ui.a_ui.tool.regi", 816, 12, 36, 33, true,  false, "Region Map Button"},
        {"worl", "ui.a_ui.tool.wmap", 866, 12, 35, 33, true,  false, "Episode Map Button"},
        {"game", "ui.a_ui.tool.game", 966, 12, 35, 33, true,  false, "Game Control Button\nClick to save, load, change options, or quit"},
    };
}

const HudBars::Sprite* HudBars::sprite(const std::string& id) const {
    auto it = sprites_.find(id);
    return (it != sprites_.end() && it->second.valid()) ? &it->second : nullptr;
}

Rect HudBars::button_rect(const ModeButton& b) const {
    const int x = b.right_group ? sx_right(b.x) : sx_left(b.x);
    return {x, bottom_y() + b.y, b.w, b.h};
}

// ── Dropdown geometry ────────────────────────────────────────────────────────
// Panel = the tool/regm.a6g frame (182×200 art drawn into the RE'd 203×200
// rect). Items are 17px rows inside the list inset (5,5,192,h). The original
// repositions the panel under its arrow on open via the (un-decoded) helper
// 0x517140; we approximate that intent by dropping it straight down from the
// arrow, just below the top bar.
namespace {
constexpr int kPanelW = 203;     // RE'd panel width
constexpr int kListX  = 5;       // list inset within the panel (RE: 5,5)
constexpr int kListY  = 5;
constexpr int kListW  = 192;
constexpr int kItemH  = 17;      // stdc/sele.a6g row height
}  // namespace

const std::vector<HudBars::SpeedOption>& HudBars::speed_options() {
    // dropdown-menus-re.md §4b — labels and item+8 codes, in menu order.
    static const std::vector<SpeedOption> opts = {
        {"PAUSE", 0x000}, {"1/2 X", 0x080}, {"1 X", 0x100},
        {"2 X",   0x200}, {"4 X",   0x400},
    };
    return opts;
}

int HudBars::popup_item_count(Popup p) const {
    if (p == Popup::Speed)  return static_cast<int>(speed_options().size());
    if (p == Popup::Region) return static_cast<int>(regions_.size());
    return 0;
}

std::string HudBars::popup_item_label(Popup p, int i) const {
    if (p == Popup::Speed)  return speed_options()[i].label;
    if (p == Popup::Region) return regions_[i];
    return {};
}

Rect HudBars::popup_panel_rect(Popup p) const {
    // Height tracks the option count — the panel is only as tall as its list
    // (the regm frame is stretched to fit).
    const int arrow_x = (p == Popup::Region) ? 429 : 146;
    const int h = popup_item_count(p) * kItemH + 2 * kListY;
    return {sx_left(arrow_x), top_y() + kTopBarH, kPanelW, h};
}

Rect HudBars::popup_list_rect(Popup p) const {
    const Rect panel = popup_panel_rect(p);
    return {panel.x + kListX, panel.y + kListY, kListW,
            popup_item_count(p) * kItemH};
}

Rect HudBars::popup_item_rect(Popup p, int i) const {
    const Rect list = popup_list_rect(p);
    return {list.x, list.y + i * kItemH, list.w, kItemH};
}

void HudBars::layout(Rect bounds) {
    bounds_ = bounds;
}

void HudBars::blit(SDL_Renderer* r, const Sprite& s, int frame, int frame_w,
                   const Rect& dst) const {
    SDL_Rect src{frame * frame_w, 0, frame_w, s.h};
    SDL_Rect d = dst.to_sdl();
    SDL_RenderCopy(r, s.tex.handle(), &src, &d);
}

void HudBars::draw_label(SDL_Renderer* r, FontCache& fonts,
                         const std::string& text, const Rect& box,
                         SDL_Color color, bool center_h) const {
    // Original game uses font/sans/10 for all bart and hot1/2/3 text widgets
    // (confirmed: SetFont call sites 0x53f7cc–0x53fa58 in the layout fn).
    Font* f = fonts.get("sans", 10);
    if (!f || text.empty()) return;
    const int tw  = f->measure_text(text.c_str());
    const int th  = f->ascender() + f->descender();
    const int dy  = (box.h - th) / 2;
    const int by  = box.y + (dy > 0 ? dy : 0) + f->ascender();
    const int x   = center_h ? box.x + (box.w - tw) / 2 : box.x + 4;

    SDL_Rect clip = box.to_sdl();
    SDL_RenderSetClipRect(r, &clip);
    f->draw_text_shadowed(r, text.c_str(), x, by, color);
    SDL_RenderSetClipRect(r, nullptr);
}

void HudBars::render(SDL_Renderer* renderer, FontCache& fonts) const {
    // ── Bottom bar (barb) ──────────────────────────────────────────────────
    if (const Sprite* botb = sprite("ui.a_ui.tool.botb")) {
        SDL_Rect d = bottom_bar_rect().to_sdl();
        SDL_RenderCopy(renderer, botb->tex.handle(), nullptr, &d);
    } else {
        SDL_SetRenderDrawColor(renderer, 48, 44, 38, 255);
        SDL_Rect d = bottom_bar_rect().to_sdl();
        SDL_RenderFillRect(renderer, &d);
    }

    // 8 mode buttons. Toggle buttons: frame 1 = selected. Action buttons: always frame 0.
    for (const ModeButton& b : buttons_) {
        const Sprite* s = sprite(b.sprite_id);
        if (!s) continue;
        const int frame = (b.is_toggle && b.tag == active_mode_) ? 1 : 0;
        blit(renderer, *s, frame, b.w, button_rect(b));
    }

    // Tooltip in the bottom-bar center area. The original draws it across the
    // hot1/hot2/hot3 sub-views (toolbar-re.md Phase 3, barb-local rects):
    //   hot1 = (287,12,450,14) top half, hot2 = (287,26,450,14) bottom half,
    //   hot3 = (271,12,482,29) full area.
    // Toggle/control buttons carry a 2-line "name\nhint" tip → name in hot1,
    // hint in hot2. Map buttons (and the idle tooltip_) are 1 line → hot3.
    const char* tip_cstr = nullptr;
    if (hovered_btn_ >= 0 && hovered_btn_ < static_cast<int>(buttons_.size()))
        tip_cstr = buttons_[hovered_btn_].tooltip;
    const std::string tip = tip_cstr ? std::string(tip_cstr) : tooltip_;
    if (!tip.empty()) {
        const std::size_t nl = tip.find('\n');
        if (nl == std::string::npos) {
            draw_label(renderer, fonts, tip,
                       {sx_left(271), bottom_y() + 12, 482, 29}, kText, /*center_h=*/true);
        } else {
            draw_label(renderer, fonts, tip.substr(0, nl),
                       {sx_left(287), bottom_y() + 12, 450, 14}, kText, /*center_h=*/true);
            draw_label(renderer, fonts, tip.substr(nl + 1),
                       {sx_left(287), bottom_y() + 26, 450, 14}, kText, /*center_h=*/true);
        }
    }

    // ── Top bar (bart) ─────────────────────────────────────────────────────
    if (const Sprite* topb = sprite("ui.a_ui.tool.topb")) {
        SDL_Rect d = top_bar_rect().to_sdl();
        SDL_RenderCopy(renderer, topb->tex.handle(), nullptr, &d);
    } else {
        SDL_SetRenderDrawColor(renderer, 48, 44, 38, 255);
        SDL_Rect d = top_bar_rect().to_sdl();
        SDL_RenderFillRect(renderer, &d);
    }

    const Sprite* down = sprite("ui.a_ui.stdc.down");   // 44×18, 2 frames 22×18
    const Sprite* coin = sprite("ui.a_ui.stdc.coin");   // 12×12

    // Date in upper left ("1666 BC").
    draw_label(renderer, fonts, date_,
               {sx_left(8), top_y() + 2, 130, 18}, kText);

    // Speed cluster: arrow (frame 1 while its popup is open) + plain text. The
    // field chrome is already part of the topb background — no sprite is drawn
    // under the text.
    if (down) blit(renderer, *down, open_popup_ == Popup::Speed ? 1 : 0, 22,
                   speed_arrow_rect());
    draw_label(renderer, fonts, "Game Speed: " + speed_,
               {sx_left(172), top_y() + 2, 250, 18}, kText);

    // Region name cluster: arrow + plain region name.
    if (down) blit(renderer, *down, open_popup_ == Popup::Region ? 1 : 0, 22,
                   region_arrow_rect());
    draw_label(renderer, fonts, region_name_,
               {sx_left(455), top_y() + 2, 300, 18}, kText);

    // Gold: coin sprite on far right, number just to its left.
    if (coin) blit(renderer, *coin, 0, coin->w,
                   {sx_right(986), top_y() + 3, 12, 12});
    if (Font* f = fonts.get("sans", 10); f && !treasury_.empty()) {
        const int tw = f->measure_text(treasury_.c_str());
        const int x  = sx_right(986) - 4 - tw;
        const int y  = top_y() + 2 + f->ascender();
        f->draw_text_shadowed(renderer, treasury_.c_str(), x, y, kText);
    }
}

void HudBars::render_overlay(SDL_Renderer* renderer, FontCache& fonts) const {
    // The open dropdown is drawn here (after the whole dialog stack) so it sits
    // above the build menu and any other panel.
    if (open_popup_ != Popup::None)
        render_popup(renderer, fonts, open_popup_);
}

void HudBars::render_popup(SDL_Renderer* r, FontCache& fonts, Popup p) const {
    const Rect panel = popup_panel_rect(p);

    // Panel frame: tool/regm.a6g. The base game draws this sprite with the
    // draw-script "@" (interpreter fcn 0x56c573 → blit 0x57f720), which scales
    // the sprite to the view rect. The original keeps the panel rect at the
    // sprite's *native height*, so "@" only ever stretches it horizontally
    // (182→203) and never vertically — that's why the baked top/bottom bevels
    // stay crisp (regm carries no 9-slice/border data; the engine's procedural
    // bevel opcodes R/E/S aren't used here). We size the panel to the option
    // list, so to honour the "never scale vertically" property we blit at
    // native vertical scale and crop the texture's middle: the top half is
    // sourced from the sprite top (keeps the top bevel), the bottom half from
    // the sprite bottom (keeps the bottom bevel). Horizontal scaling matches
    // the original.
    if (const Sprite* regm = sprite("ui.a_ui.tool.regm")) {
        SDL_Texture* tex = regm->tex.handle();
        if (panel.h >= regm->h) {
            SDL_Rect d = panel.to_sdl();
            SDL_RenderCopy(r, tex, nullptr, &d);
        } else {
            const int topH = panel.h / 2;
            const int botH = panel.h - topH;
            SDL_Rect s_top{0, 0, regm->w, topH};
            SDL_Rect d_top{panel.x, panel.y, panel.w, topH};
            SDL_RenderCopy(r, tex, &s_top, &d_top);
            SDL_Rect s_bot{0, regm->h - botH, regm->w, botH};
            SDL_Rect d_bot{panel.x, panel.y + topH, panel.w, botH};
            SDL_RenderCopy(r, tex, &s_bot, &d_bot);
        }
    } else {
        SDL_SetRenderDrawColor(r, 32, 30, 26, 240);
        SDL_Rect d = panel.to_sdl();
        SDL_RenderFillRect(r, &d);
    }

    const Sprite* sele = sprite("ui.a_ui.stdc.sele");   // 194×17 hover highlight
    const Rect list = popup_list_rect(p);
    const int count = popup_item_count(p);

    // Clip rows to the list area so an overflowing list doesn't paint past the
    // panel (a functional scrollbar — rebo/vscr — is not wired up yet).
    SDL_Rect clip = list.to_sdl();
    SDL_RenderSetClipRect(r, &clip);

    for (int i = 0; i < count; ++i) {
        const Rect row = popup_item_rect(p, i);
        const bool highlight =
            (i == hovered_item_) ||
            (p == Popup::Region && i == current_region_);
        if (highlight && sele)
            blit(r, *sele, 0, sele->w, row);
        // List-item text: font/sans/9 (dropdown-menus-re.md §3c).
        if (Font* f = fonts.get("sans", 9)) {
            const std::string label = popup_item_label(p, i);
            const int by = row.y + (row.h - (f->ascender() + f->descender())) / 2
                           + f->ascender();
            f->draw_text_shadowed(r, label.c_str(), row.x + 4, by, kText);
        }
    }

    SDL_RenderSetClipRect(r, nullptr);
}

bool HudBars::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        hovered_btn_ = -1;
        for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
            if (button_rect(buttons_[i]).contains(e.motion.x, e.motion.y)) {
                hovered_btn_ = i;
                break;
            }
        }
        // Track the hovered dropdown row for the highlight.
        hovered_item_ = -1;
        if (open_popup_ != Popup::None) {
            for (int i = 0; i < popup_item_count(open_popup_); ++i) {
                if (popup_item_rect(open_popup_, i).contains(e.motion.x, e.motion.y)) {
                    hovered_item_ = i;
                    break;
                }
            }
        }
        return false;  // don't consume — camera hover still needs motion
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const int mx = e.button.x, my = e.button.y;

        // Dropdown open-arrows toggle their popup (clicking the open one, or the
        // other arrow, behaves like the original's 'down' handler).
        if (speed_arrow_rect().contains(mx, my)) {
            open_popup_ = (open_popup_ == Popup::Speed) ? Popup::None : Popup::Speed;
            hovered_item_ = -1;
            return true;
        }
        if (region_arrow_rect().contains(mx, my)) {
            open_popup_ = (open_popup_ == Popup::Region) ? Popup::None : Popup::Region;
            hovered_item_ = -1;
            return true;
        }

        // Interactions with an open popup.
        if (open_popup_ != Popup::None) {
            const Popup p = open_popup_;
            for (int i = 0; i < popup_item_count(p); ++i) {
                if (popup_item_rect(p, i).contains(mx, my)) {
                    open_popup_   = Popup::None;
                    hovered_item_ = -1;
                    if (p == Popup::Speed) {
                        if (on_speed_selected)
                            on_speed_selected(speed_options()[i].code,
                                              speed_options()[i].label);
                    } else if (on_region_selected) {
                        on_region_selected(i);
                    }
                    return true;
                }
            }
            // Click anywhere else dismisses the popup and is absorbed — while a
            // dropdown is open it behaves modally, so the click never leaks to
            // the bars or anything beneath.
            open_popup_   = Popup::None;
            hovered_item_ = -1;
            return true;
        }

        for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
            if (button_rect(buttons_[i]).contains(mx, my)) {
                pressed_btn_ = i;
                return true;
            }
        }
        return top_bar_rect().contains(mx, my) ||
               bottom_bar_rect().contains(mx, my);
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int pressed = pressed_btn_;
        pressed_btn_ = -1;
        if (pressed >= 0 &&
            button_rect(buttons_[pressed]).contains(e.button.x, e.button.y)) {
            const ModeButton& b = buttons_[pressed];
            if (b.is_toggle) {
                // Toggle: clicking the active button deactivates it; clicking
                // any other activates it and deactivates the previous one.
                active_mode_ = (b.tag == active_mode_) ? "" : b.tag;
            }
            // Action buttons (is_toggle = false) don't change active_mode_.
            if (on_mode_clicked) on_mode_clicked(b.tag);
            return true;
        }
        if (pressed >= 0) return true;
        return top_bar_rect().contains(e.button.x, e.button.y) ||
               bottom_bar_rect().contains(e.button.x, e.button.y);
    }

    return false;
}

}  // namespace opente::ui
