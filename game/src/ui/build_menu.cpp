#include "ui/build_menu.h"

#include <algorithm>
#include <string>

#include "render/font.h"
#include "render/font_cache.h"

namespace opente::ui {

namespace {

inline void sdl_fill(SDL_Renderer* r, const Rect& rect,
                     Uint8 red, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, red, g, b, a);
    SDL_Rect sr = rect.to_sdl();
    SDL_RenderFillRect(r, &sr);
}

inline void sdl_outline(SDL_Renderer* r, const Rect& rect,
                        Uint8 red, Uint8 g, Uint8 b, Uint8 a) {
    SDL_SetRenderDrawColor(r, red, g, b, a);
    SDL_Rect sr = rect.to_sdl();
    SDL_RenderDrawRect(r, &sr);
}

// All UI text in the original is drawn with a 1px drop shadow; route every
// text draw in this panel through the shadowed path. (Black, offset (1,1) —
// tune kTextShadow / the offset if a screenshot shows otherwise.)
constexpr SDL_Color kTextShadow{0, 0, 0, 255};

// Exact text colours from the original (layout fn 0x424940 -> 0x56d4b0).
// The colour literal is encoded 0x00BBGGRR (red in the LOW byte), confirmed
// from the RGB565 converter 0x56e470 (byte[0] -> red channel).
//   title 'titl'               = 0x0042c1fc -> (252,193,66)  golden yellow
//   categories / button labels = 0x0075bfde -> (222,191,117) tan/gold
//   building list / confirm    = 0x00ffffff -> white
//   panel/desc box/cancel icon = 0x80000008  (high bit = "no solid fill,
//     sprite-backed" sentinel, NOT a palette colour — these draw via sprites/
//     the stone art; it drives no readable text.)
// The description body/name text is coloured dynamically on selection (not a
// static layout constant); it's white in-game.
constexpr SDL_Color kColTitle{252, 193, 66, 255};   // #FCC142
constexpr SDL_Color kColCat{222, 191, 117, 255};    // #DEBF75
constexpr SDL_Color kColWhite{255, 255, 255, 255};

void draw_centred(SDL_Renderer* r, Font* f, const Rect& rect,
                  const char* text, SDL_Color c) {
    if (!f || !text || !text[0]) return;
    const int tw = f->measure_text(text);
    const int th = f->ascender() + f->descender();
    const int x  = rect.x + (rect.w - tw) / 2;
    const int y  = rect.y + (rect.h - th) / 2 + f->ascender();
    f->draw_text_shadowed(r, text, x, y, c, kTextShadow);
}

void draw_left(SDL_Renderer* r, Font* f,
               int x, int row_y, int row_h,
               const char* text, SDL_Color c) {
    if (!f || !text || !text[0]) return;
    const int y = row_y + (row_h - (f->ascender() + f->descender())) / 2 + f->ascender();
    f->draw_text_shadowed(r, text, x, y, c, kTextShadow);
}

void draw_right(SDL_Renderer* r, Font* f,
                int rect_x, int rect_w, int row_y, int row_h,
                const char* text, SDL_Color c) {
    if (!f || !text || !text[0]) return;
    const int tw = f->measure_text(text);
    const int x  = rect_x + rect_w - tw - 4;
    const int y  = row_y + (row_h - (f->ascender() + f->descender())) / 2 + f->ascender();
    f->draw_text_shadowed(r, text, x, y, c, kTextShadow);
}

}  // namespace

// ---------------------------------------------------------------------------

void BuildMenu::blit_skin(SDL_Renderer* r, const SkinSprite& s, int ox, int oy,
                          int frame, int nframes) {
    if (!s.valid()) return;
    if (nframes < 1) nframes = 1;
    const int fw = s.w / nframes;
    const SDL_Rect src{frame * fw, 0, fw, s.h};
    const SDL_Rect dst{ox + s.anchor_x, oy + s.anchor_y, fw, s.h};
    SDL_RenderCopy(r, s.tex, &src, &dst);
}

// ---------------------------------------------------------------------------
// Constructor / data setters
// ---------------------------------------------------------------------------

BuildMenu::BuildMenu(ConsSkin skin) : skin_(std::move(skin)) {}

void BuildMenu::set_data(const BuildMenuData& data) {
    for (int t = 0; t < kNumTabs; ++t)
        tab_entries_[t] = data.tabs[t];
    // Reset selection state when data changes.
    selected_id_.clear();
    selected_row_  = -1;
    scroll_offset_ = 0;
    hover_row_     = -1;
}

void BuildMenu::set_confirm_visible(bool visible) {
    confirm_visible_ = visible;
}

void BuildMenu::set_construction_mode_active(bool active) {
    construction_mode_active_ = active;
}

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------

void BuildMenu::layout(Rect window_bounds) {
    const int panel_w = skin_.valid() ? skin_.background.w  : kFallbackWidth;
    const int panel_h = skin_.valid() ? skin_.background.h  : kFallbackHeight;

    // RE source: toolbar-re.md Phase 2 — bart (top bar) at y=0, h=20 in the
    // original 1024×768. The panel sits flush below it: x=0 (left edge),
    // y=20. See also HudBars::kTopBarH in hud.h.
    static constexpr int kHudTopH = 20;
    menu_rect_ = {0, kHudTopH, panel_w, panel_h};
    bounds_    = menu_rect_;

    if (skin_.valid()) {
        dialog_ox_ = menu_rect_.x - skin_.background.anchor_x;
        dialog_oy_ = menu_rect_.y - skin_.background.anchor_y;
    }

    const int mx = menu_rect_.x, my = menu_rect_.y;

    // Five category rows (thin 20px text rows), order = kTabs.
    for (int i = 0; i < kNumTabs; ++i) {
        tab_rects_[i] = {mx + kCatX, my + kCatY[i], kCatW, kCatH};
    }

    list_rect_    = {mx + kListX, my + kListY, kListW, kListH};
    preview_rect_ = {mx + kPrevX, my + kPrevY, kPrevW, kPrevH};
    info_rect_    = {mx + kDescX, my + kDescY, kDescW, kDescH};
    conf_rect_    = {mx + kBtnX,  my + kConfY, kBtnW, kBtnH};
    canc_rect_    = {mx + kBtnX,  my + kCancY, kBtnW, kBtnH};

    if (!skin_.valid()) {
        bg_panel_ = std::make_unique<Panel>(SDL_Color{20, 22, 35, 235});
        bg_panel_->layout(menu_rect_);
    }

    scroll_offset_ = 0;
    hover_row_     = -1;
    selected_id_.clear();
    selected_row_  = -1;
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

const BuildMenuEntry* BuildMenu::selected_entry() const noexcept {
    if (selected_row_ < 0) return nullptr;
    const auto& e = active_entries();
    if (selected_row_ >= static_cast<int>(e.size())) return nullptr;
    return &e[selected_row_];
}

int BuildMenu::row_screen_y(int i) const noexcept {
    return list_rect_.y + i * kRowHeight - scroll_offset_;
}

int BuildMenu::max_scroll() const noexcept {
    const int content = static_cast<int>(active_entries().size()) * kRowHeight;
    return std::max(0, content - list_rect_.h);
}

void BuildMenu::clamp_scroll() {
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll()));
}

void BuildMenu::switch_tab(int tab) {
    active_tab_    = tab;
    selected_id_.clear();
    selected_row_  = -1;
    scroll_offset_ = 0;
    hover_row_     = -1;
}

// ---------------------------------------------------------------------------
// Rendering helpers
// ---------------------------------------------------------------------------

// Computes the scrollbar sub-rects. The 16px column sits at the list's right
// edge (the original list reserves this gutter to the right of its 240px row
// content). Returns true if the content overflows (thumb is movable).
bool BuildMenu::scrollbar_geom(Rect& up, Rect& down, Rect& track,
                               Rect& thumb) const {
    const int bx = list_rect_.x + list_rect_.w - kScrollW;
    up   = {bx, list_rect_.y, kScrollW, kArrowH};
    down = {bx, list_rect_.y + list_rect_.h - kArrowH, kScrollW, kArrowH};
    const int track_y = up.y + up.h;
    const int track_h = down.y - track_y;
    track = {bx, track_y, kScrollW, track_h};

    const int content = static_cast<int>(active_entries().size()) * kRowHeight;
    const int max_s   = max_scroll();
    if (content <= list_rect_.h || max_s <= 0 || track_h <= 0) {
        thumb = track;             // not scrollable: thumb fills the track
        return false;
    }
    int thumb_h = std::max(kThumbMinH,
        static_cast<int>(static_cast<float>(track_h) * list_rect_.h / content));
    thumb_h = std::min(thumb_h, track_h);
    const int thumb_y = track_y + static_cast<int>(
        (track_h - thumb_h) * static_cast<float>(scroll_offset_) / max_s);
    thumb = {bx, thumb_y, kScrollW, thumb_h};
    return true;
}

void BuildMenu::render_scrollbar(SDL_Renderer* r) const {
    Rect up, down, track, thumb;
    const bool scrollable = scrollbar_geom(up, down, track, thumb);

    if (!(skin_.valid() && skin_.scrollbar.valid())) {
        // Fallback (no skin): simple flat bar.
        sdl_fill(r, track, 30, 30, 50, 160);
        if (scrollable) sdl_fill(r, thumb, 110, 110, 150, 200);
        return;
    }

    SDL_Texture* tex = skin_.scrollbar.tex;
    const int sh = skin_.scrollbar.h;   // 120

    // Blit a 16xh source slice of vscr (srcY, srcH) into dst.
    auto blit = [&](int srcY, int srcH, const Rect& dst) {
        const SDL_Rect src{0, srcY, kScrollW, srcH};
        const SDL_Rect d = dst.to_sdl();
        SDL_RenderCopy(r, tex, &src, &d);
    };
    // Fill [y0,y1) at x by tiling a kSliceH-tall source slice (3-part stretch).
    auto fill_tiled = [&](int srcY, int x, int y0, int y1) {
        for (int y = y0; y < y1; y += kSliceH)
            blit(srcY, kSliceH, {x, y, kScrollW, std::min(kSliceH, y1 - y)});
    };

    // Track (background bar): tiled groove slice down the full track.
    fill_tiled(kTrackTile, track.x, track.y, track.y + track.h);

    // Slider (foreground thumb): 3-part vertical — top cap, tiled middle,
    // bottom cap (each a 4px source slice).
    if (scrollable && thumb.h >= 2 * kSliceH) {
        blit(kSlidTop, kSliceH, {thumb.x, thumb.y, kScrollW, kSliceH});
        fill_tiled(kSlidMid, thumb.x, thumb.y + kSliceH, thumb.y + thumb.h - kSliceH);
        blit(kSlidBot, kSliceH, {thumb.x, thumb.y + thumb.h - kSliceH, kScrollW, kSliceH});
    } else if (scrollable) {
        blit(kSlidMid, kSliceH, thumb);
    }

    // Arrow buttons: up = top 16px of vscr, down = bottom 16px.
    blit(kVscrUp, kArrowH, up);
    blit(sh - kArrowH, kArrowH, down);
}

void BuildMenu::render_list(SDL_Renderer* r, Font* font) const {
    SDL_Rect clip = list_rect_.to_sdl();
    SDL_RenderSetClipRect(r, &clip);

    const auto& entries = active_entries();  // std::vector<BuildMenuEntry>
    for (int i = 0; i < static_cast<int>(entries.size()); ++i) {
        const int sy = row_screen_y(i);
        if (sy + kRowHeight <= list_rect_.y) continue;
        if (sy >= list_rect_.y + list_rect_.h)   break;

        // Row highlight uses the green textured 'sele' sprite (240x20),
        // the game's one selection bar (cons.sele). The list rows are this
        // sprite's width, narrower than the list frame.
        const int row_w = (skin_.valid() && skin_.selection.valid())
                              ? skin_.selection.w : list_rect_.w;

        if (i == selected_row_) {
            if (skin_.valid() && skin_.selection.valid()) {
                const int vy = sy + (kRowHeight - skin_.selection.h) / 2;
                const SDL_Rect dst{list_rect_.x, vy,
                                   skin_.selection.w, skin_.selection.h};
                SDL_RenderCopy(r, skin_.selection.tex, nullptr, &dst);
            } else {
                sdl_fill(r, {list_rect_.x, sy, row_w, kRowHeight},
                         60, 80, 30, 200);
            }
        } else if (i == hover_row_) {
            sdl_fill(r, {list_rect_.x, sy, row_w, kRowHeight}, 50, 55, 30, 90);
        }

        const SDL_Color tc = kColWhite;   // list rows are white in the original

        std::string cs = entries[i].label.c_str();
        if (entries[i].cost > 0) {
            cs = cs + " " + std::to_string(entries[i].cost) + " coins";
        }
        draw_left(r, font, list_rect_.x + 6, sy, kRowHeight, cs.c_str(), tc);
    }

    SDL_RenderSetClipRect(r, nullptr);
    render_scrollbar(r);
}

void BuildMenu::render_preview(SDL_Renderer* r) const {
    // The recessed preview frame is part of the cons.back art; don't paint a
    // synthetic box over it. (Building sprite preview not yet wired.) Only the
    // fallback no-skin path gets a placeholder box.
    if (!skin_.valid()) {
        sdl_fill(r, preview_rect_, 20, 20, 25, 180);
        sdl_outline(r, preview_rect_, 60, 58, 45, 140);
    }
}

void BuildMenu::render_info(SDL_Renderer* r, Font* font) const {

    const BuildMenuEntry* sel = selected_entry();
    if (!sel || !font) return;

    const int mx = menu_rect_.x, my = menu_rect_.y;
    const int lh   = font->ascender() + font->descender();
    const int step = lh + 3;

    // Title: the building name, centred in the 'desc' label widget which sits
    // ABOVE the body box (original: label +0xa80 @ y=461; body +0x900 @ y=486,
    // with an engraved groove between — the "divider line").
    const Rect title_rc{mx + kDLblX, my + kDLblY, kDLblW, kDLblH};
    draw_centred(r, font, title_rc, sel->label.c_str(), kColWhite);

    // Body: cost + wrapped description, all centred. The 'text' child widget
    // (+0x9c0) is parented to the desc box at relative (0,0) with width 225,
    // so the column is the box's left edge .. +225 — centre lines within THAT
    // column, not the 272 frame (centring on the frame ran the text too wide).
    const int box_cx  = info_rect_.x + kDescContentW / 2;
    const int max_w   = kDescContentW;
    const int bottom  = info_rect_.y + info_rect_.h;
    int y = info_rect_.y + 6 + font->ascender();

    auto draw_c = [&](const std::string& s, SDL_Color c) {
        if (s.empty() || y >= bottom) return;
        const int tw = font->measure_text(s.c_str());
        font->draw_text_shadowed(r, s.c_str(), box_cx - tw / 2, y, c, kTextShadow);
        y += step;
    };

    if (sel->cost > 0) {
        draw_c("Cost: " + std::to_string(sel->cost), kColWhite);
        y += 2;
    }

    if (!sel->desc.empty()) {
        const char* p = sel->desc.c_str();
        std::string word, line;
        while (true) {
            const char ch = *p ? *p++ : '\0';
            if (ch == ' ' || ch == '\0') {
                const std::string trial = line.empty() ? word : line + " " + word;
                if (!word.empty() &&
                    font->measure_text(trial.c_str()) > max_w &&
                    !line.empty()) {
                    draw_c(line, kColWhite);
                    line = word;
                } else {
                    line = trial;
                }
                word.clear();
                if (ch == '\0') break;
            } else {
                word += ch;
            }
        }
        draw_c(line, kColWhite);
    }
}

void BuildMenu::render_bottom_buttons(SDL_Renderer* r, Font* font) const {
    const bool can_confirm = confirm_visible_;

    // Confirm. The sprite is two 32px frames (normal | pressed); draw one.
    if (skin_.valid() && skin_.confirm_btn.valid()) {
        if (!can_confirm)
            SDL_SetTextureColorMod(skin_.confirm_btn.tex, 80, 80, 80);
        blit_skin(r, skin_.confirm_btn, dialog_ox_, dialog_oy_,
                  pressed_btn_ == 1 ? 1 : 0, 2);
        if (!can_confirm)
            SDL_SetTextureColorMod(skin_.confirm_btn.tex, 255, 255, 255);
    } else {
        sdl_fill(r, conf_rect_,
                 can_confirm ? 40 : 55,
                 can_confirm ? 80 : 55,
                 can_confirm ? 40 : 55, 220);
    }
    {
        std::string label = "CONFIRM";
        const BuildMenuEntry* sel = selected_entry();
        if (sel && sel->cost > 0)
            label += " (" + std::to_string(sel->cost) + " coins)";
        draw_left(r, font, conf_rect_.x + 46, conf_rect_.y, conf_rect_.h,
                  label.c_str(),
                  can_confirm ? kColCat : SDL_Color{120, 115, 90, 180});
    }

    // Cancel / exit. Same two-frame sprite layout.
    if (skin_.valid() && skin_.cancel_btn.valid()) {
        blit_skin(r, skin_.cancel_btn, dialog_ox_, dialog_oy_,
                  pressed_btn_ == 2 ? 1 : 0, 2);
    } else {
        sdl_fill(r, canc_rect_, 55, 35, 35, 200);
    }
    draw_left(r, font, canc_rect_.x + 46, canc_rect_.y, canc_rect_.h,
              "EXIT CONSTRUCTION MODE", kColCat);
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------

void BuildMenu::render(SDL_Renderer* r, FontCache& fonts) const {
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);

    // Per-element fonts, matching the originals set in the layout fn 0x424940
    // (font-spec strings passed to the text-style setter 0x56d4b0):
    //   title 'titl'      -> font/seri/11
    //   category rows     -> font/seri/9
    //   building list     -> font/clea/10
    //   description       -> font/clea/10
    //   conf/canc labels  -> font/seri/10
    Font* f_title = fonts.get("seri", 11);
    Font* f_cat   = fonts.get("seri", 9);
    Font* f_list  = fonts.get("clea", 10);
    Font* f_desc  = fonts.get("clea", 10);
    Font* f_btn   = fonts.get("seri", 10);

    // Background.
    if (skin_.valid()) {
        const SDL_Rect dst{menu_rect_.x, menu_rect_.y,
                           skin_.background.w, skin_.background.h};
        SDL_RenderCopy(r, skin_.background.tex, nullptr, &dst);
    } else {
        if (bg_panel_) bg_panel_->render(r, fonts);
        sdl_fill(r, {menu_rect_.x, menu_rect_.y, menu_rect_.w, kTitleH},
                 30, 34, 52, 255);
        draw_centred(r, f_title,
                     {menu_rect_.x, menu_rect_.y, menu_rect_.w, kTitleH},
                     "CONSTRUCTION", kColTitle);
        SDL_SetRenderDrawColor(r, 80, 80, 120, 200);
        SDL_RenderDrawLine(r,
                           menu_rect_.x, menu_rect_.y + kTitleH,
                           menu_rect_.x + menu_rect_.w - 1, menu_rect_.y + kTitleH);
    }

    // Title text (drawn into the 'titl' rect when running with the skin;
    // the fallback path already drew its own header above).
    if (skin_.valid()) {
        const Rect title{menu_rect_.x + kTitleX, menu_rect_.y + kTitleY,
                         kTitleW, kTitleH};
        draw_centred(r, f_title, title, "CONSTRUCTION", kColTitle);
    }

    // Five category rows — thin text rows; the active row gets the 'sele'
    // highlight bar (cons.sele is exactly 240x20 and aligns to this column).
    for (int i = 0; i < kNumTabs; ++i) {
        const Rect& tr  = tab_rects_[i];
        const bool  act = (i == active_tab_);

        if (act) {
            if (skin_.valid() && skin_.selection.valid()) {
                const int vy = tr.y + (tr.h - skin_.selection.h) / 2;
                const SDL_Rect dst{tr.x, vy,
                                   skin_.selection.w, skin_.selection.h};
                SDL_RenderCopy(r, skin_.selection.tex, nullptr, &dst);
            } else {
                sdl_fill(r, tr, 60, 80, 30, 200);
            }
        }

        // Original draws all category rows in the same tan; the active row is
        // indicated by the green 'sele' bar, not a brighter text colour.
        draw_centred(r, f_cat, tr, kTabs[i].label, kColCat);
    }

    render_list(r, f_list);
    render_preview(r);
    render_info(r, f_desc);
    render_bottom_buttons(r, f_btn);
}

// ---------------------------------------------------------------------------
// handle_event
// ---------------------------------------------------------------------------

bool BuildMenu::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (!list_rect_.contains(mx, my)) return false;
        scroll_offset_ -= e.wheel.y * kScrollStep;
        clamp_scroll();
        return true;
    }

    if (e.type == SDL_MOUSEMOTION) {
        const int mx = e.motion.x, my = e.motion.y;

        if (dragging_thumb_) {
            Rect up, down, track, thumb;
            if (scrollbar_geom(up, down, track, thumb)) {
                const int span = track.h - thumb.h;
                if (span > 0) {
                    const float frac = static_cast<float>(my - drag_dy_ - track.y) / span;
                    scroll_offset_ = static_cast<int>(frac * max_scroll() + 0.5f);
                    clamp_scroll();
                }
            }
            return true;
        }

        hover_row_ = -1;
        if (list_rect_.contains(mx, my)) {
            const int idx = (my - list_rect_.y + scroll_offset_) / kRowHeight;
            if (idx >= 0 && idx < static_cast<int>(active_entries().size()))
                hover_row_ = idx;
        }
        return bounds_.contains(mx, my);
    }

    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        const int mx = e.button.x, my = e.button.y;
        if (!bounds_.contains(mx, my)) return false;

        for (int i = 0; i < kNumTabs; ++i) {
            if (tab_rects_[i].contains(mx, my)) {
                switch_tab(i);
                return true;
            }
        }

        // Scrollbar (right edge of the list) — checked before list rows since
        // the 16px bar column overlaps the list rect.
        {
            Rect up, down, track, thumb;
            const bool scrollable = scrollbar_geom(up, down, track, thumb);
            if (up.contains(mx, my)) {
                pressed_arrow_ = 1;
                scroll_offset_ -= kRowHeight;
                clamp_scroll();
                return true;
            }
            if (down.contains(mx, my)) {
                pressed_arrow_ = 2;
                scroll_offset_ += kRowHeight;
                clamp_scroll();
                return true;
            }
            if (scrollable && thumb.contains(mx, my)) {
                dragging_thumb_ = true;
                drag_dy_ = my - thumb.y;
                return true;
            }
            if (scrollable && track.contains(mx, my)) {
                // Page up/down depending on which side of the thumb was hit.
                scroll_offset_ += (my < thumb.y) ? -list_rect_.h : list_rect_.h;
                clamp_scroll();
                return true;
            }
        }

        if (list_rect_.contains(mx, my)) {
            const int idx = (my - list_rect_.y + scroll_offset_) / kRowHeight;
            const auto& entries = active_entries();
            if (idx >= 0 && idx < static_cast<int>(entries.size())) {
                selected_row_ = idx;
                selected_id_  = entries[idx].id;
                if (on_item_selected) on_item_selected(selected_id_);
            }
            return true;
        }

        // Buttons: press on down (show the pressed frame), fire on release.
        if (confirm_visible_ && conf_rect_.contains(mx, my)) {
            pressed_btn_ = 1;
            return true;
        }
        if (canc_rect_.contains(mx, my)) {
            pressed_btn_ = 2;
            return true;
        }

        return true;
    }

    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const int mx = e.button.x, my = e.button.y;
        if (dragging_thumb_ || pressed_arrow_) {
            dragging_thumb_ = false;
            pressed_arrow_  = 0;
            return true;
        }
        const int was = pressed_btn_;
        pressed_btn_ = 0;
        // Clear state before invoking the callback: it may close (delete) this
        // widget, so no member access is allowed afterwards.
        if (was == 1 && confirm_visible_ && conf_rect_.contains(mx, my)) {
            if (on_confirm_clicked) on_confirm_clicked();
            return true;
        }
        if (was == 2 && canc_rect_.contains(mx, my)) {
            if (on_exit_clicked) on_exit_clicked();
            return true;
        }
        return was != 0;
    }

    return false;
}

}  // namespace opente::ui
