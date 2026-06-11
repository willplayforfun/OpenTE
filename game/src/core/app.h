#pragma once

#include <SDL.h>

#include <filesystem>

#include "render/texture.h"

namespace opente::core {

/// Top-level application: owns the SDL window/renderer, runs the main loop,
/// and dispatches input events.
///
/// This is the toolchain-spike version of the app: it loads a single sprite
/// from the extractor's `game_data/` output and lets the player move it
/// around with arrow keys or by clicking. As real systems (world, sim, ui)
/// are built, App will own/coordinate those subsystems instead of holding a
/// single sprite directly.
class App {
public:
    /// `executable_path` is argv[0], used to locate `game_data/` relative to
    /// the running binary. Returns false if SDL/window/renderer setup fails.
    bool init(const std::filesystem::path& executable_path);

    /// Runs the main loop until the window is closed or Escape is pressed.
    /// Returns the process exit code.
    int run();

    ~App();

private:
    void handle_event(const SDL_Event& event);
    void update();
    void render();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    render::Texture sprite_;

    bool running_ = false;

    // Top-left position of the sprite, in window pixels. Moved by arrow
    // keys or by clicking (the sprite re-centers on the click point).
    int sprite_x_ = 100;
    int sprite_y_ = 100;
};

}  // namespace opente::core
