#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <SDL.h>
#include <SDL_ttf.h>

#include "ui/widget.h"

namespace opente::ui {

/// Owns the widget stack (modal/non-modal dialogs) and the SDL_ttf font.
///
/// Usage in the main loop:
///   - App::init()   → ui_manager_.init(renderer, assets_dir)
///   - Event loop    → if (!ui_manager_.handle_event(e)) { /* game handles e */ }
///   - Render loop   → ui_manager_.render()   (after game world, before SDL_RenderPresent)
///   - App::~App()   → ui_manager_.shutdown()
///
/// Dialogs are pushed onto the stack with open() and removed with close().
/// handle_event() dispatches only to the topmost widget; if it's modal (see
/// open_modal()), events are not forwarded to widgets below it or to the game.
class UIManager {
public:
    /// Initialises SDL_ttf and attempts to load the UI font from
    /// `assets_dir / "fonts" / "ui.ttf"`.  Returns false only on a hard
    /// SDL_ttf initialisation failure; a missing font is non-fatal (text
    /// falls back to placeholder rects).
    bool init(SDL_Renderer* renderer, const std::filesystem::path& assets_dir);

    void shutdown();

    /// Pushes a non-modal dialog.  Events not consumed by this widget still
    /// reach widgets below it and the game world.
    void open(std::unique_ptr<Widget> dialog, int window_w, int window_h);

    /// Pushes a modal dialog.  All events are consumed by this widget (or
    /// discarded) until close() is called on it.
    void open_modal(std::unique_ptr<Widget> dialog, int window_w, int window_h);

    /// Removes `dialog` from the stack (no-op if not found).
    void close(Widget* dialog);

    /// Returns true if any widget on the stack consumed the event.
    bool handle_event(const SDL_Event& e);

    /// Renders all widgets in z-order (bottom to top).
    void render();

    TTF_Font* font() const noexcept { return font_; }
    bool has_open_dialogs() const noexcept { return !stack_.empty(); }

private:
    struct Entry {
        std::unique_ptr<Widget> widget;
        bool modal = false;
    };

    SDL_Renderer* renderer_ = nullptr;
    TTF_Font* font_ = nullptr;
    std::vector<Entry> stack_;
};

}  // namespace opente::ui
