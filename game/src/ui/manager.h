#pragma once

#include <filesystem>
#include <memory>
#include <vector>

#include <SDL.h>

#include "render/font.h"
#include "render/font_cache.h"
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
    /// Points the shared font cache at `game_data_dir / "fonts"`. Atlases are
    /// loaded lazily on first request. Returns false only on a hard SDL
    /// initialisation failure; missing atlases are non-fatal (text falls back
    /// to placeholder rects).
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

    /// Default UI font (Clean 12pt). May be null if the atlas is missing.
    render::BitmapFont* font() noexcept { return fonts_.ui(); }

    /// The shared font cache, for callers that need a specific face/size.
    render::FontCache& fonts() noexcept { return fonts_; }

    bool has_open_dialogs() const noexcept { return !stack_.empty(); }

private:
    struct Entry {
        std::unique_ptr<Widget> widget;
        bool modal = false;
    };

    SDL_Renderer* renderer_ = nullptr;
    render::FontCache fonts_;
    std::vector<Entry> stack_;
};

}  // namespace opente::ui
