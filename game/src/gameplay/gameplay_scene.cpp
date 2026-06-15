#include "gameplay/gameplay_scene.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <iostream>
#include <utility>

#include "render/iso.h"
#include "ui/build_menu.h"

namespace opente::gameplay {

namespace {

constexpr float kPanSpeed = 600.0f;  // world-pixels/sec at zoom 1.0
constexpr float kZoomStep = 0.1f;

constexpr const char* kHqSpriteId = "bldg.chi1.head";
constexpr const char* kDecorationSpritePrefix = "flor.";

constexpr const char* kConsSpriteIds[] = {
    "ui.a_ui.cons.back",
    "ui.a_ui.cons.sele",
    "ui.a_ui.cons.conf",
    "ui.a_ui.cons.canc",
};

}  // namespace

GameplayScene::GameplayScene(SDL_Window* window,
                             SDL_Renderer* renderer,
                             const data::DataRegistry& registry,
                             const std::string& map_id)
    : window_(window), renderer_(renderer), registry_(&registry)
{
    try {
        world_ = world::World::load(registry, map_id);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GameplayScene: failed to load map '%s': %s",
                     map_id.c_str(), e.what());
        return;
    }

    std::cout << "Loaded map '" << world_->region().name() << "' ("
              << world_->region().width() << "x" << world_->region().height() << ")\n";

    terrain_renderer_.emplace(renderer_, world_->region());
    terrain_tileset_ = render::TerrainTileset::load(renderer_, registry.game_data_dir(), registry);
    terrain_renderer_->set_tileset(&terrain_tileset_.value());

    load_sprites();

    if (!ui_manager_.init(renderer_, registry.game_data_dir())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GameplayScene: UIManager::init failed");
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

    // Center the camera on the player's starting headquarters.
    if (!world_->region().regions().empty()) {
        const world::Headquarters& hq = world_->region().regions().front().headquarters;
        const render::Vec2 hq_world = render::tile_to_world(
            static_cast<float>(hq.x), static_cast<float>(hq.y));
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(window_, &win_w, &win_h);
        camera_.world_pixel_offset = {
            hq_world.x - win_w / 2.0f,
            hq_world.y - win_h / 2.0f,
        };
    }
}

GameplayScene::~GameplayScene() {
    ui_manager_.shutdown();
}

void GameplayScene::load_sprites() {
    const std::filesystem::path& game_data_dir = registry_->game_data_dir();

    // Quick id→entry lookup for cons-panel sprites.
    std::map<std::string, const data::SpriteEntry*> cons_entries;
    for (const auto& s : registry_->manifest().sprites) {
        for (const char* ui_id : kConsSpriteIds) {
            if (s.id == ui_id) { cons_entries[s.id] = &s; break; }
        }
    }

    for (const data::SpriteEntry& sprite : registry_->manifest().sprites) {
        if (sprite.id == kHqSpriteId) {
            hq_sprite_.texture = render::Texture::load(renderer_, game_data_dir / sprite.file);
            hq_sprite_.anchor_x = static_cast<float>(sprite.anchor_x);
            hq_sprite_.anchor_y = static_cast<float>(sprite.anchor_y);
            continue;
        }

        if (sprite.id.rfind(kDecorationSpritePrefix, 0) == 0) {
            AnchoredSprite anchored;
            anchored.texture = render::Texture::load(renderer_, game_data_dir / sprite.file);
            anchored.anchor_x = static_cast<float>(sprite.anchor_x);
            anchored.anchor_y = static_cast<float>(sprite.anchor_y);
            decoration_sprites_.emplace(sprite.id, std::move(anchored));
            continue;
        }

        for (const char* ui_id : kConsSpriteIds) {
            if (sprite.id == ui_id) {
                ui_textures_.emplace(sprite.id,
                    render::Texture::load(renderer_, game_data_dir / sprite.file));
                break;
            }
        }
    }

    auto make_skin_sprite = [&](const char* id) -> ui::SkinSprite {
        auto tex_it   = ui_textures_.find(id);
        auto entry_it = cons_entries.find(id);
        if (tex_it == ui_textures_.end() || !tex_it->second.valid() ||
            entry_it == cons_entries.end())
            return {};
        const data::SpriteEntry* e = entry_it->second;
        return {tex_it->second.handle(), e->width, e->height, e->anchor_x, e->anchor_y};
    };

    cons_skin_.background  = make_skin_sprite("ui.a_ui.cons.back");
    cons_skin_.selection   = make_skin_sprite("ui.a_ui.cons.sele");
    cons_skin_.confirm_btn = make_skin_sprite("ui.a_ui.cons.conf");
    cons_skin_.cancel_btn  = make_skin_sprite("ui.a_ui.cons.canc");

    if (!cons_skin_.valid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "cons panel sprites not found — build menu will use flat colours");
    }
}

// ---------------------------------------------------------------------------
// Scene interface
// ---------------------------------------------------------------------------

bool GameplayScene::handle_event(const SDL_Event& event) {
    // Dev-GUI toggle — highest priority, before ImGui and the UI layer.
    if (event.type == SDL_KEYDOWN &&
        event.key.keysym.scancode == SDL_SCANCODE_GRAVE) {
        show_dev_gui_ = !show_dev_gui_;
        return true;
    }

    // Suppress game input while ImGui has keyboard/mouse focus.
    const ImGuiIO& io = ImGui::GetIO();
    if (io.WantCaptureKeyboard &&
        (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP))
        return false;
    if (io.WantCaptureMouse && event.type == SDL_MOUSEWHEEL)
        return false;

    // UI widget layer gets priority over game input.
    if (ui_manager_.handle_event(event))
        return true;

    switch (event.type) {
        case SDL_KEYDOWN:
        case SDL_KEYUP: {
            const bool down = (event.type == SDL_KEYDOWN);
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE:
                    if (down) {
                        if (ui_manager_.has_open_dialogs()) {
                            if (build_menu_ptr_) {
                                ui_manager_.close(build_menu_ptr_);
                                build_menu_ptr_ = nullptr;
                            }
                        } else {
                            wants_quit_ = true;
                        }
                    }
                    return true;
                case SDLK_b:
                    if (down) toggle_build_menu();
                    return true;
                case SDLK_LEFT:  case SDLK_a: pan_left_  = down; return true;
                case SDLK_RIGHT: case SDLK_d: pan_right_ = down; return true;
                case SDLK_UP:    case SDLK_w: pan_up_    = down; return true;
                case SDLK_DOWN:  case SDLK_s: pan_down_  = down; return true;
                default: break;
            }
            break;
        }

        case SDL_MOUSEWHEEL:
            if (event.wheel.y != 0) {
                camera_.set_zoom(camera_.zoom +
                                 (event.wheel.y > 0 ? kZoomStep : -kZoomStep));
            }
            return true;

        default:
            break;
    }
    return false;
}

void GameplayScene::update(float dt) {
    const float distance = kPanSpeed * dt / camera_.zoom;
    if (pan_left_)  camera_.world_pixel_offset.x -= distance;
    if (pan_right_) camera_.world_pixel_offset.x += distance;
    if (pan_up_)    camera_.world_pixel_offset.y -= distance;
    if (pan_down_)  camera_.world_pixel_offset.y += distance;
}

void GameplayScene::render() {
    SDL_SetRenderDrawColor(renderer_, 10, 15, 25, 255);
    SDL_RenderClear(renderer_);

    if (world_) {
        terrain_renderer_->render(camera_);
        render_decorations();
        render_buildings();
    }

    ui_manager_.render();

    if (show_font_test_)       render_font_test();
    if (show_dev_gui_)         render_dev_gui();
    if (show_lighting_window_) render_lighting_window();
}

// ---------------------------------------------------------------------------
// World rendering
// ---------------------------------------------------------------------------

void GameplayScene::render_decorations() {
    for (const world::Decoration& dec : world_->region().decorations()) {
        const auto it = decoration_sprites_.find(dec.sprite);
        if (it == decoration_sprites_.end() || !it->second.texture.valid()) continue;
        const AnchoredSprite& spr = it->second;

        render::Vec2 world_pos = render::tile_to_world(
            static_cast<float>(dec.x), static_cast<float>(dec.y));
        world_pos.y -= terrain_renderer_->sample_height(dec.x, dec.y)
                       * render::kPixelsPerAltiUnit;
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        const float w = spr.texture.width()  * camera_.zoom;
        const float h = spr.texture.height() * camera_.zoom;
        const SDL_FRect dest{
            screen_pos.x + spr.anchor_x * camera_.zoom,
            screen_pos.y + spr.anchor_y * camera_.zoom, w, h};
        SDL_RenderCopyF(renderer_, spr.texture.handle(), nullptr, &dest);
    }
}

void GameplayScene::render_buildings() {
    if (!hq_sprite_.texture.valid()) return;

    for (const world::MapRegion& map_region : world_->region().regions()) {
        const world::Headquarters& hq = map_region.headquarters;
        render::Vec2 world_pos = render::tile_to_world(
            static_cast<float>(hq.x), static_cast<float>(hq.y));
        world_pos.y -= terrain_renderer_->sample_height(hq.x, hq.y)
                       * render::kPixelsPerAltiUnit;
        const render::Vec2 screen_pos = camera_.world_to_screen(world_pos);

        const float w = hq_sprite_.texture.width()  * camera_.zoom;
        const float h = hq_sprite_.texture.height() * camera_.zoom;
        const SDL_FRect dest{
            screen_pos.x + hq_sprite_.anchor_x * camera_.zoom,
            screen_pos.y + hq_sprite_.anchor_y * camera_.zoom, w, h};
        SDL_RenderCopyF(renderer_, hq_sprite_.texture.handle(), nullptr, &dest);
    }
}

void GameplayScene::toggle_build_menu() {
    if (build_menu_ptr_) {
        ui_manager_.close(build_menu_ptr_);
        build_menu_ptr_ = nullptr;
        return;
    }

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

// ---------------------------------------------------------------------------
// Dev GUI
// ---------------------------------------------------------------------------

void GameplayScene::render_dev_gui() {
    ImGui::Begin("Dev Tools", &show_dev_gui_);
    if (terrain_renderer_) {
        if (ImGui::CollapsingHeader("Terrain")) {
            ImGui::Checkbox("Shore overlays",    &terrain_renderer_->shore_overlays_enabled);
            ImGui::Checkbox("Slope shading",     &terrain_renderer_->slope_shading_enabled);
            ImGui::Checkbox("Terrain blending",  &terrain_renderer_->terrain_blending_enabled);
            ImGui::Checkbox("Debug labels",      &terrain_renderer_->terrain_debug_labels_enabled);
            if (ImGui::DragFloat("Alti scale factor",
                                 &render::kAltiScaleFactor, 0.1f, 0.5f, 50.0f,
                                 "%.2f (EXE: 10.0)")) {
                render::kAltiToWorldHeight  = render::kAltiScaleFactor / 256.0f;
                render::kPixelsPerAltiUnit  = render::kPixelsPerWorldHeightUnit
                                              * render::kAltiToWorldHeight;
                terrain_renderer_->rebuild_vertex_colors();
            }
            if (ImGui::DragFloat("Pixels per world-height unit",
                                 &render::kPixelsPerWorldHeightUnit, 0.25f, 1.0f, 200.0f,
                                 "%.2f (EXE: 45.25)")) {
                render::kPixelsPerAltiUnit = render::kPixelsPerWorldHeightUnit
                                             * render::kAltiToWorldHeight;
            }
        }
        ImGui::Separator();
        if (ImGui::Button("Lighting controls..."))
            show_lighting_window_ = true;
    }
    ImGui::Separator();
    if (ImGui::CollapsingHeader("Font")) {
        ImGui::Checkbox("Font test overlay", &show_font_test_);
        ImGui::TextDisabled("Shows text samples with baseline guides.");
    }
    ImGui::End();
}

void GameplayScene::render_font_test() {
    render::BitmapFont* font = ui_manager_.font();
    if (!font) return;

    const SDL_Rect panel{20, 20, 680, 440};
    SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer_, 10, 10, 20, 230);
    SDL_RenderFillRect(renderer_, &panel);
    SDL_SetRenderDrawColor(renderer_, 100, 100, 160, 200);
    SDL_RenderDrawRect(renderer_, &panel);

    const int lh  = font->line_height();
    const int asc = font->ascender();
    int x = panel.x + 12;
    int y = panel.y + 12 + asc;

    auto row = [&](const char* text, SDL_Color color) {
        SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer_, 180, 40, 40, 80);
        SDL_RenderDrawLine(renderer_, x, y, panel.x + panel.w - 12, y);
        font->draw_text(renderer_, text, x, y, color);
        y += lh + 2;
    };

    const SDL_Color white  = {255, 255, 255, 255};
    const SDL_Color yellow = {220, 200, 100, 255};
    const SDL_Color cyan   = {120, 220, 220, 255};
    const SDL_Color gray   = {160, 160, 160, 255};

    row("The quick brown fox jumps over the lazy dog", white);
    row("ABCDEFGHIJKLMNOPQRSTUVWXYZ", yellow);
    row("abcdefghijklmnopqrstuvwxyz", yellow);
    row("0123456789  !\"#$%&'()*+,-./:;<=>?@[\\]^_`{|}~", cyan);
    row("Pack my box with five dozen liquor jugs.", white);
    row("Sphinx of black quartz, judge my vow.", white);
    row("Ag Bp Qj Wy Tz  (ascender/descender mix)", gray);
    row("Trade Empires  -  OpenTE clone  -  bitmap font test", white);

    y += 4;
    ImGui::SetNextWindowPos({static_cast<float>(x), static_cast<float>(y)});
    ImGui::SetNextWindowSize({500, 0});
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, {0, 0, 0, 0});
    ImGui::Begin("##font_info", nullptr,
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_NoNav        | ImGuiWindowFlags_NoMove);
    ImGui::TextColored({0.5f, 0.5f, 0.7f, 1.0f},
        "clea 12pt  asc=%d  lh=%d  (backtick=dev, toggle in Font section)", asc, lh);
    ImGui::End();
    ImGui::PopStyleColor();
}

void GameplayScene::render_lighting_window() {
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
    changed |= ImGui::DragFloat("Slope gradient scale", &render::kSlopeGradientScale,
                                0.01f, 0.0f, 20.0f);
    changed |= ImGui::DragFloat("Ambient R", &render::kAmbientR, 0.001f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Ambient G", &render::kAmbientG, 0.001f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Ambient B", &render::kAmbientB, 0.001f, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Vertex color scale", &render::kVertexColorScale,
                                0.01f, 0.0f, 4.0f);

    ImGui::Separator();
    if (ImGui::Button("Reset to EXE defaults")) {
        render::kSlopeGradientScale      = render_defaults_.slope_gradient_scale;
        render::kAmbientR                = render_defaults_.ambient_r;
        render::kAmbientG                = render_defaults_.ambient_g;
        render::kAmbientB                = render_defaults_.ambient_b;
        render::kVertexColorScale        = render_defaults_.vertex_color_scale;
        changed = true;
    }

    if (changed)
        terrain_renderer_->rebuild_vertex_colors();

    ImGui::End();
}

}  // namespace opente::gameplay
