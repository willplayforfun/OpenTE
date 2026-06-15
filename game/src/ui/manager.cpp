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
    fonts_.clear();
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
    if (stack_.empty()) return false;
    Entry& top = stack_.back();
    const bool consumed = top.widget->handle_event(e);
    if (top.modal) return true;
    return consumed;
}

void UIManager::render() {
    for (const auto& entry : stack_) {
        entry.widget->render(renderer_, fonts_);
    }
}

}  // namespace opente::ui
