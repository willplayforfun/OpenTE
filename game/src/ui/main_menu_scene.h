#pragma once

#include <SDL.h>

#include "core/scene.h"
#include "data/registry.h"
#include "render/font_cache.h"
#include "render/texture.h"

namespace opente::ui {

/// Main startup screen — the first thing shown when the game launches.
///
/// Renders the full-screen background (lbak) and the text menu buttons (game,
/// load, tuto, quit, save, resu, cred, high, opts). On hover the text turns
/// white; on press the text shifts +1px right +1px down (from EXE disasm).
/// Button positions and fonts are from the
/// original TMainStartupScreen layout (documentation/main-menu-re.md).
///
/// Uses SDL_RenderSetLogicalSize(1024, 768) so all coordinates match the
/// original game's 1024×768 virtual canvas. The logical size is restored to
/// 0×0 in the destructor so the next scene sees the real window dimensions.
///
/// Transition signals — check these in the main loop *between* frames (never
/// from inside a callback, to avoid destroying the scene while it is on the
/// call stack):
///   wants_start_game() → caller should replace this scene with GameplayScene
///   wants_quit()       → caller should exit
class MainMenuScene : public core::Scene {
public:
    MainMenuScene(SDL_Window* window,
                  SDL_Renderer* renderer,
                  const data::DataRegistry& registry);
    ~MainMenuScene() override;

    bool handle_event(const SDL_Event& event) override;
    void update(float dt) override;
    void render() override;
    bool wants_quit()       const override { return wants_quit_; }
    bool wants_start_game() const          { return wants_start_game_; }

private:
    struct Button {
        const char* id;
        SDL_Rect    rect;      // in 1024×768 logical space
        const char* label;
        int         font_pt;
        bool        enabled;
    };

    static constexpr int kLogicalW = 1024;
    static constexpr int kLogicalH = 768;

    void render_background();
    void render_buttons();
    int  hit_button(int mx, int my) const;
    void activate(int idx);

    SDL_Window*   window_   = nullptr;
    SDL_Renderer* renderer_ = nullptr;

    render::Texture lbak_;

    render::FontCache fonts_;

    int  hovered_  = -1;
    int  pressed_  = -1;
    bool wants_quit_        = false;
    bool wants_start_game_  = false;

    static const Button  kButtons[];
    static const int     kButtonCount;
};

}  // namespace opente::ui
