#pragma once

#include <SDL.h>

#include <filesystem>
#include <optional>

#include "data/registry.h"
#include "render/camera.h"
#include "render/texture.h"
#include "world/world.h"

namespace opente::core {

/// Top-level application: owns the SDL window/renderer, the loaded data and
/// world, and the camera; runs the main loop and dispatches input events.
///
/// Stage 1: loads `game_data/`, the `ep01_china` map, and renders its
/// terrain/decorations/HQ building isometrically with arrow-key pan and
/// mouse-wheel zoom. No simulation yet.
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
    void update(float dt_seconds);
    void render();

    void render_terrain();
    void render_decorations();
    void render_buildings();

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    std::optional<data::DataRegistry> registry_;
    std::optional<world::World> world_;
    render::Camera camera_;
    render::Texture hq_sprite_;

    bool running_ = false;

    // Held-key state for continuous (per-frame) camera panning.
    bool pan_left_ = false;
    bool pan_right_ = false;
    bool pan_up_ = false;
    bool pan_down_ = false;
};

}  // namespace opente::core
