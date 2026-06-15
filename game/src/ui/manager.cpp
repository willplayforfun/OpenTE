#include "ui/manager.h"

#include <SDL_log.h>
#include <algorithm>

namespace opente::ui {

bool UIManager::init(SDL_Renderer* renderer, const std::filesystem::path& assets_dir) {
    renderer_ = renderer;

    if (TTF_Init() != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "TTF_Init failed: %s", TTF_GetError());
        return false;
    }

    // Load the UI font.  The font file must be provided by the user and is not
    // bundled with OpenTE (see docs/setup/building-and-running.md).
    // Place a compatible open-licence TTF at assets/fonts/ui.ttf next to the
    // executable, or next to the build tree during development.
    const std::filesystem::path font_path = assets_dir / "fonts" / "ui.ttf";
    if (std::filesystem::exists(font_path)) {
        font_ = TTF_OpenFont(font_path.string().c_str(), 14);
        if (!font_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "TTF_OpenFont('%s') failed: %s — UI text will use placeholder rects.",
                        font_path.string().c_str(), TTF_GetError());
        }
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "UI font not found at '%s' — UI text will use placeholder rects.  "
                    "Place a .ttf file there to enable text rendering.",
                    font_path.string().c_str());
    }

    return true;
}

void UIManager::shutdown() {
    stack_.clear();
    if (font_) {
        TTF_CloseFont(font_);
        font_ = nullptr;
    }
    TTF_Quit();
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

    // Dispatch to the topmost widget.  If it's modal, eat the event regardless.
    Entry& top = stack_.back();
    const bool consumed = top.widget->handle_event(e);
    if (top.modal) return true;  // modal: always consumed
    return consumed;
}

void UIManager::render() {
    for (const auto& entry : stack_) {
        entry.widget->render(renderer_, font_);
    }
}

}  // namespace opente::ui
