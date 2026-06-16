#include "gameplay/gameplay_scene.h"

#include <imgui.h>
#include <imgui_impl_sdl2.h>
#include <imgui_impl_sdlrenderer2.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <utility>

#include "render/iso.h"
#include "ui/build_menu.h"
#include "ui/hud.h"

namespace opente::gameplay {

namespace {

constexpr float kPanSpeed = 600.0f;  // world-pixels/sec at zoom 1.0
constexpr float kZoomStep = 0.1f;

constexpr const char* kDecorationSpritePrefix = "flor.";

constexpr const char* kConsSpriteIds[] = {
    "ui.a_ui.cons.back",
    "ui.a_ui.cons.sele",
    "ui.a_ui.cons.conf",
    "ui.a_ui.cons.canc",
    "ui.a_ui.stdc.vscr",   // shared vertical scrollbar (up/down arrows)
};

}  // namespace

GameplayScene::GameplayScene(SDL_Window* window,
                             SDL_Renderer* renderer,
                             const data::DataRegistry& registry,
                             const std::string& map_id)
    : window_(window), renderer_(renderer), registry_(&registry)
{
    load_sprites();

    if (!ui_manager_.init(renderer_, registry.game_data_dir())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GameplayScene: UIManager::init failed");
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);
    ui_manager_.set_hud(std::make_unique<ui::HudBars>(), win_w, win_h);

    render_defaults_ = {
        render::kSlopeGradientScale,
        render::kAmbientR,
        render::kAmbientG,
        render::kAmbientB,
        render::kVertexColorScale,
        render::kAltiScaleFactor,
        render::kPixelsPerWorldHeightUnit,
    };

    load_map(map_id);
}

void GameplayScene::load_map(const std::string& map_id) {
    // Tear down current map state (releases GPU textures before reallocating).
    if (terrain_renderer_) terrain_renderer_->set_tileset(nullptr);
    terrain_renderer_.reset();
    terrain_tileset_.reset();
    world_.reset();
    hq_sprite_ = {};

    try {
        world_ = world::World::load(*registry_, map_id);
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GameplayScene: failed to load map '%s': %s",
                     map_id.c_str(), e.what());
        return;
    }

    current_map_id_ = map_id;

    // Update selected_map_index_ to match the loaded map for the dev GUI listbox.
    const auto& maps = registry_->manifest().maps;
    for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
        if (maps[i].id == map_id) { selected_map_index_ = i; break; }
    }

    const std::string& culture = world_->region().culture_set();

    std::cout << "Loaded map '" << world_->region().name()
              << "' (culture: " << (culture.empty() ? "unknown" : culture)
              << ", " << world_->region().width() << "x" << world_->region().height() << ")\n";

    terrain_renderer_.emplace(renderer_, world_->region());
    terrain_tileset_ = render::TerrainTileset::load(
        renderer_, registry_->game_data_dir(), *registry_, culture);
    terrain_renderer_->set_tileset(&terrain_tileset_.value());

    // Load the HQ building sprite for this culture.
    const std::string hq_sprite_id = "bldg." + culture + ".head";
    for (const data::SpriteEntry& sprite : registry_->manifest().sprites) {
        if (sprite.id == hq_sprite_id) {
            hq_sprite_.texture = render::Texture::load(
                renderer_, registry_->game_data_dir() / sprite.file);
            hq_sprite_.anchor_x = static_cast<float>(sprite.anchor_x);
            hq_sprite_.anchor_y = static_cast<float>(sprite.anchor_y);
            break;
        }
    }

    // Center the camera on the first player's starting headquarters.
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
    cons_skin_.scrollbar   = make_skin_sprite("ui.a_ui.stdc.vscr");

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
                        if (construction_mode_.is_active()) {
                            // Exit construction mode, keep build menu open.
                            construction_mode_.exit();
                            sim_paused_ = false;
                            if (build_menu_ptr_) {
                                auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
                                m->set_construction_mode_active(false);
                                m->set_confirm_visible(false);
                            }
                        } else if (ui_manager_.has_open_dialogs()) {
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

        case SDL_MOUSEMOTION:
            if (construction_mode_.is_active()) {
                int tx = 0, ty = 0;
                if (pick_tile_from_mouse(event.motion.x, event.motion.y, tx, ty))
                    construction_mode_.on_mouse_move(tx, ty);
            }
            return false;  // don't consume — let camera hover work

        case SDL_MOUSEBUTTONDOWN: {
            if (!construction_mode_.is_active()) break;
            int tx = 0, ty = 0;
            if (!pick_tile_from_mouse(event.button.x, event.button.y, tx, ty)) break;
            construction_mode_.on_mouse_move(tx, ty);

            if (event.button.button == SDL_BUTTON_LEFT) {
                const bool confirmed = construction_mode_.on_left_click();
                sim_paused_ = construction_mode_.is_active();
                if (confirmed) {
                    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                                "Placement confirmed: '%s' at tile (%d, %d)",
                                construction_mode_.selected_id().c_str(), tx, ty);
                    // TODO Stage 2+: spawn placed building entity here.
                }
                if (build_menu_ptr_) {
                    auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
                    m->set_construction_mode_active(construction_mode_.is_active());
                    m->set_confirm_visible(
                        construction_mode_.phase() == ConstructionPhase::BuildingPinned);
                }
            } else if (event.button.button == SDL_BUTTON_RIGHT) {
                construction_mode_.on_right_click();
                sim_paused_ = construction_mode_.is_active();
                if (build_menu_ptr_) {
                    auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
                    m->set_construction_mode_active(construction_mode_.is_active());
                    m->set_confirm_visible(
                        construction_mode_.phase() == ConstructionPhase::BuildingPinned);
                }
            }
            return true;
        }

        case SDL_MOUSEWHEEL:
            if (event.wheel.y != 0) {
                float old_zoom = camera_.zoom;
                camera_.set_zoom(camera_.zoom +
                                 (event.wheel.y > 0 ? kZoomStep : -kZoomStep));
                float new_zoom = camera_.zoom;
                int win_w = 0, win_h = 0;
                SDL_GetWindowSize(window_, &win_w, &win_h);
                float cx = win_w / 2.0f;
                float cy = win_h / 2.0f;
                camera_.world_pixel_offset.x += cx / old_zoom - cx / new_zoom;
                camera_.world_pixel_offset.y += cy / old_zoom - cy / new_zoom;
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
        render_construction_overlays();  // after terrain, before buildings/decorations
        render_decorations();
        render_buildings();
    }

    render_hud_overlay();  // before UI widgets so they draw on top
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
    if (build_menu_ptr_) return;  // B key while open: no-op (keep it open)

    if (!build_menu_data_built_) {
        rebuild_build_menu_data();
        build_menu_data_built_ = true;
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);

    auto menu = std::make_unique<ui::BuildMenu>(cons_skin_);
    menu->set_data(build_menu_data_);
    menu->set_construction_mode_active(construction_mode_.is_active());
    menu->set_confirm_visible(
        construction_mode_.phase() == ConstructionPhase::BuildingPinned);

    menu->on_item_selected = [this](const std::string& id) {
        on_build_menu_item_selected(id);
    };
    menu->on_confirm_clicked = [this]() {
        // Confirm = same as a second left-click (already pinned).
        construction_mode_.on_left_click();
        sim_paused_ = construction_mode_.is_active();
        if (build_menu_ptr_) {
            auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
            m->set_construction_mode_active(false);
            m->set_confirm_visible(false);
        }
    };
    menu->on_exit_clicked = [this]() {
        // Exit construction mode but keep the build menu open.
        construction_mode_.exit();
        sim_paused_ = false;
        if (build_menu_ptr_) {
            auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
            m->set_construction_mode_active(false);
            m->set_confirm_visible(false);
        }
    };

    build_menu_ptr_ = menu.get();
    ui_manager_.open(std::move(menu), win_w, win_h);
}

void GameplayScene::rebuild_build_menu_data() {
    build_menu_data_ = {};

    // Tab 0 — pathways (hardcoded, matching original order).
    build_menu_data_.tabs[0] = {
        {"trai", "Trail",    "", 0, true},
        {"road", "Road",     "", 0, true},
        {"rail", "Railway",  "", 0, true},
        {"cana", "Canal",    "", 0, true},
    };

    // Tabs 1-4 — buildings filtered by type.
    for (const auto& [id, bldg] : registry_->buildings()) {
        int tab = -1;
        if      (bldg.type == "mark")                     tab = 1;
        else if (bldg.type == "bdep" || bldg.type == "ware") tab = 2;
        else if (bldg.type == "bpro")                     tab = 3;
        else if (bldg.type == "bdem")                     tab = 4;
        if (tab < 0) continue;
        build_menu_data_.tabs[tab].push_back({id, bldg.name, bldg.desc, bldg.build_cost, false});
    }

    for (int t = 1; t < ui::BuildMenuData::kNumTabs; ++t) {
        std::sort(build_menu_data_.tabs[t].begin(), build_menu_data_.tabs[t].end(),
                  [](const ui::BuildMenuEntry& a, const ui::BuildMenuEntry& b) {
                      return a.label < b.label;
                  });
    }
}

void GameplayScene::on_build_menu_item_selected(const std::string& id) {
    if (id.empty()) return;
    static const std::array<const char*, 4> kPathIds = {"trai", "road", "rail", "cana"};
    const bool is_path = std::any_of(kPathIds.begin(), kPathIds.end(),
                                     [&](const char* p) { return id == p; });
    if (is_path) {
        construction_mode_.enter_trail(id);
    } else {
        construction_mode_.enter_building(id);
    }
    sim_paused_ = true;
    if (build_menu_ptr_) {
        auto* m = static_cast<ui::BuildMenu*>(build_menu_ptr_);
        m->set_construction_mode_active(true);
        m->set_confirm_visible(false);  // becomes true only after pinning
    }
}

void GameplayScene::render_construction_overlays() {
    if (!construction_mode_.is_active() || !terrain_renderer_) return;

    std::vector<render::OverlayTileSet> overlays;

    const ConstructionPhase ph = construction_mode_.phase();

    if (ph == ConstructionPhase::BuildingAiming || ph == ConstructionPhase::BuildingPinned) {
        const std::string& bid = construction_mode_.selected_id();
        const auto it = registry_->buildings().find(bid);
        if (it != registry_->buildings().end()) {
            const data::Building& bldg = it->second;
            const int ptx = (ph == ConstructionPhase::BuildingPinned)
                                ? construction_mode_.pinned_tx()
                                : construction_mode_.cursor_tx();
            const int pty = (ph == ConstructionPhase::BuildingPinned)
                                ? construction_mode_.pinned_ty()
                                : construction_mode_.cursor_ty();

            render::OverlayTileSet ots;
            ots.center_tx          = ptx;
            ots.center_ty          = pty;
            ots.footprint_w        = bldg.footprint.width;
            ots.footprint_h        = bldg.footprint.height;
            ots.exclusion_shape_id = bldg.exclusion_shape_id;
            ots.exclusion_color    = {80,  200, 80,  100};
            ots.footprint_color    = {255, 230, 60,  180};
            overlays.push_back(ots);
        }
    } else if (ph == ConstructionPhase::TrailPlacing) {
        // Cursor preview tile.
        render::OverlayTileSet cursor;
        cursor.center_tx    = construction_mode_.cursor_tx();
        cursor.center_ty    = construction_mode_.cursor_ty();
        cursor.footprint_color = {80, 180, 255, 140};
        overlays.push_back(cursor);

        // Committed trail markers.
        for (const TrailMarker& m : construction_mode_.trail_markers()) {
            render::OverlayTileSet ms;
            ms.center_tx       = m.tx;
            ms.center_ty       = m.ty;
            ms.footprint_color = {60, 140, 255, 200};
            overlays.push_back(ms);
        }
    }

    if (!overlays.empty())
        overlay_renderer_.render(renderer_, camera_, *terrain_renderer_, overlays);
}

void GameplayScene::render_hud_overlay() {
    if (!sim_paused_) return;
    render::BitmapFont* font = ui_manager_.font();
    if (!font) return;

    static constexpr SDL_Color kTextColor  = {255, 220,  60, 255};
    static constexpr SDL_Color kTextShadow = {  0,   0,   0, 255};

    const char* msg = "Paused: Construction Mode";
    const int tw = font->measure_text(msg);
    const int asc = font->ascender();
    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);
    font->draw_text_shadowed(renderer_, msg,
                             win_w - tw - 12,
                             win_h - asc - font->descender() - 12,
                             kTextColor, kTextShadow);
}

bool GameplayScene::pick_tile_from_mouse(int screen_x, int screen_y,
                                          int& out_tx, int& out_ty) const {
    if (!world_) return false;
    // screen → world (inverse camera projection)
    const float wx = static_cast<float>(screen_x) / camera_.zoom
                     + camera_.world_pixel_offset.x;
    const float wy = static_cast<float>(screen_y) / camera_.zoom
                     + camera_.world_pixel_offset.y;
    const render::Vec2 tile = render::world_to_tile(wx, wy);
    out_tx = static_cast<int>(std::floor(tile.x));
    out_ty = static_cast<int>(std::floor(tile.y));
    return true;
}

// ---------------------------------------------------------------------------
// Dev GUI
// ---------------------------------------------------------------------------

void GameplayScene::render_dev_gui() {
    ImGui::Begin("Dev Tools", &show_dev_gui_);

    if (ImGui::CollapsingHeader("Map", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (world_) {
            ImGui::TextDisabled("%-12s  culture: %s",
                current_map_id_.c_str(),
                world_->region().culture_set().c_str());
        } else {
            ImGui::TextDisabled("(no map loaded)");
        }

        const auto& maps = registry_->manifest().maps;
        if (!maps.empty()) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginListBox("##maplist", ImVec2(0, 200))) {
                for (int i = 0; i < static_cast<int>(maps.size()); ++i) {
                    const bool selected = (i == selected_map_index_);
                    if (ImGui::Selectable(maps[i].id.c_str(), selected))  {
                        if (maps[i].id != current_map_id_) {
                            load_map(maps[i].id);
                        }
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }
        } else {
            ImGui::TextDisabled("No maps in manifest.");
        }
    }

    ImGui::Separator();
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
