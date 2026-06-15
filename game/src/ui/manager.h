#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <SDL.h>

#include "render/font.h"
#include "ui/widget.h"

namespace opente::ui {

/// Owns the widget stack (modal/non-modal dialogs) and the bitmap UI font.
///
/// Usage in the main loop:
///   - App::init()   → ui_manager_.init(renderer, game_data_dir)
///   - Event loop    → if (!ui_manager_.handle_event(e)) { /* game handles e */ }
///   - Render loop   → ui_manager_.render()   (after game world, before SDL_RenderPresent)
///   - App::~App()   → ui_manager_.shutdown()
class UIManager {
public:
    /// Loads the bitmap UI font from `game_data_dir / "fonts" / "clea_12pt.json"`.
    /// Returns false only on a hard SDL initialisation failure; a missing font
    /// is non-fatal (text falls back to placeholder rects).
    bool init(SDL_Renderer* renderer, const std::filesystem::path& game_data_dir);

    void shutdown();

    /// Pushes a non-modal dialog.
    void open(std::unique_ptr<Widget> dialog, int window_w, int window_h);

    /// Pushes a modal dialog. All events are consumed until close() is called.
    void open_modal(std::unique_ptr<Widget> dialog, int window_w, int window_h);

    /// Removes `dialog` from the stack (no-op if not found).
    void close(Widget* dialog);

    /// Returns true if any widget consumed the event.
    bool handle_event(const SDL_Event& e);

    /// Renders all widgets in z-order (bottom to top).
    void render();

    render::BitmapFont* font() const noexcept { return font_.get(); }
    bool has_open_dialogs() const noexcept { return !stack_.empty(); }

private:
    struct Entry {
        std::unique_ptr<Widget> widget;
        bool modal = false;
    };

    SDL_Renderer* renderer_ = nullptr;
    std::unique_ptr<render::BitmapFont> font_;
    std::vector<Entry> stack_;
};

}  // namespace opente::ui
