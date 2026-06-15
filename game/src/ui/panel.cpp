#include "ui/panel.h"

#include <algorithm>

#include "render/font.h"
#include "render/font_cache.h"

namespace opente::ui {

// ---------------------------------------------------------------------------
// Panel
// ---------------------------------------------------------------------------

void Panel::add_child(std::unique_ptr<Widget> child, Rect local_rect) {
    Entry entry{std::move(child), local_rect};
    place_child(entry);
    children_.push_back(std::move(entry));
}

void Panel::place_child(Entry& entry) const {
    Rect abs{
        bounds_.x + entry.local_rect.x,
        bounds_.y + entry.local_rect.y,
        entry.local_rect.w,
        entry.local_rect.h,
    };
    entry.widget->layout(abs);
}

void Panel::layout(Rect bounds) {
    bounds_ = bounds;
    for (auto& entry : children_) {
        place_child(entry);
    }
}

void Panel::render(SDL_Renderer* renderer, FontCache& fonts) const {
    if (bg_.a > 0) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, bg_.r, bg_.g, bg_.b, bg_.a);
        SDL_Rect r = bounds_.to_sdl();
        SDL_RenderFillRect(renderer, &r);
    }
    for (const auto& entry : children_) {
        entry.widget->render(renderer, fonts);
    }
}

bool Panel::handle_event(const SDL_Event& e) {
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (it->widget->handle_event(e)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Label
// ---------------------------------------------------------------------------

void Label::render(SDL_Renderer* renderer, FontCache& fonts) const {
    Font* font = fonts.ui();
    if (font && !text_.empty()) {
        const int text_w = font->measure_text(text_.c_str());
        const int text_h = font->ascender() + font->descender();
        const int dy = (bounds_.h - text_h) / 2;
        const int baseline_y = bounds_.y + dy + font->ascender();

        SDL_Rect clip = bounds_.to_sdl();
        SDL_RenderSetClipRect(renderer, &clip);
        font->draw_text(renderer, text_.c_str(), bounds_.x + 4, baseline_y, color_);
        SDL_RenderSetClipRect(renderer, nullptr);

        (void)text_w;  // used above for clipping; suppress unused warning
    } else if (!text_.empty()) {
        // Placeholder: a dim line so the layout is still visible.
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, color_.r, color_.g, color_.b, 60);
        SDL_Rect r{bounds_.x + 4, bounds_.y + bounds_.h / 2 - 2, bounds_.w - 8, 4};
        SDL_RenderFillRect(renderer, &r);
    }
}

// ---------------------------------------------------------------------------
// Button
// ---------------------------------------------------------------------------

void Button::layout(Rect bounds) {
    bounds_ = bounds;
    label_widget_.set_text(label_);
    label_widget_.layout(bounds);
}

void Button::render(SDL_Renderer* renderer, FontCache& fonts) const {
    const SDL_Color& bg = pressed_ ? bg_pressed_ : (hovered_ ? bg_hover_ : bg_normal_);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, bg.r, bg.g, bg.b, bg.a);
    SDL_Rect r = bounds_.to_sdl();
    SDL_RenderFillRect(renderer, &r);

    SDL_SetRenderDrawColor(renderer, 120, 120, 160, 180);
    SDL_RenderDrawRect(renderer, &r);

    label_widget_.render(renderer, fonts);
}

bool Button::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEMOTION) {
        hovered_ = bounds_.contains(e.motion.x, e.motion.y);
        return false;
    }
    if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
        if (bounds_.contains(e.button.x, e.button.y)) {
            pressed_ = true;
            hovered_ = true;
            return true;
        }
    }
    if (e.type == SDL_MOUSEBUTTONUP && e.button.button == SDL_BUTTON_LEFT) {
        const bool was_pressed = pressed_;
        pressed_ = false;
        if (was_pressed && bounds_.contains(e.button.x, e.button.y)) {
            if (on_click_) on_click_();
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// ScrollPanel
// ---------------------------------------------------------------------------

void ScrollPanel::layout(Rect bounds) {
    bounds_ = bounds;
    if (content_) {
        content_->layout({bounds_.x, bounds_.y - scroll_offset_, bounds_.w, content_height_});
    }
}

void ScrollPanel::render(SDL_Renderer* renderer, FontCache& fonts) const {
    SDL_Rect clip = bounds_.to_sdl();
    SDL_RenderSetClipRect(renderer, &clip);
    if (content_) content_->render(renderer, fonts);
    SDL_RenderSetClipRect(renderer, nullptr);

    if (content_height_ > bounds_.h) {
        const int track_x = bounds_.x + bounds_.w - 6;
        const int track_h = bounds_.h;

        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 40, 40, 60, 180);
        SDL_Rect track{track_x, bounds_.y, 6, track_h};
        SDL_RenderFillRect(renderer, &track);

        const float ratio = static_cast<float>(bounds_.h) / static_cast<float>(content_height_);
        const int thumb_h = std::max(20, static_cast<int>(track_h * ratio));
        const int thumb_y = bounds_.y + static_cast<int>((track_h - thumb_h) *
                                static_cast<float>(scroll_offset_) /
                                static_cast<float>(content_height_ - bounds_.h));
        SDL_SetRenderDrawColor(renderer, 120, 120, 160, 200);
        SDL_Rect thumb{track_x, thumb_y, 6, thumb_h};
        SDL_RenderFillRect(renderer, &thumb);
    }
}

bool ScrollPanel::handle_event(const SDL_Event& e) {
    if (e.type == SDL_MOUSEWHEEL && bounds_.contains(
            SDL_max(0, e.wheel.mouseX), SDL_max(0, e.wheel.mouseY))) {
        scroll_offset_ -= e.wheel.y * kScrollStep;
        clamp_scroll();
        if (content_) {
            content_->layout({bounds_.x, bounds_.y - scroll_offset_, bounds_.w, content_height_});
        }
        return true;
    }
    if (content_) return content_->handle_event(e);
    return false;
}

void ScrollPanel::scroll_to_show(int top, int bottom) {
    if (top < scroll_offset_) {
        scroll_offset_ = top;
    } else if (bottom > scroll_offset_ + bounds_.h) {
        scroll_offset_ = bottom - bounds_.h;
    }
    clamp_scroll();
    if (content_) {
        content_->layout({bounds_.x, bounds_.y - scroll_offset_, bounds_.w, content_height_});
    }
}

void ScrollPanel::clamp_scroll() {
    const int max_scroll = std::max(0, content_height_ - bounds_.h);
    scroll_offset_ = std::max(0, std::min(scroll_offset_, max_scroll));
}

}  // namespace opente::ui
