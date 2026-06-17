#include "ui/manager.h"

#include <SDL_log.h>
#include <algorithm>

namespace opente::ui {

bool UIManager::init(SDL_Renderer* renderer, const std::filesystem::path& game_data_dir) {
    renderer_ = renderer;
    fonts_.init(renderer, game_data_dir / "fonts");
    return true;
}

void UIManager::shutdown() {
    stack_.clear();
    hud_.reset();
    fonts_.clear();
}

void UIManager::set_hud(std::unique_ptr<Widget> hud, int window_w, int window_h) {
    hud_ = std::move(hud);
    if (hud_) hud_->layout(Rect{0, 0, window_w, window_h});
}

void UIManager::open(std::unique_ptr<Widget> dialog, int window_w, int window_h) {
    dialog->layout(Rect{0, 0, window_w, window_h});
    stack_.push_back({std::move(dialog), false});
}

void UIManager::open_modal(std::unique_ptr<Widget> dialog, int window_w, int window_h) {
    dialog->layout(Rect{0, 0, window_w, window_h});
    stack_.push_back({std::move(dialog), true});
}

void UIManager::close(Widget* dialog) {
    auto it = std::find_if(stack_.begin(), stack_.end(),
                           [dialog](const Entry& e) { return e.widget.get() == dialog; });
    if (it != stack_.end()) stack_.erase(it);
}

bool UIManager::handle_event(const SDL_Event& e) {
    // A HUD overlay (e.g. an open dropdown) sits above everything, so it gets
    // first crack at events.
    const bool hud_priority = hud_ && hud_->wants_event_priority();
    if (hud_priority && hud_->handle_event(e)) return true;

    // Dialogs are above the HUD in z-order, so they get next crack at events.
    if (!stack_.empty()) {
        Entry& top = stack_.back();
        const bool consumed = top.widget->handle_event(e);
        if (top.modal) return true;
        if (consumed)  return true;
    }
    // HUD buttons receive events only when no dialog consumed them (and only if
    // the HUD didn't already get a priority pass above).
    if (hud_ && !hud_priority) return hud_->handle_event(e);
    return false;
}

void UIManager::render() {
    // HUD bars are below the dialog stack...
    if (hud_) hud_->render(renderer_, fonts_);
    for (const auto& entry : stack_) {
        entry.widget->render(renderer_, fonts_);
    }
    // ...but a HUD overlay (open dropdown) draws on top of the whole stack.
    if (hud_) hud_->render_overlay(renderer_, fonts_);
}

}  // namespace opente::ui
