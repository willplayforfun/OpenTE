#include "ui/manager.h"

#include <SDL_log.h>
#include <algorithm>

namespace opente::ui {

bool UIManager::init(SDL_Renderer* renderer, const std::filesystem::path& game_data_dir) {
    renderer_ = renderer;

    const std::filesystem::path font_path =
        game_data_dir / "fonts" / "clea_12pt.json";

    font_ = render::BitmapFont::load(renderer, font_path);
    if (!font_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "UIManager: bitmap font not found at '%s' — "
                    "run the OpenTE extractor first to generate game_data/fonts/. "
                    "UI text will use placeholder rects.",
                    font_path.string().c_str());
    }

    return true;
}

void UIManager::shutdown() {
    stack_.clear();
    font_.reset();
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
        entry.widget->render(renderer_, font_.get());
    }
}

}  // namespace opente::ui
