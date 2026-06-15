#pragma once

#include <SDL.h>

#include <map>
#include <optional>
#include <string>

#include "core/scene.h"
#include "data/registry.h"
#include "render/camera.h"
#include "render/terrain_renderer.h"
#include "render/terrain_tileset.h"
#include "render/texture.h"
#include "ui/manager.h"
#include "ui/skin.h"
#include "world/world.h"

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
    bool wants_quit() const override { return wants_quit_; }

private:
    struct AnchoredSprite {
        render::Texture texture;
        float anchor_x = 0.0f;
        float anchor_y = 0.0f;
    };

    void load_sprites();
    void render_decorations();
    void render_buildings();
    void toggle_build_menu();
    void render_dev_gui();
    void render_font_test();
    void render_lighting_window();

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

    bool wants_quit_           = false;
    bool show_dev_gui_         = false;
    bool show_lighting_window_ = false;
    bool show_font_test_       = false;

    ui::Widget* build_menu_ptr_ = nullptr;

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
