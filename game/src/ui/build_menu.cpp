#include "ui/build_menu.h"

#include <algorithm>
#include <string>

namespace opente::ui {

namespace {

const char* category_display_name(const std::string& cat) {
    // Human-readable names verified from TStructurePanelView @ 0x424940.
    if (cat == "mark") return "Markets";
    if (cat == "depo") return "Depots";
    if (cat == "prod") return "Production";
    if (cat == "dema") return "Demand";
    if (cat == "bldg") return "Buildings";
    if (cat == "path") return "Pathways";
    return cat.c_str();
}

// Render one SkinSprite at its dialog-relative position.
inline void blit_skin(SDL_Renderer* r, const SkinSprite& s, int ox, int oy) {
    if (!s.valid()) return;
    const SDL_Rect dst{ox + s.anchor_x, oy + s.anchor_y, s.w, s.h};
    SDL_RenderCopy(r, s.tex, nullptr, &dst);
}

// Render a SkinSprite stretched to a custom rect.
inline void blit_skin_rect(SDL_Renderer* r, const SkinSprite& s, const SDL_Rect& dst) {
    if (!s.valid()) return;
    SDL_RenderCopy(r, s.tex, nullptr, &dst);
}

}  // namespace

// ---------------------------------------------------------------------------
// BuildMenu
// ---------------------------------------------------------------------------

BuildMenu::BuildMenu(const std::map<std::string, data::Building>& buildings,
                     OnSelectFn on_select,
                     ConsSkin skin)
    : on_select_(std::move(on_select)), skin_(std::move(skin)) {
    // Group buildings by category, sort each group alphabetically by name.
    std::map<std::string, std::vector<std::pair<std::string, const data::Building*>>> by_cat;
    for (const auto& [id, bldg] : buildings) {
        by_cat[bldg.category].emplace_back(id, &bldg);
    }
    for (auto& [cat, vec] : by_cat) {
        std::sort(vec.begin(), vec.end(),
                  [](const auto& a, const auto& b) {
                      return a.second->name < b.second->name;
                  });
        entries_.push_back({true, category_display_name(cat), "", 0});
        for (const auto& [id, bldg] : vec) {
            entries_.push_back({false, bldg->name, id, bldg->build_cost});
        }
    }
}

void BuildMenu::layout(Rect /*window_bounds*/) {
    int menu_w, menu_h;
    int list_x_off, list_y_off, list_w, list_h;

    if (skin_.valid()) {
        // Use the sprite background's native size.
        // All layout derived from manifest anchor values:
        //   back   anchor=(1,17)  -> defines dialog origin
        //   sele   anchor=(15,82) -> list top-left within dialog
        //   conf   anchor=(24,635)-> list bottom (button top)
        //   canc   anchor=(25,665)-> cancel button below confirm
        menu_w = skin_.background.w;
        menu_h = skin_.background.h;

        // Dialog origin relative to menu top-left (which is where back is drawn):
        //   dialog_origin = (menu_x - back.anchor_x, menu_y - back.anchor_y)
        // List top-left in screen coords:
        //   screen_x = dialog_origin.x + sele.anchor_x = menu_x - back.ax + sele.ax
        //   screen_y = dialog_origin.y + sele.anchor_y = menu_y - back.ay + sele.ay
        list_x_off = skin_.selection.anchor_x - skin_.background.anchor_x;  // 15-1=14
        list_y_off = skin_.selection.anchor_y - skin_.background.anchor_y;  // 82-17=65
        list_w     = skin_.selection.w - 6;  // leave 6px for scrollbar track
        // List bottom aligns with top of confirm button.
        const int list_bot_off = skin_.confirm_btn.anchor_y - skin_.background.anchor_y;  // 635-17=618
        list_h = list_bot_off - list_y_off;  // 618-65=553
    } else {
        menu_w = kFallbackWidth;
        menu_h = kFallbackHeight;
        list_x_off = kPadding;
        list_y_off = 32;  // title height
        list_w     = menu_w - kPadding * 2 - 8;
        list_h     = menu_h - list_y_off - kPadding;
    }

    menu_rect_ = {kPadding, kPadding, menu_w, menu_h};
    bounds_    = menu_rect_;

    // Dialog origin in screen space.
    if (skin_.valid()) {
        dialog_ox_ = menu_rect_.x - skin_.background.anchor_x;
        dialog_oy_ = menu_rect_.y - skin_.background.anchor_y;
    }

    list_rect_ = {
        menu_rect_.x + list_x_off,
        menu_rect_.y + list_y_off,
        list_w,
        list_h,
    };

    // Build the title label; for the skinned case, place it in the header
    // area of the background sprite (above list_rect_).
    {
        const int title_h = list_y_off > 0 ? list_y_off : 32;
        title_label_ = std::make_unique<Label>("Build Menu", SDL_Color{220, 205, 150, 255});
        title_label_->layout({menu_rect_.x, menu_rect_.y, menu_w, title_h});
    }

    // Fallback flat-colour background (only when no skin).
    if (!skin_.valid()) {
        bg_panel_ = std::make_unique<Panel>(SDL_Color{20, 22, 35, 235});
        bg_panel_->layout(menu_rect_);
    }

    // Build row widgets from entries_.
    rows_.clear();
    int y = 0;
    for (const auto& entry : entries_) {
        const int row_h = entry.is_header ? kHeaderHeight : kRowHeight;

        if (entry.is_header) {
            auto lbl = std::make_unique<Label>(
                entry.label, SDL_Color{180, 160, 100, 255});
            rows_.push_back({y, row_h, std::move(lbl)});
        } else {
            const std::string bid = entry.building_id;
            std::string btn_text  = entry.label;
            if (entry.cost > 0)
                btn_text += "  [" + std::to_string(entry.cost) + "]";

            // Transparent normal-bg: the sele sprite provides the hover visual.
            auto btn = std::make_unique<Button>(
                std::move(btn_text),
                [this, bid]() { if (on_select_) on_select_(bid); },
                SDL_Color{0,  0,  0,  0},    // normal  — transparent (sele sprite used)
                SDL_Color{60, 60, 90, 80},   // hover   — subtle tint fallback
                SDL_Color{35, 35, 55, 200});  // pressed
            rows_.push_back({y, row_h, std::move(btn)});
        }
        y += row_h + 1;
    }
    content_height_ = y;
    scroll_offset_  = 0;
    hover_row_      = -1;

    for (auto& row : rows_) {
        row.widget->layout(row_abs_rect(row));
    }
}

Rect BuildMenu::row_abs_rect(const RowWidget& row) const noexcept {
    return {
        list_rect_.x,
        list_rect_.y + row.local_y - scroll_offset_,
        list_rect_.w,
        row.local_h,
    };
}

int BuildMenu::max_scroll() const noexcept {
    return std::max(0, content_height_ - list_rect_.h);
}

void BuildMenu::clamp_scroll() {
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll()));
}

void BuildMenu::render_scrollbar(SDL_Renderer* renderer) const {
    if (content_height_ <= list_rect_.h) return;

    const int track_x = list_rect_.x + list_rect_.w + 1;
    const int track_y = list_rect_.y;
    const int track_h = list_rect_.h;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 30, 30, 50, 180);
    SDL_Rect track{track_x, track_y, 5, track_h};
    SDL_RenderFillRect(renderer, &track);

    const float ratio   = static_cast<float>(track_h) / static_cast<float>(content_height_);
    const int thumb_h   = std::max(16, static_cast<int>(track_h * ratio));
    const int max_s     = max_scroll();
    const int thumb_y   = track_y + static_cast<int>(
        (track_h - thumb_h) * static_cast<float>(scroll_offset_) / static_cast<float>(max_s));

    SDL_SetRenderDrawColor(renderer, 110, 110, 150, 200);
    SDL_Rect thumb{track_x, thumb_y, 5, thumb_h};
    SDL_RenderFillRect(renderer, &thumb);
}

void BuildMenu::render(SDL_Renderer* renderer, Font* font) const {
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);

    if (skin_.valid()) {
        // ---- Skinned rendering ----
        // Background frame at native size, top-left at menu_rect_.{x,y}.
        {
            const SDL_Rect dst{menu_rect_.x, menu_rect_.y,
                               skin_.background.w, skin_.background.h};
            SDL_RenderCopy(renderer, skin_.background.tex, nullptr, &dst);
        }

        // Title label (positioned in the area above the list).
        if (title_label_) {
            title_label_->render(renderer, font);
        }

        // Scrollable list.
        SDL_Rect clip = list_rect_.to_sdl();
        SDL_RenderSetClipRect(renderer, &clip);

        // cons.sele sprite drawn behind the hovered row (stretched to list width).
        if (hover_row_ >= 0 && hover_row_ < static_cast<int>(rows_.size())) {
            const Rect abs = row_abs_rect(rows_[hover_row_]);
            const int sy = abs.y + (abs.h - skin_.selection.h) / 2;
            const SDL_Rect sele_dst{list_rect_.x, sy, list_rect_.w, skin_.selection.h};
            blit_skin_rect(renderer, skin_.selection, sele_dst);
        }

        // Render each visible row widget.
        for (const auto& row : rows_) {
            const Rect abs = row_abs_rect(row);
            if (abs.y + abs.h <= list_rect_.y) continue;
            if (abs.y >= list_rect_.y + list_rect_.h) break;

            // Category header: dim stripe.
            if (row.local_h == kHeaderHeight) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 80);
                SDL_Rect hbg = abs.to_sdl();
                SDL_RenderFillRect(renderer, &hbg);
            }
            row.widget->render(renderer, font);
        }

        SDL_RenderSetClipRect(renderer, nullptr);

        // Confirm and cancel buttons at their manifest-defined positions.
        blit_skin(renderer, skin_.confirm_btn, dialog_ox_, dialog_oy_);
        blit_skin(renderer, skin_.cancel_btn,  dialog_ox_, dialog_oy_);

    } else {
        // ---- Fallback flat-colour rendering ----
        if (bg_panel_)    bg_panel_->render(renderer, font);

        // Title bar.
        SDL_SetRenderDrawColor(renderer, 30, 34, 52, 255);
        SDL_Rect title_bg{menu_rect_.x, menu_rect_.y, menu_rect_.w, 32};
        SDL_RenderFillRect(renderer, &title_bg);
        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 200);
        SDL_RenderDrawLine(renderer,
                           menu_rect_.x, menu_rect_.y + 32,
                           menu_rect_.x + menu_rect_.w, menu_rect_.y + 32);
        if (title_label_) title_label_->render(renderer, font);

        SDL_Rect clip = list_rect_.to_sdl();
        SDL_RenderSetClipRect(renderer, &clip);

        for (const auto& row : rows_) {
            const Rect abs = row_abs_rect(row);
            if (abs.y + abs.h <= list_rect_.y) continue;
            if (abs.y >= list_rect_.y + list_rect_.h) break;

            if (row.local_h == kHeaderHeight) {
                SDL_SetRenderDrawColor(renderer, 35, 38, 55, 255);
                SDL_Rect hbg = abs.to_sdl();
                SDL_RenderFillRect(renderer, &hbg);
            }
            row.widget->render(renderer, font);
        }

        SDL_RenderSetClipRect(renderer, nullptr);

        // Outer border.
        SDL_SetRenderDrawColor(renderer, 80, 80, 120, 200);
        SDL_Rect border = menu_rect_.to_sdl();
        SDL_RenderDrawRect(renderer, &border);
    }

    render_scrollbar(renderer);
}

bool BuildMenu::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (!bounds_.contains(mx, my)) return false;
        scroll_offset_ -= e.wheel.y * kScrollStep;
        clamp_scroll();
        for (auto& row : rows_) row.widget->layout(row_abs_rect(row));
        return true;
    }

    if (e.type == SDL_MOUSEMOTION ||
        e.type == SDL_MOUSEBUTTONDOWN ||
        e.type == SDL_MOUSEBUTTONUP) {

        // Track which row is being hovered (for sele sprite positioning).
        if (e.type == SDL_MOUSEMOTION) {
            hover_row_ = -1;
            if (list_rect_.contains(e.motion.x, e.motion.y)) {
                for (int i = 0; i < static_cast<int>(rows_.size()); ++i) {
                    const Rect abs = row_abs_rect(rows_[i]);
                    if (abs.contains(e.motion.x, e.motion.y)) {
                        hover_row_ = i;
                        break;
                    }
                }
            }
        }

        // Dispatch to rows in reverse order (topmost first).
        for (auto it = rows_.rbegin(); it != rows_.rend(); ++it) {
            if (it->widget->handle_event(e)) return true;
        }

        // Absorb clicks inside the panel even if no row consumed them.
        // Also handle clicks on confirm/cancel buttons when skinned.
        if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            if (bounds_.contains(mx, my)) {
                // Cancel button click: close the menu via on_select("").
                if (skin_.valid()) {
                    const int cx = dialog_ox_ + skin_.cancel_btn.anchor_x;
                    const int cy = dialog_oy_ + skin_.cancel_btn.anchor_y;
                    const Rect canc_r{cx, cy, skin_.cancel_btn.w, skin_.cancel_btn.h};
                    if (canc_r.contains(mx, my)) {
                        if (on_select_) on_select_("");
                        return true;
                    }
                }
                return true;
            }
        }
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            if (bounds_.contains(mx, my)) return true;
        }
    }

    return false;
}

}  // namespace opente::ui
