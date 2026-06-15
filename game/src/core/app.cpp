#include "core/app.h"

#include <SDL_image.h>
#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <iostream>
#include <memory>
#include <utility>

#include "core/paths.h"
#include "render/iso.h"
#include "ui/build_menu.h"

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
    terrain_tileset_ = render::TerrainTileset::load(renderer_, *game_data_dir, *registry_);
    terrain_renderer_->set_tileset(&terrain_tileset_.value());

    // UI sprite IDs that need to be loaded for the construction panel skin.
    static constexpr const char* kConsSpriteIds[] = {
        "ui.a_ui.cons.back",
        "ui.a_ui.cons.sele",
        "ui.a_ui.cons.conf",
        "ui.a_ui.cons.canc",
    };

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
            continue;
        }

        for (const char* ui_id : kConsSpriteIds) {
            if (sprite.id == ui_id) {
                ui_textures_.emplace(sprite.id,
                    render::Texture::load(renderer_, *game_data_dir / sprite.file));
                break;
            }
        }
    }

    // Build ConsSkin from the loaded textures.  Any sprite that failed to load
    // leaves its SkinSprite with tex=nullptr; ConsSkin::valid() fails if the
    // background is missing, falling back to flat-colour rendering.
    auto make_skin_sprite = [&](const char* id, const data::SpriteEntry* entry) -> ui::SkinSprite {
        auto it = ui_textures_.find(id);
        if (it == ui_textures_.end() || !it->second.valid() || entry == nullptr)
            return {};
        return {it->second.handle(), entry->width, entry->height,
                entry->anchor_x, entry->anchor_y};
    };

    // Build a quick id->entry lookup for the cons sprites.
    std::map<std::string, const data::SpriteEntry*> cons_entries;
    for (const auto& s : registry_->manifest().sprites) {
        for (const char* ui_id : kConsSpriteIds) {
            if (s.id == ui_id) { cons_entries[s.id] = &s; break; }
        }
    }

    cons_skin_.background   = make_skin_sprite("ui.a_ui.cons.back",
                                                cons_entries.count("ui.a_ui.cons.back") ? cons_entries["ui.a_ui.cons.back"] : nullptr);
    cons_skin_.selection    = make_skin_sprite("ui.a_ui.cons.sele",
                                                cons_entries.count("ui.a_ui.cons.sele") ? cons_entries["ui.a_ui.cons.sele"] : nullptr);
    cons_skin_.confirm_btn  = make_skin_sprite("ui.a_ui.cons.conf",
                                                cons_entries.count("ui.a_ui.cons.conf") ? cons_entries["ui.a_ui.cons.conf"] : nullptr);
    cons_skin_.cancel_btn   = make_skin_sprite("ui.a_ui.cons.canc",
                                                cons_entries.count("ui.a_ui.cons.canc") ? cons_entries["ui.a_ui.cons.canc"] : nullptr);

    if (!cons_skin_.valid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "cons panel sprites not found — build menu will use flat colours");
    }

    render_defaults_ = {
        render::kSlopeGradientScale,
        render::kAmbientR,
        render::kAmbientG,
        render::kAmbientB,
        render::kVertexColorScale,
        render::kAltiScaleFactor,
        render::kPixelsPerWorldHeightUnit,
    };

    // Initialise the UI manager.  Font atlas is in game_data/fonts/.
    if (!ui_manager_.init(renderer_, *game_data_dir)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "UIManager::init failed — cannot continue.");
        return false;
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

            // UI gets first crack at events; only forward to game if unconsumed.
            if (!ui_manager_.handle_event(event)) {
                handle_event(event);
            }
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
                        if (ui_manager_.has_open_dialogs()) {
                            // Close the topmost dialog on Escape.
                            if (build_menu_ptr_) {
                                ui_manager_.close(build_menu_ptr_);
                                build_menu_ptr_ = nullptr;
                            }
                        } else {
                            running_ = false;
                        }
                    }
                    break;
                case SDLK_b:
                    if (down) toggle_build_menu();
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

    // UI renders on top of the game world, before the ImGui dev overlay.
    ui_manager_.render();

    if (show_dev_gui_) {
        render_dev_gui();
    }
    if (show_lighting_window_) {
        render_lighting_window();
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

void App::toggle_build_menu() {
    if (build_menu_ptr_) {
        ui_manager_.close(build_menu_ptr_);
        build_menu_ptr_ = nullptr;
        return;
    }
    if (!registry_) return;  // no data loaded yet

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);

    auto menu = std::make_unique<ui::BuildMenu>(
        registry_->buildings(),
        [this](const std::string& building_id) {
            if (!building_id.empty()) {
                // TODO (Stage 2): enter placement mode for `building_id`.
                SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                            "Build menu selected: %s", building_id.c_str());
            }
            ui_manager_.close(build_menu_ptr_);
            build_menu_ptr_ = nullptr;
        },
        cons_skin_);
    build_menu_ptr_ = menu.get();
    ui_manager_.open(std::move(menu), win_w, win_h);
}

void App::render_dev_gui() {
    ImGui::Begin("Dev Tools", &show_dev_gui_);
    if (terrain_renderer_) {
        if (ImGui::CollapsingHeader("Terrain", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Shore overlays", &terrain_renderer_->shore_overlays_enabled);
            ImGui::Checkbox("Slope shading", &terrain_renderer_->slope_shading_enabled);
            ImGui::Checkbox("Terrain blending", &terrain_renderer_->terrain_blending_enabled);
            ImGui::Checkbox("Debug labels", &terrain_renderer_->terrain_debug_labels_enabled);
            if (ImGui::DragFloat("Alti scale factor", &render::kAltiScaleFactor, 0.1f, 0.5f, 50.0f, "%.2f (EXE: 10.0)")) {
                render::kAltiToWorldHeight = render::kAltiScaleFactor / 256.0f;
                render::kPixelsPerAltiUnit = render::kPixelsPerWorldHeightUnit * render::kAltiToWorldHeight;
                terrain_renderer_->rebuild_vertex_colors();
            }
            if (ImGui::DragFloat("Pixels per world-height unit", &render::kPixelsPerWorldHeightUnit, 0.25f, 1.0f, 200.0f, "%.2f (EXE: 45.25)")) {
                render::kPixelsPerAltiUnit = render::kPixelsPerWorldHeightUnit * render::kAltiToWorldHeight;
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Lighting controls...")) {
            show_lighting_window_ = true;
        }
    }
    ImGui::End();
}

void App::render_lighting_window() {
    ImGui::Begin("Lighting controls", &show_lighting_window_);

    if (!terrain_renderer_) {
        ImGui::TextUnformatted("No terrain loaded.");
        ImGui::End();
        return;
    }

    ImGui::TextWrapped(
        "Adjust the terrain slope-shading lighting in realtime. Changes "
        "rebuild the per-vertex mesh colors immediately.");
    ImGui::Separator();

    bool changed = false;
    changed |= ImGui::DragFloat("Slope gradient scale", &render::kSlopeGradientScale, 0.01f, 0.0f, 20.0f);
    changed |= ImGui::DragFloat("Ambient R", &render::kAmbientR, 0.001f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Ambient G", &render::kAmbientG, 0.001f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Ambient B", &render::kAmbientB, 0.001f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Vertex color scale", &render::kVertexColorScale, 0.01f, 0.0f, 4.0f);

    ImGui::Separator();
    if (ImGui::Button("Reset to EXE defaults")) {
        render::kSlopeGradientScale = render_defaults_.slope_gradient_scale;
        render::kAmbientR = render_defaults_.ambient_r;
        render::kAmbientG = render_defaults_.ambient_g;
        render::kAmbientB = render_defaults_.ambient_b;
        render::kVertexColorScale = render_defaults_.vertex_color_scale;
        changed = true;
    }

    if (changed) {
        terrain_renderer_->rebuild_vertex_colors();
    }

    ImGui::End();
}

App::~App() {
    ui_manager_.shutdown();

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
