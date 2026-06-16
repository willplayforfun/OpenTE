#pragma once

#include <SDL.h>

#include <map>
#include <optional>
#include <string>

#include "core/scene.h"
#include "data/registry.h"
#include "gameplay/construction_mode.h"
#include "render/area_overlay.h"
#include "render/camera.h"
#include "render/terrain_renderer.h"
#include "render/terrain_tileset.h"
#include "render/texture.h"
#include "ui/build_menu.h"
#include "ui/manager.h"
#include "ui/skin.h"
#include "world/world.h"

namespace opente::ui { class HudBars; }

namespace opente::gameplay {

/// The main in-game view: isometric map, camera pan/zoom, build menu, and
/// dev-overlay tools. Receives a loaded DataRegistry from App and owns
/// everything else it needs (world, terrain, sprites, UIManager).
class GameplayScene : public core::Scene {
public:
    GameplayScene(SDL_Window* window,
                  SDL_Renderer* renderer,
                  const data::DataRegistry& registry,
                  const std::string& map_id);

    ~GameplayScene();

    bool handle_event(const SDL_Event& event) override;
    void update(float dt) override;
    void render() override;
    bool wants_quit()      const override { return wants_quit_; }
    bool wants_main_menu() const override { return wants_main_menu_; }

private:
    struct AnchoredSprite {
        render::Texture texture;
        float anchor_x = 0.0f;
        float anchor_y = 0.0f;
    };

    void load_map(const std::string& map_id);
    void load_sprites();
    void render_decorations();
    void render_buildings();
    void toggle_build_menu();
    void render_dev_gui();
    void render_font_test();
    void render_lighting_window();

    // Construction mode helpers.
    void rebuild_build_menu_data();
    void on_build_menu_item_selected(const std::string& id);
    void render_construction_overlays();
    void render_hud_overlay();
    // Converts screen pixel → isometric tile (no height correction; good enough for Stage 2).
    bool pick_tile_from_mouse(int screen_x, int screen_y, int& out_tx, int& out_ty) const;

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    const data::DataRegistry* registry_ = nullptr;

    std::optional<world::World>           world_;
    std::optional<render::TerrainTileset> terrain_tileset_;
    std::optional<render::TerrainRenderer> terrain_renderer_;
    render::Camera camera_;

    AnchoredSprite                         hq_sprite_;
    std::map<std::string, AnchoredSprite>  decoration_sprites_;

    ui::UIManager                          ui_manager_;
    std::map<std::string, render::Texture> ui_textures_;
    ui::ConsSkin                           cons_skin_;

    bool pan_left_  = false;
    bool pan_right_ = false;
    bool pan_up_    = false;
    bool pan_down_  = false;

    std::string current_map_id_;
    int         selected_map_index_ = 0;

    bool wants_quit_           = false;
    bool wants_main_menu_      = false;
    bool show_dev_gui_         = false;
    bool show_lighting_window_ = false;
    bool show_font_test_       = false;

    ui::Widget*  build_menu_ptr_ = nullptr;
    ui::HudBars* hud_ptr_        = nullptr;

    // Construction mode.
    ConstructionMode        construction_mode_;
    render::AreaOverlayRenderer overlay_renderer_;
    ui::BuildMenuData       build_menu_data_;
    bool                    build_menu_data_built_ = false;
    bool                    sim_paused_            = false;

    struct RenderDefaults {
        float slope_gradient_scale;
        float ambient_r;
        float ambient_g;
        float ambient_b;
        float vertex_color_scale;
        float alti_scale_factor;
        float pixels_per_world_height_unit;
    };
    RenderDefaults render_defaults_ = {};
};

}  // namespace opente::gameplay
