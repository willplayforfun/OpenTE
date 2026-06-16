#include "ui/hud.h"

#include <functional>
#include <memory>

namespace opente::ui {

namespace {

// Dark stone bar (placeholder for tool.topb / tool.botb).
constexpr SDL_Color kBarBg      = { 48,  44,  38, 255};
// Inset field background (treasury, speed display, name, date, score).
constexpr SDL_Color kFieldBg    = { 28,  26,  22, 200};
// Hotkey display band (tool.butt / tool.rebo area in barb).
constexpr SDL_Color kHotkeyBg   = { 30,  28,  24, 200};
// Label text colour (warm off-white).
constexpr SDL_Color kText       = {220, 205, 175, 255};
// Mode button colours (stone appearance).
constexpr SDL_Color kBtnNorm    = { 58,  53,  46, 230};
constexpr SDL_Color kBtnHover   = { 78,  72,  62, 240};
constexpr SDL_Color kBtnPress   = { 36,  33,  28, 240};

std::unique_ptr<Panel> inset_field() {
    return std::make_unique<Panel>(kFieldBg);
}

std::unique_ptr<Label> lbl(const char* text) {
    return std::make_unique<Label>(text, kText);
}

std::unique_ptr<Button> mode_btn(const char* text) {
    return std::make_unique<Button>(text, std::function<void()>{},
                                   kBtnNorm, kBtnHover, kBtnPress);
}

}  // namespace

HudBars::HudBars()
    : top_bar_(kBarBg), bottom_bar_(kBarBg)
{
    // ── Top bar (bart) — all coords local to the 1024×20 bar ───────────────
    // Treasury / player gold  (8, 2, 129, 18)
    top_bar_.add_child(inset_field(),          {  8, 2, 129, 18});
    top_bar_.add_child(lbl("Gold: ---"),       {  8, 2, 129, 18});

    // Speed –- button         (146, 1, 22, 18)
    top_bar_.add_child(mode_btn("<"),          {146, 1,  22, 18});

    // Speed display / selector (173, 1, 161, 20)
    top_bar_.add_child(inset_field(),          {173, 1, 161, 20});
    top_bar_.add_child(lbl("Speed: Normal"),   {173, 1, 161, 20});

    // Confirm-name button      (429, 1, 22, 18)
    top_bar_.add_child(mode_btn("OK"),         {429, 1,  22, 18});

    // Player name display      (458, 1, 185, 20)
    top_bar_.add_child(inset_field(),          {458, 1, 185, 20});
    top_bar_.add_child(lbl("Player"),          {458, 1, 185, 20});

    // In-game date / era       (760, 2, 110, 18)
    top_bar_.add_child(inset_field(),          {760, 2, 110, 18});
    top_bar_.add_child(lbl("Year 0"),          {760, 2, 110, 18});

    // Score / ranking          (884, 2, 100, 18)
    top_bar_.add_child(inset_field(),          {884, 2, 100, 18});
    top_bar_.add_child(lbl("Score: 0"),        {884, 2, 100, 18});

    // Options / game-menu nub  (986, 3, 12, 12) — too small for text
    top_bar_.add_child(mode_btn(""),           {986, 3,  12, 12});

    // ── Bottom bar (barb) — all coords local to the 1024×55 bar ────────────
    // Left mode-button group (y = 14)
    bottom_bar_.add_child(mode_btn("Play"),    { 28, 14, 35, 33});  // tool.play
    bottom_bar_.add_child(mode_btn("Route"),   { 84, 14, 35, 33});  // tool.rout
    bottom_bar_.add_child(mode_btn("Build"),   {141, 14, 35, 33});  // tool.cons
    bottom_bar_.add_child(mode_btn("Tech"),    {197, 14, 35, 33});  // tool.tech

    // Hotkey display band (hot3 sub-view: x=271, y=12, w=482, h=29)
    // tool.butt (370×15) + tool.rebo (182×5) are rendered inside this band.
    bottom_bar_.add_child(std::make_unique<Panel>(kHotkeyBg),
                                               {271, 12, 482, 29});

    // Right mode-button group (y = 12)
    bottom_bar_.add_child(mode_btn("Terr"),    {765, 12,  35, 33});  // tool.terr
    bottom_bar_.add_child(mode_btn("Region"),  {816, 12,  36, 33});  // tool.regi
    bottom_bar_.add_child(mode_btn("World"),   {866, 12,  35, 33});  // tool.wmap
    bottom_bar_.add_child(mode_btn("Menu"),    {966, 12,  35, 33});  // tool.game
}

void HudBars::layout(Rect bounds) {
    bounds_ = bounds;
    top_bar_.layout(   {bounds.x, bounds.y,                          bounds.w, kTopBarH   });
    bottom_bar_.layout({bounds.x, bounds.y + bounds.h - kBottomBarH, bounds.w, kBottomBarH});
}

void HudBars::render(SDL_Renderer* renderer, FontCache& fonts) const {
    top_bar_.render(renderer, fonts);
    bottom_bar_.render(renderer, fonts);
}

bool HudBars::handle_event(const SDL_Event& e) {
    if (top_bar_.handle_event(e))    return true;
    if (bottom_bar_.handle_event(e)) return true;
    return false;
}

}  // namespace opente::ui
