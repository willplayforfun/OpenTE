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
    static const std::array<const char*, 12> ids = {
        "ui.a_ui.tool.topb",  "ui.a_ui.tool.botb",
        "ui.a_ui.tool.play",  "ui.a_ui.tool.rout",  "ui.a_ui.tool.cons",
        "ui.a_ui.tool.tech",  "ui.a_ui.tool.terr",  "ui.a_ui.tool.regi",
        "ui.a_ui.tool.wmap",  "ui.a_ui.tool.game",
        "ui.a_ui.stdc.coin",  "ui.a_ui.stdc.down",
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
    // (barb-local rects, RE-verified)
    buttons_ = {
        {"play", "ui.a_ui.tool.play", 28,  14, 35, 33, false, true,  "Merchants"},
        {"rout", "ui.a_ui.tool.rout", 84,  14, 35, 33, false, true,  "Routes"},
        {"cons", "ui.a_ui.tool.cons", 141, 14, 35, 33, false, true,  "Build"},
        {"tech", "ui.a_ui.tool.tech", 197, 14, 35, 33, false, true,  "Technologies"},
        {"terr", "ui.a_ui.tool.terr", 765, 12, 35, 33, true,  false, "Terrain View"},
        {"regi", "ui.a_ui.tool.regi", 816, 12, 36, 33, true,  false, "Region Overview"},
        {"worl", "ui.a_ui.tool.wmap", 866, 12, 35, 33, true,  false, "World Overview"},
        {"game", "ui.a_ui.tool.game", 966, 12, 35, 33, true,  false, "Main Menu"},
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

    // Tooltip in the bottom-bar center area.
    const char* tip_cstr = nullptr;
    if (hovered_btn_ >= 0 && hovered_btn_ < static_cast<int>(buttons_.size()))
        tip_cstr = buttons_[hovered_btn_].tooltip;
    const std::string& tip = tip_cstr ? std::string(tip_cstr) : tooltip_;
    if (!tip.empty()) {
        // hot3 rect from toolbar-re.md Phase 3: x=271, y=12, w=482, h=29
        // (barb-local, i.e. relative to barb's top-left).
        const Rect tip_box = {sx_left(271), bottom_y() + 12, 482, 29};
        draw_label(renderer, fonts, tip, tip_box, kText, /*center_h=*/true);
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

    // Speed cluster: arrow + plain text "Game Speed: x1".
    if (down) blit(renderer, *down, 0, 22, {sx_left(146), top_y() + 1, 22, 18});
    draw_label(renderer, fonts, "Game Speed: " + speed_,
               {sx_left(172), top_y() + 2, 250, 18}, kText);

    // Region name cluster: arrow + plain region name.
    if (down) blit(renderer, *down, 0, 22, {sx_left(429), top_y() + 1, 22, 18});
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

bool HudBars::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        hovered_btn_ = -1;
        for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
            if (button_rect(buttons_[i]).contains(e.motion.x, e.motion.y)) {
                hovered_btn_ = i;
                break;
            }
        }
        return false;  // don't consume — camera hover still needs motion
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        for (int i = 0; i < static_cast<int>(buttons_.size()); ++i) {
            if (button_rect(buttons_[i]).contains(e.button.x, e.button.y)) {
                pressed_btn_ = i;
                return true;
            }
        }
        return top_bar_rect().contains(e.button.x, e.button.y) ||
               bottom_bar_rect().contains(e.button.x, e.button.y);
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
