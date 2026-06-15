#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "ui/widget.h"

namespace opente::ui {

// ---------------------------------------------------------------------------
// Panel — container widget
// ---------------------------------------------------------------------------

/// A widget that holds an ordered list of children and optionally fills its
/// bounds with a background colour. Children are stored in render order (first
/// child drawn first, last child on top). layout() propagates to children that
/// have had bounds assigned via add_child(child, rect).
class Panel : public Widget {
public:
    explicit Panel(SDL_Color bg = {0, 0, 0, 0}) : bg_(bg) {}

    void set_background(SDL_Color c) { bg_ = c; }

    /// Adds a child widget whose bounds are specified in panel-local
    /// coordinates (origin = panel top-left). The child's layout() is called
    /// immediately and again whenever the panel's layout() is called.
    void add_child(std::unique_ptr<Widget> child, Rect local_rect);

    void layout(Rect bounds) override;
    void render(SDL_Renderer* renderer, TTF_Font* font) const override;
    bool handle_event(const SDL_Event& e) override;

protected:
    struct Entry {
        std::unique_ptr<Widget> widget;
        Rect local_rect;  // bounds relative to panel origin
    };

    SDL_Color bg_{0, 0, 0, 0};
    std::vector<Entry> children_;

private:
    void place_child(Entry& entry) const;
};

// ---------------------------------------------------------------------------
// Label — static text (or tinted rect if no font is available)
// ---------------------------------------------------------------------------

/// Renders a UTF-8 text string using SDL_ttf. The text texture is cached and
/// only regenerated when the text or font changes. Vertically centred within
/// bounds; horizontally offset by 4 px from the left edge.
///
/// When `font` is null the label draws a semi-transparent tinted rect as a
/// placeholder so the layout remains visible.
class Label : public Widget {
public:
    explicit Label(std::string text, SDL_Color color = {230, 220, 200, 255})
        : text_(std::move(text)), color_(color) {}

    ~Label() override { invalidate_cache(); }

    void set_text(std::string text) {
        if (text != text_) { text_ = std::move(text); invalidate_cache(); }
    }
    const std::string& text() const { return text_; }

    void render(SDL_Renderer* renderer, TTF_Font* font) const override;
    bool handle_event(const SDL_Event& /*e*/) override { return false; }

private:
    void invalidate_cache() const;
    void rebuild_cache(SDL_Renderer* renderer, TTF_Font* font) const;

    std::string text_;
    SDL_Color color_;

    mutable SDL_Texture* tex_ = nullptr;
    mutable int tex_w_ = 0, tex_h_ = 0;
    mutable std::string cached_text_;
    mutable TTF_Font* cached_font_ = nullptr;
};

// ---------------------------------------------------------------------------
// Button — clickable panel with hover/press state
// ---------------------------------------------------------------------------

/// A flat rectangular button that changes shade on hover and press. When
/// released inside the widget bounds the `on_click` callback is invoked.
///
/// Appearance uses flat colours; sprite-skinning is a Stage 8 concern.
class Button : public Widget {
public:
    Button(std::string label, std::function<void()> on_click,
           SDL_Color bg_normal  = {55,  55,  75,  210},
           SDL_Color bg_hover   = {80,  80, 110,  220},
           SDL_Color bg_pressed = {35,  35,  55,  220})
        : label_(std::move(label)),
          on_click_(std::move(on_click)),
          bg_normal_(bg_normal),
          bg_hover_(bg_hover),
          bg_pressed_(bg_pressed) {}

    void layout(Rect bounds) override;
    void render(SDL_Renderer* renderer, TTF_Font* font) const override;
    bool handle_event(const SDL_Event& e) override;

private:
    std::string label_;
    std::function<void()> on_click_;
    SDL_Color bg_normal_, bg_hover_, bg_pressed_;
    bool hovered_ = false;
    bool pressed_ = false;

    Label label_widget_{""};
};

// ---------------------------------------------------------------------------
// ScrollPanel — vertically scrollable single-child container
// ---------------------------------------------------------------------------

/// Clips a single child widget to its own bounds and scrolls it vertically
/// via mouse-wheel events. `content_height` is the logical height of the
/// child; the child is laid out at the full scroll width × content_height.
class ScrollPanel : public Widget {
public:
    explicit ScrollPanel(std::unique_ptr<Widget> content, int content_height)
        : content_(std::move(content)), content_height_(content_height) {}

    void layout(Rect bounds) override;
    void render(SDL_Renderer* renderer, TTF_Font* font) const override;
    bool handle_event(const SDL_Event& e) override;

    /// Scrolls so that a vertical range [top, bottom) is visible.
    void scroll_to_show(int top, int bottom);

private:
    std::unique_ptr<Widget> content_;
    int content_height_;
    int scroll_offset_ = 0;

    static constexpr int kScrollStep = 48;

    void clamp_scroll();
};

}  // namespace opente::ui
