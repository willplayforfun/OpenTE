#pragma once

#include <SDL.h>

#include <array>
#include <filesystem>
#include <map>
#include <optional>
#include <string>

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
    void render_terrain_edges();
    void render_decorations();
    void render_buildings();

    /// A loaded sprite plus the (pixel) anchor offset from its placement
    /// point to its top-left corner, as decoded from the original sprite
    /// leaf (`data::SpriteEntry::anchor_x/anchor_y`).
    struct AnchoredSprite {
        render::Texture texture;
        float anchor_x = 0.0f;
        float anchor_y = 0.0f;
    };

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    std::optional<data::DataRegistry> registry_;
    std::optional<world::World> world_;
    render::Camera camera_;
    render::Texture hq_sprite_;

    /// Per-`world::TerrainType` tile textures, indexed by the enum's
    /// underlying value (see `core/app.cpp`'s `kTerrainTextureIds`).
    std::array<render::Texture, 7> terrain_textures_;
    render::Texture terrain_edge_texture_;

    /// Ground-decoration sprites, keyed by `data::SpriteEntry::id`
    /// (e.g. "flor.chin.3").
    std::map<std::string, AnchoredSprite> decoration_sprites_;

    bool running_ = false;

    // Held-key state for continuous (per-frame) camera panning.
    bool pan_left_ = false;
    bool pan_right_ = false;
    bool pan_up_ = false;
    bool pan_down_ = false;
};

}  // namespace opente::core
