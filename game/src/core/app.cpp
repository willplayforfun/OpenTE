#include "core/app.h"

#include <SDL_image.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include "core/paths.h"
#include "gameplay/gameplay_scene.h"
#include "ui/main_menu_scene.h"

namespace opente::core {

namespace {
constexpr int         kWindowWidth  = 1024;
constexpr int         kWindowHeight = 768;
constexpr const char* kWindowTitle  = "OpenTE";
constexpr const char* kStartMapId   = "ep01_chin";
}  // namespace

bool App::init(const std::filesystem::path& executable_path) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "IMG_Init failed: %s", IMG_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(kWindowTitle,
                               SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                               kWindowWidth, kWindowHeight,
                               SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (!window_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1,
                                   SDL_RENDERER_ACCELERATED |
                                   SDL_RENDERER_PRESENTVSYNC);
    if (!renderer_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    const std::optional<std::filesystem::path> game_data_dir =
        find_game_data_dir(executable_path);
    if (!game_data_dir) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No game_data/ directory found next to the executable. "
                    "Run the extractor first (see tools/extractor/main.py). "
                    "Continuing with no map loaded.");
        return true;
    }

    try {
        registry_ = data::DataRegistry::load(*game_data_dir);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "Failed to load game data from '%s': %s",
                     game_data_dir->string().c_str(), e.what());
        return true;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);

    auto mm = std::make_unique<ui::MainMenuScene>(window_, renderer_, *registry_);
    main_menu_ = mm.get();
    scene_manager_.set_scene(std::move(mm));

    return true;
}

int App::run() {
    running_ = true;
    Uint32 last_ticks = SDL_GetTicks();
    SDL_Event event;

    while (running_) {
        while (SDL_PollEvent(&event) != 0) {
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_QUIT) {
                running_ = false;
                break;
            }

            scene_manager_.handle_event(event);
        }

        if (scene_manager_.wants_quit())
            running_ = false;

        // Main menu → gameplay transition.
        if (main_menu_ && main_menu_->wants_start_game()) {
            main_menu_ = nullptr;
            auto gp = std::make_unique<gameplay::GameplayScene>(
                window_, renderer_, *registry_, kStartMapId);
            gameplay_scene_ = gp.get();
            scene_manager_.set_scene(std::move(gp));
        }

        // Gameplay → main menu transition (game button or ESC returning to menu).
        if (gameplay_scene_ && gameplay_scene_->wants_main_menu()) {
            gameplay_scene_ = nullptr;
            auto mm = std::make_unique<ui::MainMenuScene>(
                window_, renderer_, *registry_);
            main_menu_ = mm.get();
            scene_manager_.set_scene(std::move(mm));
        }

        const Uint32 now = SDL_GetTicks();
        const float  dt  = static_cast<float>(now - last_ticks) / 1000.0f;
        last_ticks = now;

        scene_manager_.update(dt);
        render();
    }
    return 0;
}

void App::render() {
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    scene_manager_.render();

    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());
    SDL_RenderPresent(renderer_);
}

App::~App() {
    // Destroy the scene (and its SDL textures/font) before tearing down SDL.
    scene_manager_.reset();

    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

    if (renderer_) SDL_DestroyRenderer(renderer_);
    if (window_)   SDL_DestroyWindow(window_);
    IMG_Quit();
    SDL_Quit();
}

}  // namespace opente::core
