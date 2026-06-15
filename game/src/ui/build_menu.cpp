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

void draw_centred(SDL_Renderer* r, Font* f, const Rect& rect,
                  const char* text, SDL_Color c) {
    if (!f || !text || !text[0]) return;
    const int tw = f->measure_text(text);
    const int th = f->ascender() + f->descender();
    const int x  = rect.x + (rect.w - tw) / 2;
    const int y  = rect.y + (rect.h - th) / 2 + f->ascender();
    f->draw_text(r, text, x, y, c);
}

void draw_left(SDL_Renderer* r, Font* f,
               int x, int row_y, int row_h,
               const char* text, SDL_Color c) {
    if (!f || !text || !text[0]) return;
    const int y = row_y + (row_h - (f->ascender() + f->descender())) / 2 + f->ascender();
    f->draw_text(r, text, x, y, c);
}

void draw_right(SDL_Renderer* r, Font* f,
                int rect_x, int rect_w, int row_y, int row_h,
                const char* text, SDL_Color c) {
    if (!f || !text || !text[0]) return;
    const int tw = f->measure_text(text);
    const int x  = rect_x + rect_w - tw - 4;
    const int y  = row_y + (row_h - (f->ascender() + f->descender())) / 2 + f->ascender();
    f->draw_text(r, text, x, y, c);
}

}  // namespace

// ---------------------------------------------------------------------------

void BuildMenu::blit_skin(SDL_Renderer* r, const SkinSprite& s, int ox, int oy) {
    if (!s.valid()) return;
    const SDL_Rect dst{ox + s.anchor_x, oy + s.anchor_y, s.w, s.h};
    SDL_RenderCopy(r, s.tex, nullptr, &dst);
}

// ---------------------------------------------------------------------------
// Constructor
// ---------------------------------------------------------------------------

BuildMenu::BuildMenu(const std::map<std::string, data::Building>& buildings,
                     OnSelectFn on_select,
                     ConsSkin skin)
    : on_select_(std::move(on_select)), skin_(std::move(skin)) {

    // PATHWAYS tab (index 0) — fixed list of transport types.
    for (const auto& pw : kPathways) {
        tab_entries_[0].push_back({pw.id, pw.label, "", 0, true});
    }

    // Building tabs (indices 1-4) — filtered by Building::type.
    for (const auto& [id, bldg] : buildings) {
        int tab = -1;
        if      (bldg.type == "mark")              tab = 1;
        else if (bldg.type == "bdep" ||
                 bldg.type == "ware")              tab = 2;
        else if (bldg.type == "bpro")              tab = 3;
        else if (bldg.type == "bdem")              tab = 4;
        if (tab < 0) continue;

        tab_entries_[tab].push_back({id, bldg.name, bldg.desc, bldg.build_cost, false});
    }

    for (int t = 1; t < kNumTabs; ++t) {
        std::sort(tab_entries_[t].begin(), tab_entries_[t].end(),
                  [](const ListEntry& a, const ListEntry& b) {
                      return a.label < b.label;
                  });
    }
}

// ---------------------------------------------------------------------------
// layout
// ---------------------------------------------------------------------------

void BuildMenu::layout(Rect /*window_bounds*/) {
    const int panel_w = skin_.valid() ? skin_.background.w  : kFallbackWidth;
    const int panel_h = skin_.valid() ? skin_.background.h  : kFallbackHeight;

    menu_rect_ = {8, 8, panel_w, panel_h};
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

const BuildMenu::ListEntry* BuildMenu::selected_entry() const noexcept {
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

void BuildMenu::render_scrollbar(SDL_Renderer* r) const {
    const int content = static_cast<int>(active_entries().size()) * kRowHeight;
    if (content <= list_rect_.h) return;

    const int tx = list_rect_.x + list_rect_.w + 2;
    const int th = list_rect_.h;
    sdl_fill(r, {tx, list_rect_.y, 4, th}, 30, 30, 50, 160);

    const float ratio  = static_cast<float>(th) / static_cast<float>(content);
    const int thumb_h  = std::max(16, static_cast<int>(th * ratio));
    const int max_s    = max_scroll();
    const int thumb_y  = max_s > 0
        ? list_rect_.y + static_cast<int>((th - thumb_h) *
                         static_cast<float>(scroll_offset_) / max_s)
        : list_rect_.y;
    sdl_fill(r, {tx, thumb_y, 4, thumb_h}, 110, 110, 150, 200);
}

void BuildMenu::render_list(SDL_Renderer* r, Font* font) const {
    SDL_Rect clip = list_rect_.to_sdl();
    SDL_RenderSetClipRect(r, &clip);

    const auto& entries = active_entries();
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

        const SDL_Color tc = (i == selected_row_)
            ? SDL_Color{40, 38, 20, 255}                 // dark on green bar
            : SDL_Color{205, 192, 140, 230};

        draw_left(r, font, list_rect_.x + 6, sy, kRowHeight,
                  entries[i].label.c_str(), tc);

        if (entries[i].cost > 0) {
            const std::string cs = std::to_string(entries[i].cost) + " coins";
            draw_right(r, font, list_rect_.x, row_w,
                       sy, kRowHeight, cs.c_str(),
                       (i == selected_row_) ? SDL_Color{50, 45, 25, 230}
                                            : SDL_Color{180, 168, 110, 200});
        }
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
    if (!skin_.valid()) {
        sdl_fill(r, info_rect_, 18, 18, 22, 210);
        sdl_outline(r, info_rect_, 60, 58, 45, 140);
    }

    const ListEntry* sel = selected_entry();
    if (!sel || !font) return;

    const int mx = menu_rect_.x, my = menu_rect_.y;
    const int lh   = font->ascender() + font->descender();
    const int step = lh + 3;

    // Title: the building name, centred in the 'desc' label widget which sits
    // ABOVE the body box (original: label +0xa80 @ y=461; body +0x900 @ y=486,
    // with an engraved groove between — the "divider line").
    const Rect title_rc{mx + kDLblX, my + kDLblY, kDLblW, kDLblH};
    draw_centred(r, font, title_rc, sel->label.c_str(),
                 SDL_Color{240, 225, 160, 255});

    // Divider between the title and the body text.
    const int div_y = my + kDescY - 2;
    SDL_SetRenderDrawColor(r, 90, 80, 55, 200);
    SDL_RenderDrawLine(r, mx + kDescX + 6, div_y,
                       mx + kDescX + kDescW - 6, div_y);

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
        font->draw_text(r, s.c_str(), box_cx - tw / 2, y, c);
        y += step;
    };

    if (sel->cost > 0) {
        draw_c(std::to_string(sel->cost) + " coins",
               SDL_Color{205, 195, 145, 230});
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
                    draw_c(line, SDL_Color{175, 162, 115, 200});
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
        draw_c(line, SDL_Color{175, 162, 115, 200});
    }
}

void BuildMenu::render_bottom_buttons(SDL_Renderer* r, Font* font) const {
    const bool can_confirm = !selected_id_.empty();

    // Confirm.
    if (skin_.valid() && skin_.confirm_btn.valid()) {
        if (!can_confirm)
            SDL_SetTextureColorMod(skin_.confirm_btn.tex, 80, 80, 80);
        blit_skin(r, skin_.confirm_btn, dialog_ox_, dialog_oy_);
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
        const ListEntry* sel = selected_entry();
        if (sel && sel->cost > 0)
            label += " (" + std::to_string(sel->cost) + " coins)";
        draw_left(r, font, conf_rect_.x + 46, conf_rect_.y, conf_rect_.h,
                  label.c_str(),
                  can_confirm ? SDL_Color{220, 215, 160, 255}
                              : SDL_Color{120, 115, 90, 180});
    }

    // Cancel / exit.
    if (skin_.valid() && skin_.cancel_btn.valid()) {
        blit_skin(r, skin_.cancel_btn, dialog_ox_, dialog_oy_);
    } else {
        sdl_fill(r, canc_rect_, 55, 35, 35, 200);
    }
    draw_left(r, font, canc_rect_.x + 46, canc_rect_.y, canc_rect_.h,
              "EXIT CONSTRUCTION MODE", SDL_Color{200, 180, 140, 220});
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
                     "CONSTRUCTION", SDL_Color{220, 205, 150, 255});
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
        draw_centred(r, f_title, title, "CONSTRUCTION",
                     SDL_Color{225, 210, 155, 255});
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

        draw_centred(r, f_cat, tr, kTabs[i].label,
                     act ? SDL_Color{255, 245, 160, 255}
                         : SDL_Color{200, 188, 140, 220});
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

        if (list_rect_.contains(mx, my)) {
            const int idx = (my - list_rect_.y + scroll_offset_) / kRowHeight;
            const auto& entries = active_entries();
            if (idx >= 0 && idx < static_cast<int>(entries.size())) {
                selected_row_ = idx;
                selected_id_  = entries[idx].id;
            }
            return true;
        }

        if (!selected_id_.empty() && conf_rect_.contains(mx, my)) {
            if (on_select_) on_select_(selected_id_);
            return true;
        }

        if (canc_rect_.contains(mx, my)) {
            if (on_select_) on_select_("");
            return true;
        }

        return true;
    }

    return false;
}

}  // namespace opente::ui
