#pragma once

#include "ui/panel.h"
#include "ui/widget.h"

namespace opente::ui {

/// Always-visible HUD chrome: top info bar (bart) and bottom toolbar (barb).
///
/// Pixel layout matches the RE'd original (documentation/toolbar-re.md):
///   Top bar    bart : (0, 0,    w,  20)  — sprite tool.topb
///   Bottom bar barb : (0, h-55, w,  55)  — sprite tool.botb
///   Map viewport    : (10, 24, 951, 681) — not rendered here, for reference
///
/// Sprite skinning (tool.topb / tool.botb / tool.play etc.) is a Stage 8
/// concern.  Until then every slot is a flat-coloured placeholder with a text
/// label that identifies it.
class HudBars : public Widget {
public:
    HudBars();

    void layout(Rect bounds) override;
    void render(SDL_Renderer* renderer, FontCache& fonts) const override;
    bool handle_event(const SDL_Event& e) override;

    static constexpr int kTopBarH    = 20;
    static constexpr int kBottomBarH = 55;

private:
    Panel top_bar_;
    Panel bottom_bar_;
};

}  // namespace opente::ui
