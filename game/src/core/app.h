#pragma once

#include <SDL.h>

#include <filesystem>
#include <optional>

#include "core/scene_manager.h"
#include "data/registry.h"

// Forward-declare to avoid pulling in the full header here.
namespace opente::ui { class MainMenuScene; }

namespace opente::core {

/// Application shell: owns the SDL window/renderer, the shared DataRegistry,
/// and the SceneManager. Runs the main loop and routes raw SDL events.
///
/// Scenes own everything screen-specific (input state, camera, UI widgets,
/// sprites). App passes durable resources (DataRegistry) into scenes and
/// ferries cross-scene data (high scores, player setup) between them when
/// transitions occur.
class App {
public:
    bool init(const std::filesystem::path& executable_path);
    int  run();
    ~App();

private:
    void render();

    // SDL resources declared before scene_manager_ so they outlive it.
    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    std::optional<data::DataRegistry> registry_;

    // scene_manager_ declared last so it's destroyed first (before the
    // renderer), ensuring all scene-owned textures are freed in time.
    SceneManager scene_manager_;

    // Non-owning pointer valid while MainMenuScene is the active scene.
    // Nulled before any scene transition away from the main menu.
    ui::MainMenuScene* main_menu_ = nullptr;

    bool running_ = false;
};

}  // namespace opente::core
