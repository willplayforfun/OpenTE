#pragma once

#include <SDL.h>
#include <SDL_ttf.h>

namespace opente::ui {

/// Screen-space rectangle used for widget bounds and hit-testing.
struct Rect {
    int x = 0, y = 0, w = 0, h = 0;

    bool contains(int px, int py) const noexcept {
        return px >= x && px < x + w && py >= y && py < y + h;
    }
    SDL_Rect to_sdl() const noexcept { return {x, y, w, h}; }
};

/// Base class for all UI widgets. The lifecycle is:
///   1. layout(bounds) — called once (or on window resize) to assign bounds.
///   2. render()/handle_event() — called each frame/event thereafter.
///
/// `font` passed to render() may be null if SDL_ttf initialisation failed or
/// no font file was found; implementations must degrade gracefully (e.g. draw
/// a tinted rect instead of text).
///
/// Coordinates are always screen-space pixels, unaffected by the game camera.
class Widget {
public:
    virtual ~Widget() = default;

    /// Assigns the widget's screen-space bounds. Implementations that contain
    /// children must override this to propagate layout to them.
    virtual void layout(Rect bounds) { bounds_ = bounds; }

    /// Draws the widget. `font` may be null.
    virtual void render(SDL_Renderer* renderer, TTF_Font* font) const = 0;

    /// Handles an SDL event. Returns true if the event was consumed and should
    /// not be forwarded to widgets below this one in the z-order.
    virtual bool handle_event(const SDL_Event& e) = 0;

    const Rect& bounds() const noexcept { return bounds_; }

protected:
    Rect bounds_{};
};

}  // namespace opente::ui
