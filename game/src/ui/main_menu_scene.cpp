#include "ui/main_menu_scene.h"

#include <string>

#include "render/font.h"

namespace opente::ui {

// Normal text: 0x002fbdfc COLORREF (BBGGRR) → R=0xfc G=0xbd B=0x2f (golden amber).
static constexpr SDL_Color kGold   = {252, 189,  47, 255};
// Hover/pressed text: plain white.
static constexpr SDL_Color kWhite  = {255, 255, 255, 255};
// Greyed-out for disabled buttons.
static constexpr SDL_Color kDim    = {100,  75,  19, 160};
// Drop shadow behind all button text.
static constexpr SDL_Color kShadow = {  0,   0,   0, 255};

// See documentation/main-menu-re.md "Widget rectangles".
const MainMenuScene::Button MainMenuScene::kButtons[] = {
    // id      rect                    label           pt  enabled
    {"game", {655, 135, 354, 30}, "New Game",    24, true },
    {"load", {655, 184, 354, 30}, "Load Game",   24, true },
    {"tuto", {655, 239, 354, 30}, "Tutorial",    24, true },
    {"quit", {655, 293, 354, 30}, "Quit",        24, true },
    {"save", {655, 344, 354, 30}, "Save Game",   24, false},  // no active game
    {"resu", {655, 395, 354, 30}, "Resume",      24, false},  // no active game
    {"cred", {655, 592, 200, 25}, "Credits",     14, true },
    {"high", {655, 628, 200, 25}, "High Scores", 14, true },
    {"opts", {655, 664, 200, 25}, "Options",     14, true },
};
const int MainMenuScene::kButtonCount =
    static_cast<int>(sizeof(kButtons) / sizeof(kButtons[0]));

MainMenuScene::MainMenuScene(SDL_Window* window,
                              SDL_Renderer* renderer,
                              const data::DataRegistry& registry)
    : window_(window),
      renderer_(renderer)
{
    SDL_RenderSetLogicalSize(renderer_, kLogicalW, kLogicalH);

    const auto ui_dir    = registry.game_data_dir() / "sprites" / "ui" / "main";
    const auto fonts_dir = registry.game_data_dir() / "fonts";

    lbak_ = render::Texture::load(renderer_, ui_dir / "lbak.png");

    fonts_.init(renderer_, fonts_dir);
}

MainMenuScene::~MainMenuScene() {
    // Remove logical scaling so the next scene sees the real window size.
    SDL_RenderSetLogicalSize(renderer_, 0, 0);
}

bool MainMenuScene::handle_event(const SDL_Event& event) {
    switch (event.type) {
    case SDL_MOUSEMOTION:
        hovered_ = hit_button(event.motion.x, event.motion.y);
        break;

    case SDL_MOUSEBUTTONDOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            int idx = hit_button(event.button.x, event.button.y);
            if (idx >= 0 && kButtons[idx].enabled)
                pressed_ = idx;
        }
        break;

    case SDL_MOUSEBUTTONUP:
        if (event.button.button == SDL_BUTTON_LEFT) {
            int idx = hit_button(event.button.x, event.button.y);
            if (pressed_ >= 0 && pressed_ == idx && kButtons[idx].enabled)
                activate(idx);
            pressed_ = -1;
        }
        break;

    case SDL_KEYDOWN:
        if (event.key.keysym.sym == SDLK_ESCAPE)
            wants_quit_ = true;
        break;

    default:
        break;
    }
    return false;
}

void MainMenuScene::update(float /*dt*/) {}

void MainMenuScene::render() {
    SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 255);
    SDL_RenderClear(renderer_);
    render_background();
    render_buttons();
}

void MainMenuScene::render_background() {
    if (!lbak_.valid()) return;
    SDL_RenderCopy(renderer_, lbak_.handle(), nullptr, nullptr);
}

void MainMenuScene::render_buttons() {
    for (int i = 0; i < kButtonCount; ++i) {
        const Button& btn = kButtons[i];
        const bool is_hovered = (hovered_ == i) && btn.enabled;
        const bool is_pressed = (pressed_ == i) && btn.enabled;

        render::BitmapFont* font = fonts_.get("seri", btn.font_pt);
        if (!font) continue;

        // Disabled → dim gold; hover/press → white; normal → gold.
        SDL_Color color;
        if (!btn.enabled)   color = kDim;
        else if (is_hovered || is_pressed) color = kWhite;
        else                color = kGold;

        const int text_w = font->measure_text(btn.label);
        int text_x    = btn.rect.x + (btn.rect.w - text_w) / 2;
        int baseline_y = btn.rect.y +
            (btn.rect.h + font->ascender() - font->descender()) / 2;

        // On press: shift +1 right +1 down
        if (is_pressed) {
            text_x     += 1;
            baseline_y += 1;
        }

        font->draw_text_shadowed(renderer_, btn.label,
                                  text_x, baseline_y, color, kShadow);
    }
}

int MainMenuScene::hit_button(int mx, int my) const {
    for (int i = 0; i < kButtonCount; ++i) {
        const SDL_Rect& r = kButtons[i].rect;
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h)
            return i;
    }
    return -1;
}

void MainMenuScene::activate(int idx) {
    const char* id = kButtons[idx].id;
    if (std::string(id) == "quit")
        wants_quit_ = true;
    else if (std::string(id) == "game")
        wants_start_game_ = true;
    // load, tuto, high, opts, cred: not yet implemented.
}

}  // namespace opente::ui
