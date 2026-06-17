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
    "ui.a_ui.stdc.sele",   // building-list row highlight (194×17)
    "ui.a_ui.cons.conf",
    "ui.a_ui.cons.canc",
    "ui.a_ui.stdc.vscr",   // shared vertical scrollbar (up/down arrows)
};

// Path-type preview sprites shown in the build-menu preview pane (228×88 each).
// Each pair is {path id, manifest sprite id}.
constexpr const char* kPathSpriteIds[][2] = {
    {"trai", "ui.a_ui.path.trai"},
    {"road", "ui.a_ui.path.road"},
    {"rail", "ui.a_ui.path.rail"},
    {"cana", "ui.a_ui.path.cana"},
};

}  // namespace

GameplayScene::GameplayScene(SDL_Window* window,
                             SDL_Renderer* renderer,
                             const data::DataRegistry& registry,
                             const std::string& episode_id)
    : window_(window), renderer_(renderer), registry_(&registry)
{
    load_sprites();

    if (!ui_manager_.init(renderer_, registry.game_data_dir())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GameplayScene: UIManager::init failed");
    }

    int win_w = 0, win_h = 0;
    SDL_GetWindowSize(window_, &win_w, &win_h);
    auto hud = std::make_unique<ui::HudBars>(renderer_, registry);
    hud_ptr_ = hud.get();
    hud->on_mode_clicked = [this](const std::string& tag) {
        if (tag == "game") {
            wants_main_menu_ = true;
            return;
        }
        if (tag == "terr" || tag == "regi" || tag == "worl") {
            // terr = current view (no-op); regi/worl not implemented yet.
            return;
        }
        // Toggle-group buttons (play/rout/cons/tech).
        // active_mode_ is already updated by the HUD before the callback fires.
        const bool now_active = (hud_ptr_->active_mode() == tag);
        if (tag == "cons") {
            if (now_active) {
                toggle_build_menu();
            } else if (build_menu_ptr_) {
                ui_manager_.close(build_menu_ptr_);
                build_menu_ptr_ = nullptr;
            }
        } else {
            // play/rout/tech: side panels not implemented; dismiss build menu.
            if (build_menu_ptr_) {
                ui_manager_.close(build_menu_ptr_);
                build_menu_ptr_ = nullptr;
            }
        }
    };
    // Top-bar Game-Speed dropdown. Speed control isn't implemented yet, so this
    // is a stub: we just echo the chosen label back into the display. The HUD
    // changes no state itself — every signal arrives here.
    hud->on_speed_selected = [this](int code, const std::string& label) {
        SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION,
                    "Game speed selected: %s (code 0x%x) — not implemented (stub)",
                    label.c_str(), code);
        if (hud_ptr_) hud_ptr_->set_speed(label);
    };
    // Top-bar Region dropdown: switch the active region.
    hud->on_region_selected = [this](int index) {
        if (world_ && index >= 0 && index < world_->region_count())
            activate_region(index);
    };
    ui_manager_.set_hud(std::move(hud), win_w, win_h);

    render_defaults_ = {
        render::kSlopeGradientScale,
        render::kAmbientR,
        render::kAmbientG,
        render::kAmbientB,
        render::kVertexColorScale,
        render::kAltiScaleFactor,
        render::kPixelsPerWorldHeightUnit,
    };

    load_episode(episode_id);
}

void GameplayScene::load_episode(const std::string& episode_id) {
    // Tear down previous episode state (release GPU resources before reallocating).
    for (auto& tr : terrain_renderers_) tr->set_tileset(nullptr);
    terrain_renderers_.clear();
    terrain_tilesets_.clear();
    world_.reset();
    hq_sprite_ = {};

    try {
        world_ = std::make_unique<world::World>(
            world::World::load_episode(*registry_, episode_id));
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "GameplayScene: failed to load episode '%s': %s",
                     episode_id.c_str(), e.what());
        return;
    }

    current_episode_id_ = episode_id;

    // Build one tileset + renderer per region. unique_ptr ownership ensures
    // the TerrainTileset addresses are stable even if the vectors resize.
    for (int i = 0; i < world_->region_count(); ++i) {
        const world::Region& reg = world_->region(i);
        terrain_tilesets_.push_back(std::make_unique<render::TerrainTileset>(
            render::TerrainTileset::load(renderer_, registry_->game_data_dir(),
                                         *registry_, reg.culture_set())));
        terrain_renderers_.push_back(
            std::make_unique<render::TerrainRenderer>(renderer_, reg));
        terrain_renderers_.back()->set_tileset(terrain_tilesets_.back().get());
    }

    // Sync the dev GUI episode listbox selection.
    int idx = 0;
    for (const auto& [ep_id, _] : registry_->episodes()) {
        if (ep_id == episode_id) { selected_episode_index_ = idx; break; }
        ++idx;
    }

    activate_region(0);
}

void GameplayScene::activate_region(int index) {
    active_region_index_ = index;
    const world::Region& reg = world_->region(index);

    const std::string& culture = reg.culture_set();
    std::cout << "Active region '" << reg.name()
              << "' (episode: " << current_episode_id_
              << ", culture: " << (culture.empty() ? "unknown" : culture)
              << ", " << reg.width() << "x" << reg.height() << ")\n";

    if (hud_ptr_) {
        hud_ptr_->set_region_name(reg.name());
        // Feed the top-bar Region dropdown the episode's region list + current.
        std::vector<std::string> names;
        names.reserve(world_->region_count());
        for (int i = 0; i < world_->region_count(); ++i)
            names.push_back(world_->region(i).name());
        hud_ptr_->set_regions(std::move(names), active_region_index_);
    }

    // Load the HQ building sprite for this region's culture.
    hq_sprite_ = {};
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
    if (!reg.regions().empty()) {
        const world::Headquarters& hq = reg.regions().front().headquarters;
        const render::Vec2 hq_world = render::tile_to_world(
            static_cast<float>(hq.x), static_cast<float>(hq.y));
        int win_w = 0, win_h = 0;
        SDL_GetWindowSize(window_, &win_w, &win_h);
        camera_.world_pixel_offset = {
            hq_world.x - win_w / 2.0f,
            hq_world.y - win_h / 2.0f,
        };
    }

    build_menu_data_built_ = false;
}

render::TerrainRenderer* GameplayScene::active_terrain_renderer() const {
    if (terrain_renderers_.empty()) return nullptr;
    return terrain_renderers_[active_region_index_].get();
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

    cons_skin_.background    = make_skin_sprite("ui.a_ui.cons.back");
    cons_skin_.selection     = make_skin_sprite("ui.a_ui.cons.sele");
    cons_skin_.row_selection = make_skin_sprite("ui.a_ui.stdc.sele");
    cons_skin_.confirm_btn   = make_skin_sprite("ui.a_ui.cons.conf");
    cons_skin_.cancel_btn    = make_skin_sprite("ui.a_ui.cons.canc");
    cons_skin_.scrollbar     = make_skin_sprite("ui.a_ui.stdc.vscr");

    if (!cons_skin_.valid()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "cons panel sprites not found — build menu will use flat colours");
    }

    // Path preview sprites — load each and build the SkinSprite map.
    path_skin_sprites_.clear();
    for (const auto& sprite : registry_->manifest().sprites) {
        for (const auto& kv : kPathSpriteIds) {
            if (sprite.id != kv[1]) continue;
            ui_textures_.emplace(sprite.id,
                render::Texture::load(renderer_, game_data_dir / sprite.file));
            const auto& tex = ui_textures_.at(sprite.id);
            if (tex.valid()) {
                path_skin_sprites_[kv[0]] = {
                    tex.handle(), sprite.width, sprite.height,
                    sprite.anchor_x, sprite.anchor_y,
                };
            }
        }
    }
    if (path_skin_sprites_.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "path preview sprites not found — build menu will use placeholder");
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
                                m->clear_selection();
                            }
                        } else if (ui_manager_.has_open_dialogs()) {
                            if (build_menu_ptr_) {
                                ui_manager_.close(build_menu_ptr_);
                                build_menu_ptr_ = nullptr;
                                if (hud_ptr_) hud_ptr_->set_active_mode("");
                            }
                        } else {
                            wants_quit_ = true;
                        }
                    }
                    return true;
                case SDLK_b:
                    if (down) {
                        if (build_menu_ptr_) {
                            ui_manager_.close(build_menu_ptr_);
                            build_menu_ptr_ = nullptr;
                            if (construction_mode_.is_active()) {
                                construction_mode_.exit();
                                sim_paused_ = false;
                            }
                            if (hud_ptr_) hud_ptr_->set_active_mode("");
                        } else {
                            if (hud_ptr_) hud_ptr_->set_active_mode("cons");
                            toggle_build_menu();
                        }
                    }
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
        active_terrain_renderer()->render(camera_);
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
    const world::Region& reg = world_->region(active_region_index_);
    render::TerrainRenderer* tr = active_terrain_renderer();
    for (const world::Decoration& dec : reg.decorations()) {
        const auto it = decoration_sprites_.find(dec.sprite);
        if (it == decoration_sprites_.end() || !it->second.texture.valid()) continue;
        const AnchoredSprite& spr = it->second;

        render::Vec2 world_pos = render::tile_to_world(
            static_cast<float>(dec.x), static_cast<float>(dec.y));
        world_pos.y -= tr->sample_height(dec.x, dec.y)
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

    const world::Region& reg = world_->region(active_region_index_);
    render::TerrainRenderer* tr = active_terrain_renderer();
    for (const world::MapRegion& map_region : reg.regions()) {
        const world::Headquarters& hq = map_region.headquarters;
        render::Vec2 world_pos = render::tile_to_world(
            static_cast<float>(hq.x), static_cast<float>(hq.y));
        world_pos.y -= tr->sample_height(hq.x, hq.y)
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

    // Panel labels from the extracted `strings` table (cons.labl.*). Each
    // lookup falls back to the BuildMenuStrings default (the original literal)
    // if the key is missing. Category order matches the tabs: path/mark/depo/
    // prod/dema.
    ui::BuildMenuStrings bms;
    bms.title         = registry_->text("cons.labl.titl", bms.title);
    bms.categories[0] = registry_->text("cons.labl.path", bms.categories[0]);
    bms.categories[1] = registry_->text("cons.labl.mark", bms.categories[1]);
    bms.categories[2] = registry_->text("cons.labl.depo", bms.categories[2]);
    bms.categories[3] = registry_->text("cons.labl.prod", bms.categories[3]);
    bms.categories[4] = registry_->text("cons.labl.dema", bms.categories[4]);
    bms.confirm       = registry_->text("cons.labl.conf", bms.confirm);
    bms.cancel        = registry_->text("cons.labl.canc", bms.cancel);
    bms.row           = registry_->text("cons.labl.bnam", bms.row);
    menu->set_strings(std::move(bms));
    menu->set_path_sprites(path_skin_sprites_);

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
            m->clear_selection();
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

    // Tabs 1-4 — buildings for the active region's culture set, filtered by type.
    const std::string& culture = world_->region(active_region_index_).culture_set();
    for (const auto& [id, bldg] : registry_->buildings()) {
        if (bldg.culture_set != culture) continue;
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
    render::TerrainRenderer* tr = active_terrain_renderer();
    if (!construction_mode_.is_active() || !tr) return;

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
            ots.anchor_tx          = ptx;
            ots.anchor_ty          = pty;
            ots.footprint_w        = bldg.footprint.width;
            ots.footprint_h        = bldg.footprint.height;
            ots.exclusion_shape_id = bldg.exclusion_shape_id;
            ots.is_valid           = true;  // TODO Stage 3+: run actual placement check
            overlays.push_back(ots);
        }
    } else if (ph == ConstructionPhase::TrailPlacing) {
        const int ctx = construction_mode_.cursor_tx();
        const int cty = construction_mode_.cursor_ty();
        const auto& markers = construction_mode_.trail_markers();
        const world::Region& reg = world_->region(active_region_index_);
        const int map_w = reg.width();
        const int map_h = reg.height();

        // Render actual path sprites for the preview via the terrain renderer.
        if (!markers.empty()) {
            std::vector<std::pair<int,int>> wpts;
            wpts.reserve(markers.size());
            for (const TrailMarker& m : markers)
                wpts.push_back({m.tx, m.ty});
            tr->render_preview_path(wpts, ctx, cty,
                                    construction_mode_.selected_id(), camera_);
        }

        auto add_tile = [&](int tx, int ty, SDL_Color color) {
            if (tx < 0 || tx >= map_w || ty < 0 || ty >= map_h) return;
            render::OverlayTileSet ots;
            ots.anchor_tx      = tx;
            ots.anchor_ty      = ty;
            ots.color_override = color;
            overlays.push_back(ots);
        };

        // Waypoint markers (bright yellow) show where the user clicked.
        constexpr SDL_Color kWaypoint = {255, 220, 60, 180};
        for (const TrailMarker& m : markers)
            add_tile(m.tx, m.ty, kWaypoint);

        // Cursor tile (subtle orange).
        add_tile(ctx, cty, {255, 160, 40, 140});
    }

    if (!overlays.empty())
        overlay_renderer_.render(renderer_, camera_, *tr, overlays);
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
    // screen → world (inverse camera projection). Only world-Y carries the
    // terrain-height displacement — the mesh shifts each vertex up by
    // height·kPixelsPerAltiUnit before zoom (see terrain_renderer.cpp /
    // area_overlay.cpp); world-X is unaffected.
    const float wx = static_cast<float>(screen_x) / camera_.zoom
                     + camera_.world_pixel_offset.x;
    const float wy = static_cast<float>(screen_y) / camera_.zoom
                     + camera_.world_pixel_offset.y;

    // Initial flat pick. This alone matches the original game's cursor
    // transform (TMapView::fcn.004689a0 — a flat isometric inverse with no
    // terrain-height term; see documentation/03-exe-analysis.md Round 19).
    render::Vec2 tile = render::world_to_tile(wx, wy);

    // Height-corrected refinement (better than the original, which used a flat
    // pick + a height-displaced marker). The surface point under the cursor
    // was drawn at world-Y `wy` AFTER its terrain height was subtracted, so the
    // un-displaced world-Y of that surface point is `wy + height·k`. Re-pick
    // with the corrected Y and iterate to convergence so the highlighted tile
    // matches the pixel under the cursor on slopes. Converges in 1-2 steps on
    // smooth terrain; cap at 3 and bail early once the tile stops changing.
    if (const render::TerrainRenderer* tr = active_terrain_renderer()) {
        for (int i = 0; i < 3; ++i) {
            const int stx = static_cast<int>(std::floor(tile.x));
            const int sty = static_cast<int>(std::floor(tile.y));
            const float h = tr->sample_height(
                static_cast<double>(stx) + 0.5, static_cast<double>(sty) + 0.5);
            const render::Vec2 next = render::world_to_tile(
                wx, wy + h * render::kPixelsPerAltiUnit);
            const bool stable = static_cast<int>(std::floor(next.x)) == stx &&
                                static_cast<int>(std::floor(next.y)) == sty;
            tile = next;
            if (stable) break;
        }
    }

    out_tx = static_cast<int>(std::floor(tile.x));
    out_ty = static_cast<int>(std::floor(tile.y));
    return true;
}

// ---------------------------------------------------------------------------
// Dev GUI
// ---------------------------------------------------------------------------

void GameplayScene::render_dev_gui() {
    ImGui::Begin("Dev Tools", &show_dev_gui_);

    if (ImGui::CollapsingHeader("Episode", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (world_) {
            const world::Region& reg = world_->region(active_region_index_);
            ImGui::TextDisabled("Episode: %-6s  Region: %s  Culture: %s",
                current_episode_id_.c_str(),
                reg.name().c_str(),
                reg.culture_set().c_str());
        } else {
            ImGui::TextDisabled("(no episode loaded)");
        }

        const auto& episodes = registry_->episodes();
        if (!episodes.empty()) {
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginListBox("##eplist", ImVec2(0, 120))) {
                int i = 0;
                for (const auto& [ep_id, ep] : episodes) {
                    const bool selected = (i == selected_episode_index_);
                    char label[64];
                    std::snprintf(label, sizeof(label), "%-6s  %s",
                                  ep_id.c_str(), ep.name.c_str());
                    if (ImGui::Selectable(label, selected)) {
                        if (ep_id != current_episode_id_)
                            load_episode(ep_id);
                    }
                    if (selected) ImGui::SetItemDefaultFocus();
                    ++i;
                }
                ImGui::EndListBox();
            }
        } else {
            ImGui::TextDisabled("No episodes in registry.");
        }

        // Region switcher — only shown for multi-region episodes.
        if (world_ && world_->region_count() > 1) {
            ImGui::Separator();
            ImGui::TextUnformatted("Regions:");
            ImGui::SetNextItemWidth(-1);
            if (ImGui::BeginListBox("##reglist", ImVec2(0, 80))) {
                for (int i = 0; i < world_->region_count(); ++i) {
                    const bool selected = (i == active_region_index_);
                    if (ImGui::Selectable(world_->region(i).name().c_str(), selected))
                        activate_region(i);
                    if (selected) ImGui::SetItemDefaultFocus();
                }
                ImGui::EndListBox();
            }
        }
    }

    ImGui::Separator();
    if (auto* tr = active_terrain_renderer()) {
        if (ImGui::CollapsingHeader("Terrain")) {
            ImGui::Checkbox("Shore overlays",    &tr->shore_overlays_enabled);
            ImGui::Checkbox("Slope shading",     &tr->slope_shading_enabled);
            ImGui::Checkbox("Terrain blending",  &tr->terrain_blending_enabled);
            ImGui::Checkbox("Debug labels",      &tr->terrain_debug_labels_enabled);
            if (ImGui::DragFloat("Alti scale factor",
                                 &render::kAltiScaleFactor, 0.1f, 0.5f, 50.0f,
                                 "%.2f (EXE: 10.0)")) {
                render::kAltiToWorldHeight  = render::kAltiScaleFactor / 256.0f;
                render::kPixelsPerAltiUnit  = render::kPixelsPerWorldHeightUnit
                                              * render::kAltiToWorldHeight;
                tr->rebuild_vertex_colors();
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

    render::TerrainRenderer* tr = active_terrain_renderer();
    if (!tr) {
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
        tr->rebuild_vertex_colors();

    ImGui::End();
}

}  // namespace opente::gameplay
