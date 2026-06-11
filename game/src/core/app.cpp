#include "core/app.h"

#include <SDL_image.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <iostream>

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

/// `game_data` sprite ids for the per-`world::TerrainType` tile textures
/// (see `tools/extractor/sprites/terrain.py`), indexed by the enum's
/// underlying value.
constexpr std::array<const char*, 7> kTerrainTextureIds = {{
    "terrain.deep_water",
    "terrain.shallow_water",
    "terrain.plains",
    "terrain.hills",
    "terrain.mountains",
    "terrain.desert",
    "terrain.forest",
}};

constexpr const char* kTerrainEdgeTextureId = "terrain.edge";

/// Sprite ids for ground-decoration sprites (see
/// `tools/extractor/sprites/decorations.py`) all share this prefix.
constexpr const char* kDecorationSpritePrefix = "flor.";

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
            continue;
        }

        if (sprite.id == kTerrainEdgeTextureId) {
            terrain_edge_texture_ = render::Texture::load(renderer_, *game_data_dir / sprite.file);
            continue;
        }

        const auto terrain_it = std::find(kTerrainTextureIds.begin(), kTerrainTextureIds.end(), sprite.id);
        if (terrain_it != kTerrainTextureIds.end()) {
            const auto index = static_cast<std::size_t>(terrain_it - kTerrainTextureIds.begin());
            terrain_textures_[index] = render::Texture::load(renderer_, *game_data_dir / sprite.file);
            continue;
        }

        if (sprite.id.rfind(kDecorationSpritePrefix, 0) == 0) {
            AnchoredSprite anchored;
            anchored.texture = render::Texture::load(renderer_, *game_data_dir / sprite.file);
            anchored.anchor_x = static_cast<float>(sprite.anchor_x);
            anchored.anchor_y = static_cast<float>(sprite.anchor_y);
            decoration_sprites_.emplace(sprite.id, std::move(anchored));
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

    const float tile_w = render::kTileWidth * camera_.zoom;
    const float tile_h = render::kTileHeight * camera_.zoom;

    for (int ty = 0; ty < region.height(); ++ty) {
        for (int tx = 0; tx < region.width(); ++tx) {
            const auto index = static_cast<std::size_t>(region.terrain_at(tx, ty));
            const render::Texture& texture = terrain_textures_[index];
            if (!texture.valid()) {
                continue;
            }

            // Tile (tx, ty)'s diamond spans from its "north" corner (the
            // top point, per render/iso.h) down kTileHeight and from
            // kTileWidth/2 left of it to kTileWidth/2 right of it.
            const render::Vec2 north = render::tile_to_world(static_cast<float>(tx), static_cast<float>(ty));
            const render::Vec2 top_left =
                camera_.world_to_screen({north.x - render::kTileWidth / 2.0f, north.y});

            const SDL_FRect dest{top_left.x, top_left.y, tile_w, tile_h};
            SDL_RenderCopyF(renderer_, texture.handle(), nullptr, &dest);
        }
    }

    render_terrain_edges();
}

void App::render_terrain_edges() {
    if (!terrain_edge_texture_.valid()) {
        return;
    }

    const world::Region& region = world_->region();
    const float tile_w = render::kTileWidth * camera_.zoom;
    const float tile_h = render::kTileHeight * camera_.zoom;

    // Draw a row of "skirt" tiles directly below the map's south and east
    // border tiles, using the vertical rock/cliff texture (terrain.edge),
    // approximating the original game's map-edge rendering.
    auto draw_skirt = [&](int tx, int ty) {
        const render::Vec2 north = render::tile_to_world(static_cast<float>(tx), static_cast<float>(ty));
        const render::Vec2 top_left =
            camera_.world_to_screen({north.x - render::kTileWidth / 2.0f, north.y + render::kTileHeight});
        const SDL_FRect dest{top_left.x, top_left.y, tile_w, tile_h};
        SDL_RenderCopyF(renderer_, terrain_edge_texture_.handle(), nullptr, &dest);
    };

    for (int tx = 0; tx < region.width(); ++tx) {
        draw_skirt(tx, region.height() - 1);
    }
    for (int ty = 0; ty < region.height(); ++ty) {
        draw_skirt(region.width() - 1, ty);
    }
}

void App::render_decorations() {
    for (const world::Decoration& decoration : world_->region().decorations()) {
        const auto it = decoration_sprites_.find(decoration.sprite);
        if (it == decoration_sprites_.end() || !it->second.texture.valid()) {
            continue;
        }
        const AnchoredSprite& sprite = it->second;

        const render::Vec2 world_pos =
            render::tile_to_world(static_cast<float>(decoration.x), static_cast<float>(decoration.y));
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        const float w = sprite.texture.width() * camera_.zoom;
        const float h = sprite.texture.height() * camera_.zoom;
        const SDL_FRect dest{screen_pos.x + sprite.anchor_x * camera_.zoom,
                              screen_pos.y + sprite.anchor_y * camera_.zoom, w, h};
        SDL_RenderCopyF(renderer_, sprite.texture.handle(), nullptr, &dest);
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
