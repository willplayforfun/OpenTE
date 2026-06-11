#include "core/app.h"

#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>

#include "core/paths.h"

namespace opente::core {

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr const char* kWindowTitle = "OpenTE (toolchain spike)";
constexpr int kMoveStep = 8;
}  // namespace

bool App::init(const std::filesystem::path& executable_path) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return false;
    }

    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "IMG_Init failed: %s", IMG_GetError());
        return false;
    }

    window_ = SDL_CreateWindow(kWindowTitle, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                kWindowWidth, kWindowHeight,
                                SDL_WINDOW_SHOWN | SDL_WINDOW_RESIZABLE);
    if (window_ == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }

    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (renderer_ == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }

    // Load the extractor's manifest and the first sprite it lists. This is
    // the toolchain-spike proof that the game can read both the JSON table
    // data and the PNG assets produced by the Python extractor (see
    // tools/extractor/main.py).
    const std::optional<std::filesystem::path> game_data_dir = find_game_data_dir(executable_path);
    if (!game_data_dir) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                     "No game_data/ directory found next to the executable. "
                     "Run the extractor first (see tools/extractor/main.py). "
                     "Continuing with no sprite loaded.");
        return true;
    }

    const std::filesystem::path manifest_path = *game_data_dir / "manifest.json";
    std::ifstream manifest_file(manifest_path);
    if (!manifest_file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Could not open manifest at '%s'.",
                     manifest_path.string().c_str());
        return true;
    }

    nlohmann::json manifest;
    try {
        manifest_file >> manifest;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to parse manifest '%s': %s",
                      manifest_path.string().c_str(), e.what());
        return true;
    }

    if (!manifest.contains("sprites") || manifest["sprites"].empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "Manifest '%s' has no sprites listed.",
                     manifest_path.string().c_str());
        return true;
    }

    const nlohmann::json& sprite_entry = manifest["sprites"].front();
    const std::string sprite_id = sprite_entry.value("id", "<unknown>");
    const std::string sprite_file = sprite_entry.value("file", "");
    const std::filesystem::path sprite_path = *game_data_dir / sprite_file;

    sprite_ = render::Texture::load(renderer_, sprite_path);
    if (sprite_.valid()) {
        std::cout << "Loaded sprite '" << sprite_id << "' (" << sprite_.width() << "x"
                  << sprite_.height() << ") from " << sprite_path.string() << std::endl;
        const std::string title = std::string(kWindowTitle) + " - " + sprite_id;
        SDL_SetWindowTitle(window_, title.c_str());
    }

    return true;
}

int App::run() {
    running_ = true;
    SDL_Event event;
    while (running_) {
        while (SDL_PollEvent(&event) != 0) {
            handle_event(event);
        }
        update();
        render();
    }
    return 0;
}

void App::handle_event(const SDL_Event& event) {
    switch (event.type) {
        case SDL_QUIT:
            running_ = false;
            break;

        case SDL_KEYDOWN:
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    running_ = false;
                    break;
                case SDLK_LEFT:
                    sprite_x_ -= kMoveStep;
                    break;
                case SDLK_RIGHT:
                    sprite_x_ += kMoveStep;
                    break;
                case SDLK_UP:
                    sprite_y_ -= kMoveStep;
                    break;
                case SDLK_DOWN:
                    sprite_y_ += kMoveStep;
                    break;
                default:
                    break;
            }
            break;

        case SDL_MOUSEBUTTONDOWN:
            if (event.button.button == SDL_BUTTON_LEFT) {
                // Re-center the sprite on the click point.
                sprite_x_ = event.button.x - sprite_.width() / 2;
                sprite_y_ = event.button.y - sprite_.height() / 2;
            }
            break;

        default:
            break;
    }
}

void App::update() {
    // Nothing to simulate yet in the spike.
}

void App::render() {
    SDL_SetRenderDrawColor(renderer_, 30, 40, 60, 255);
    SDL_RenderClear(renderer_);

    if (sprite_.valid()) {
        SDL_Rect dest{sprite_x_, sprite_y_, sprite_.width(), sprite_.height()};
        SDL_RenderCopy(renderer_, sprite_.handle(), nullptr, &dest);
    }

    SDL_RenderPresent(renderer_);
}

App::~App() {
    if (renderer_ != nullptr) {
        SDL_DestroyRenderer(renderer_);
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
    }
    IMG_Quit();
    SDL_Quit();
}

}  // namespace opente::core
