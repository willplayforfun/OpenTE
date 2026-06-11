#include "core/app.h"

#include <SDL_image.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>
#include <vector>

#include "core/paths.h"
#include "render/iso.h"

namespace opente::core {

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr const char* kWindowTitle = "OpenTE (Stage 1: walk the map)";

// World-pixels-per-second pan speed at zoom 1.0.
constexpr float kPanSpeed = 600.0f;

// Mouse-wheel zoom step.
constexpr float kZoomStep = 0.1f;

constexpr const char* kStartMapId = "ep01_china";
constexpr const char* kHqSpriteId = "bldg.chi1.head";

/// Flat tile colors for `world::TerrainType`, indexed by the enum's
/// underlying value. Placeholder until real terrain tile sprites are
/// extracted (see implementation/roadmap.md Stage 2+).
constexpr std::array<SDL_Color, 7> kTerrainColors = {{
    {40, 70, 140, 255},    // DeepWater
    {80, 130, 200, 255},   // ShallowWater
    {100, 170, 80, 255},   // Plains
    {140, 160, 80, 255},   // Hills
    {130, 120, 110, 255},  // Mountains
    {210, 190, 120, 255},  // Desert
    {50, 110, 60, 255},    // Forest
}};

constexpr SDL_Color kDecorationColor{170, 130, 70, 255};

SDL_Color terrain_color(world::TerrainType type) {
    const auto index = static_cast<std::size_t>(type);
    return index < kTerrainColors.size() ? kTerrainColors[index] : SDL_Color{255, 0, 255, 255};
}

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

    const std::optional<std::filesystem::path> game_data_dir = find_game_data_dir(executable_path);
    if (!game_data_dir) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                     "No game_data/ directory found next to the executable. "
                     "Run the extractor first (see tools/extractor/main.py). "
                     "Continuing with no map loaded.");
        return true;
    }

    try {
        registry_ = data::DataRegistry::load(*game_data_dir);
        world_ = world::World::load(*registry_, kStartMapId);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load game data from '%s': %s",
                      game_data_dir->string().c_str(), e.what());
        return true;
    }

    std::cout << "Loaded map '" << world_->region().name() << "' (" << world_->region().width() << "x"
              << world_->region().height() << ")" << std::endl;

    for (const data::SpriteEntry& sprite : registry_->manifest().sprites) {
        if (sprite.id == kHqSpriteId) {
            hq_sprite_ = render::Texture::load(renderer_, *game_data_dir / sprite.file);
            break;
        }
    }

    // Center the camera on the player's starting headquarters, if any.
    if (!world_->region().regions().empty()) {
        const world::Headquarters& hq = world_->region().regions().front().headquarters;
        const render::Vec2 hq_world = render::tile_to_world(static_cast<float>(hq.x), static_cast<float>(hq.y));
        camera_.world_pixel_offset = {hq_world.x - kWindowWidth / 2.0f, hq_world.y - kWindowHeight / 2.0f};
    }

    return true;
}

int App::run() {
    running_ = true;
    Uint32 last_ticks = SDL_GetTicks();
    SDL_Event event;
    while (running_) {
        while (SDL_PollEvent(&event) != 0) {
            handle_event(event);
        }

        const Uint32 now_ticks = SDL_GetTicks();
        const float dt = static_cast<float>(now_ticks - last_ticks) / 1000.0f;
        last_ticks = now_ticks;

        update(dt);
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
        case SDL_KEYUP: {
            const bool down = event.type == SDL_KEYDOWN;
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (down) {
                        running_ = false;
                    }
                    break;
                case SDLK_LEFT:
                case SDLK_a:
                    pan_left_ = down;
                    break;
                case SDLK_RIGHT:
                case SDLK_d:
                    pan_right_ = down;
                    break;
                case SDLK_UP:
                case SDLK_w:
                    pan_up_ = down;
                    break;
                case SDLK_DOWN:
                case SDLK_s:
                    pan_down_ = down;
                    break;
                default:
                    break;
            }
            break;
        }

        case SDL_MOUSEWHEEL:
            if (event.wheel.y != 0) {
                camera_.set_zoom(camera_.zoom + (event.wheel.y > 0 ? kZoomStep : -kZoomStep));
            }
            break;

        default:
            break;
    }
}

void App::update(float dt_seconds) {
    const float distance = kPanSpeed * dt_seconds / camera_.zoom;
    if (pan_left_) {
        camera_.world_pixel_offset.x -= distance;
    }
    if (pan_right_) {
        camera_.world_pixel_offset.x += distance;
    }
    if (pan_up_) {
        camera_.world_pixel_offset.y -= distance;
    }
    if (pan_down_) {
        camera_.world_pixel_offset.y += distance;
    }
}

void App::render() {
    SDL_SetRenderDrawColor(renderer_, 10, 15, 25, 255);
    SDL_RenderClear(renderer_);

    if (world_) {
        render_terrain();
        render_decorations();
        render_buildings();
    }

    SDL_RenderPresent(renderer_);
}

void App::render_terrain() {
    const world::Region& region = world_->region();

    std::vector<SDL_Vertex> vertices;
    std::vector<int> indices;
    vertices.reserve(static_cast<std::size_t>(region.width()) * region.height() * 4);
    indices.reserve(static_cast<std::size_t>(region.width()) * region.height() * 6);

    for (int ty = 0; ty < region.height(); ++ty) {
        for (int tx = 0; tx < region.width(); ++tx) {
            const SDL_Color color = terrain_color(region.terrain_at(tx, ty));

            // Diamond corners for tile (tx, ty): north/east/south/west, per
            // the iso projection in render/iso.h.
            const render::Vec2 north = render::tile_to_world(static_cast<float>(tx), static_cast<float>(ty));
            const render::Vec2 east = render::tile_to_world(static_cast<float>(tx + 1), static_cast<float>(ty));
            const render::Vec2 south =
                render::tile_to_world(static_cast<float>(tx + 1), static_cast<float>(ty + 1));
            const render::Vec2 west = render::tile_to_world(static_cast<float>(tx), static_cast<float>(ty + 1));

            const render::Vec2 north_s = camera_.world_to_screen(north);
            const render::Vec2 east_s = camera_.world_to_screen(east);
            const render::Vec2 south_s = camera_.world_to_screen(south);
            const render::Vec2 west_s = camera_.world_to_screen(west);

            const int base = static_cast<int>(vertices.size());
            vertices.push_back(SDL_Vertex{{north_s.x, north_s.y}, color, {0, 0}});
            vertices.push_back(SDL_Vertex{{east_s.x, east_s.y}, color, {0, 0}});
            vertices.push_back(SDL_Vertex{{south_s.x, south_s.y}, color, {0, 0}});
            vertices.push_back(SDL_Vertex{{west_s.x, west_s.y}, color, {0, 0}});

            indices.insert(indices.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
        }
    }

    SDL_RenderGeometry(renderer_, nullptr, vertices.data(), static_cast<int>(vertices.size()), indices.data(),
                        static_cast<int>(indices.size()));
}

void App::render_decorations() {
    constexpr float kMarkerSize = 8.0f;

    SDL_SetRenderDrawColor(renderer_, kDecorationColor.r, kDecorationColor.g, kDecorationColor.b,
                            kDecorationColor.a);

    for (const world::Decoration& decoration : world_->region().decorations()) {
        const render::Vec2 world_pos =
            render::tile_to_world(static_cast<float>(decoration.x), static_cast<float>(decoration.y));
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        const float size = kMarkerSize * camera_.zoom;
        const SDL_FRect rect{screen_pos.x - size / 2.0f, screen_pos.y - size / 2.0f, size, size};
        SDL_RenderFillRectF(renderer_, &rect);
    }
}

void App::render_buildings() {
    if (!hq_sprite_.valid()) {
        return;
    }

    for (const world::MapRegion& map_region : world_->region().regions()) {
        const world::Headquarters& hq = map_region.headquarters;
        const render::Vec2 world_pos = render::tile_to_world(static_cast<float>(hq.x), static_cast<float>(hq.y));
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        // Placeholder anchor: bottom-center of the sprite sits on the tile
        // point. Real per-sprite anchors aren't extracted yet (Stage 2+).
        const float w = hq_sprite_.width() * camera_.zoom;
        const float h = hq_sprite_.height() * camera_.zoom;
        const SDL_FRect dest{screen_pos.x - w / 2.0f, screen_pos.y - h, w, h};
        SDL_RenderCopyF(renderer_, hq_sprite_.handle(), nullptr, &dest);
    }
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
