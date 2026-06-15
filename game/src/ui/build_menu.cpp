#include "ui/build_menu.h"

#include <algorithm>
#include <string>

namespace opente::ui {

namespace {

const char* category_display_name(const std::string& cat) {
    // Human-readable names for the 4cc category codes used by the original
    // game (TConstructionPanel sprite tags, verified via EXE analysis at
    // TStructurePanelView @ 0x424940 in Trade Empires.exe).
    if (cat == "mark") return "Markets";
    if (cat == "depo") return "Depots";
    if (cat == "prod") return "Production";
    if (cat == "dema") return "Demand";
    if (cat == "bldg") return "Buildings";
    if (cat == "path") return "Pathways";
    return cat.c_str();
}

}  // namespace

BuildMenu::BuildMenu(const std::map<std::string, data::Building>& buildings,
                     OnSelectFn on_select)
    : on_select_(std::move(on_select)) {
    // Group buildings by category, then sort alphabetically within groups.
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
    // Fixed position: top-left with a small margin.
    menu_rect_ = {kPadding, kPadding, kMenuWidth, kMenuHeight};
    bounds_ = menu_rect_;

    // Background panel.
    bg_panel_ = std::make_unique<Panel>(SDL_Color{20, 22, 35, 235});
    bg_panel_->layout(menu_rect_);

    // Title bar + separator.
    title_label_ = std::make_unique<Label>("Build Menu", SDL_Color{220, 205, 150, 255});
    title_label_->layout({menu_rect_.x, menu_rect_.y, kMenuWidth, kTitleHeight});

    // Scrollable list area sits below the title bar.
    list_rect_ = {
        menu_rect_.x,
        menu_rect_.y + kTitleHeight,
        kMenuWidth,
        kMenuHeight - kTitleHeight,
    };

    // Build row widgets.
    rows_.clear();
    int y = 0;
    for (const auto& entry : entries_) {
        const int row_h = entry.is_header ? kHeaderHeight : kRowHeight;

        if (entry.is_header) {
            auto lbl = std::make_unique<Label>(
                entry.label, SDL_Color{180, 160, 100, 255});
            rows_.push_back({y, row_h, std::move(lbl)});
        } else {
            const std::string bid  = entry.building_id;
            std::string btn_label  = entry.label;
            if (entry.cost > 0) {
                btn_label += "  [" + std::to_string(entry.cost) + "]";
            }
            auto btn = std::make_unique<Button>(
                std::move(btn_label),
                [this, bid]() {
                    if (on_select_) on_select_(bid);
                });
            rows_.push_back({y, row_h, std::move(btn)});
        }
        y += row_h + 1;
    }
    content_height_ = y;
    scroll_offset_  = 0;

    // Lay out each row widget with its initial absolute position.
    for (auto& row : rows_) {
        row.widget->layout(row_abs_rect(row));
    }
}

Rect BuildMenu::row_abs_rect(const RowWidget& row) const noexcept {
    return {
        list_rect_.x + 4,
        list_rect_.y + row.local_y - scroll_offset_,
        list_rect_.w - 10,  // leave room for scrollbar
        row.local_h,
    };
}

int BuildMenu::max_scroll() const noexcept {
    return std::max(0, content_height_ - list_rect_.h);
}

void BuildMenu::clamp_scroll() {
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll()));
}

void BuildMenu::render(SDL_Renderer* renderer, TTF_Font* font) const {
    // Background.
    bg_panel_->render(renderer, font);

    // Title area.
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 30, 34, 52, 255);
    SDL_Rect title_bg{menu_rect_.x, menu_rect_.y, kMenuWidth, kTitleHeight};
    SDL_RenderFillRect(renderer, &title_bg);
    title_label_->render(renderer, font);

    // Separator.
    SDL_SetRenderDrawColor(renderer, 80, 80, 120, 200);
    SDL_RenderDrawLine(renderer,
                       menu_rect_.x, menu_rect_.y + kTitleHeight,
                       menu_rect_.x + kMenuWidth, menu_rect_.y + kTitleHeight);

    // Clip and render the scrollable row list.
    SDL_Rect clip = list_rect_.to_sdl();
    SDL_RenderSetClipRect(renderer, &clip);

    for (const auto& row : rows_) {
        const Rect abs = row_abs_rect(row);
        // Skip rows entirely outside the clip region.
        if (abs.y + abs.h < list_rect_.y) continue;
        if (abs.y >= list_rect_.y + list_rect_.h) break;

        // Category headers: dim background stripe.
        if (row.local_h == kHeaderHeight) {
            SDL_SetRenderDrawColor(renderer, 35, 38, 55, 255);
            SDL_Rect hbg = abs.to_sdl();
            SDL_RenderFillRect(renderer, &hbg);
        }

        row.widget->render(renderer, font);
    }

    SDL_RenderSetClipRect(renderer, nullptr);

    // Scrollbar.
    if (content_height_ > list_rect_.h) {
        const int track_x = menu_rect_.x + kMenuWidth - 6;
        SDL_SetRenderDrawColor(renderer, 30, 30, 50, 200);
        SDL_Rect track{track_x, list_rect_.y, 6, list_rect_.h};
        SDL_RenderFillRect(renderer, &track);

        const float ratio = static_cast<float>(list_rect_.h) /
                            static_cast<float>(content_height_);
        const int thumb_h = std::max(20, static_cast<int>(list_rect_.h * ratio));
        const int thumb_y = list_rect_.y + static_cast<int>(
            (list_rect_.h - thumb_h) *
            (static_cast<float>(scroll_offset_) / static_cast<float>(max_scroll())));
        SDL_SetRenderDrawColor(renderer, 110, 110, 150, 210);
        SDL_Rect thumb{track_x, thumb_y, 6, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }

    // Outer border.
    SDL_SetRenderDrawColor(renderer, 80, 80, 120, 200);
    SDL_Rect border = menu_rect_.to_sdl();
    SDL_RenderDrawRect(renderer, &border);
}

bool BuildMenu::handle_event(const SDL_Event& e) {
    // Mouse-wheel scrolling.
    if (e.type == SDL_MOUSEWHEEL) {
        int mx = 0, my = 0;
        SDL_GetMouseState(&mx, &my);
        if (!bounds_.contains(mx, my)) return false;

        scroll_offset_ -= e.wheel.y * kScrollStep;
        clamp_scroll();
        // Re-layout rows with the new scroll offset.
        for (auto& row : rows_) {
            row.widget->layout(row_abs_rect(row));
        }
        return true;
    }

    // Forward other events to rows (reverse order = top-most first).
    if (e.type == SDL_MOUSEMOTION ||
        e.type == SDL_MOUSEBUTTONDOWN ||
        e.type == SDL_MOUSEBUTTONUP) {

        // Update hover/press on all rows (buttons return false for motion).
        for (auto it = rows_.rbegin(); it != rows_.rend(); ++it) {
            if (it->widget->handle_event(e)) return true;
        }

        // Absorb clicks inside the menu panel even if no row consumed them.
        if (e.type == SDL_MOUSEBUTTONDOWN || e.type == SDL_MOUSEBUTTONUP) {
            int mx = 0, my = 0;
            SDL_GetMouseState(&mx, &my);
            if (bounds_.contains(mx, my)) return true;
        }
    }

    return false;
}

}  // namespace opente::ui
