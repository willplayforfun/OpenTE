#include "core/app.h"

#include <SDL_image.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <iostream>
#include <utility>

#include "core/paths.h"
#include "render/iso.h"

namespace opente::core {

namespace {
constexpr int kWindowWidth = 1280;
constexpr int kWindowHeight = 720;
constexpr const char* kWindowTitle = "OpenTE";

// World-pixels-per-second pan speed at zoom 1.0.
constexpr float kPanSpeed = 600.0f;

// Mouse-wheel zoom step.
constexpr float kZoomStep = 0.1f;

constexpr const char* kStartMapId = "ep01_china";
constexpr const char* kHqSpriteId = "bldg.chi1.head";

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

    terrain_renderer_.emplace(renderer_, world_->region());
    terrain_renderer_->load_textures(*game_data_dir, *registry_);

    for (const data::SpriteEntry& sprite : registry_->manifest().sprites) {
        if (sprite.id == kHqSpriteId) {
            hq_sprite_.texture = render::Texture::load(renderer_, *game_data_dir / sprite.file);
            hq_sprite_.anchor_x = static_cast<float>(sprite.anchor_x);
            hq_sprite_.anchor_y = static_cast<float>(sprite.anchor_y);
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

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();
    ImGui_ImplSDL2_InitForSDLRenderer(window_, renderer_);
    ImGui_ImplSDLRenderer2_Init(renderer_);

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
            ImGui_ImplSDL2_ProcessEvent(&event);

            if (event.type == SDL_KEYDOWN && event.key.keysym.scancode == SDL_SCANCODE_GRAVE) {
                show_dev_gui_ = !show_dev_gui_;
                continue;
            }

            const ImGuiIO& io = ImGui::GetIO();
            if (io.WantCaptureKeyboard && (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP))
                continue;
            if (io.WantCaptureMouse && (event.type == SDL_MOUSEWHEEL))
                continue;

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
    ImGui_ImplSDLRenderer2_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    SDL_SetRenderDrawColor(renderer_, 10, 15, 25, 255);
    SDL_RenderClear(renderer_);

    if (world_) {
        terrain_renderer_->render(camera_);
        render_decorations();
        render_buildings();
    }

    if (show_dev_gui_) {
        render_dev_gui();
    }
    ImGui::Render();
    ImGui_ImplSDLRenderer2_RenderDrawData(ImGui::GetDrawData());

    SDL_RenderPresent(renderer_);
}

void App::render_decorations() {
    for (const world::Decoration& decoration : world_->region().decorations()) {
        const auto it = decoration_sprites_.find(decoration.sprite);
        if (it == decoration_sprites_.end() || !it->second.texture.valid()) {
            continue;
        }
        const AnchoredSprite& sprite = it->second;

        render::Vec2 world_pos =
            render::tile_to_world(static_cast<float>(decoration.x), static_cast<float>(decoration.y));
        world_pos.y -= terrain_renderer_->sample_height(decoration.x, decoration.y) * render::kPixelsPerAltiUnit;
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        const float w = sprite.texture.width() * camera_.zoom;
        const float h = sprite.texture.height() * camera_.zoom;
        const SDL_FRect dest{screen_pos.x + sprite.anchor_x * camera_.zoom,
                              screen_pos.y + sprite.anchor_y * camera_.zoom, w, h};
        SDL_RenderCopyF(renderer_, sprite.texture.handle(), nullptr, &dest);
    }
}

void App::render_buildings() {
    if (!hq_sprite_.texture.valid()) {
        return;
    }

    for (const world::MapRegion& map_region : world_->region().regions()) {
        const world::Headquarters& hq = map_region.headquarters;
        render::Vec2 world_pos = render::tile_to_world(static_cast<float>(hq.x), static_cast<float>(hq.y));
        world_pos.y -= terrain_renderer_->sample_height(hq.x, hq.y) * render::kPixelsPerAltiUnit;
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        const float w = hq_sprite_.texture.width() * camera_.zoom;
        const float h = hq_sprite_.texture.height() * camera_.zoom;
        const SDL_FRect dest{screen_pos.x + hq_sprite_.anchor_x * camera_.zoom,
                              screen_pos.y + hq_sprite_.anchor_y * camera_.zoom, w, h};
        SDL_RenderCopyF(renderer_, hq_sprite_.texture.handle(), nullptr, &dest);
    }
}

void App::render_dev_gui() {
    ImGui::Begin("Dev Tools", &show_dev_gui_);
    if (terrain_renderer_) {
        if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Shore overlays", &terrain_renderer_->shore_overlays_enabled);
            ImGui::Checkbox("Slope shading", &terrain_renderer_->slope_shading_enabled);
            ImGui::Checkbox("Terrain blending", &terrain_renderer_->terrain_blending_enabled);
            ImGui::Checkbox("Debug labels", &terrain_renderer_->terrain_debug_labels_enabled);
        }
    }
    ImGui::End();
}

App::~App() {
    ImGui_ImplSDLRenderer2_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();

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
