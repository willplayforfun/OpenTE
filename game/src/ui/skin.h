#pragma once
#include <SDL.h>

namespace opente::ui {

/// One sprite used by a UI skin: a borrowed texture pointer plus the sprite's
/// dimensions and its anchor offset within the dialog's local coordinate system.
///
/// Anchor convention (identical to world-sprite anchors, but in screen space):
///   All sprites in a dialog share a single "dialog origin" (reference point).
///   To draw sprite S, compute:
///     screen_x = dialog_origin.x + S.anchor_x
///     screen_y = dialog_origin.y + S.anchor_y
///   The background sprite defines where dialog_origin falls:
///     dialog_origin = (bg_screen_x - bg.anchor_x, bg_screen_y - bg.anchor_y)
///   So positioning the background at (menu_x, menu_y) gives:
///     dialog_origin = (menu_x - bg.anchor_x, menu_y - bg.anchor_y)
///
/// The texture pointer is borrowed — the owning render::Texture RAII wrapper
/// lives in App.
struct SkinSprite {
    SDL_Texture* tex = nullptr;
    int w = 0;
    int h = 0;
    int anchor_x = 0;
    int anchor_y = 0;

    bool valid() const { return tex != nullptr; }
};

/// Skinning data for the construction (build) panel, loaded from
/// `ui.a_ui.cons.*` sprites extracted from `a_ui,6.{}`.
///
/// Verified sprite sizes and anchors (from manifest):
///   background  306×696  anchor=(1,17)   — full panel frame
///   selection   240×20   anchor=(15,82)  — row hover/selection bar
///   confirm_btn  64×29   anchor=(24,635) — place/confirm button
///   cancel_btn   64×29   anchor=(25,665) — cancel/close button
struct ConsSkin {
    SkinSprite background;   // ui.a_ui.cons.back
    SkinSprite selection;    // ui.a_ui.cons.sele
    SkinSprite confirm_btn;  // ui.a_ui.cons.conf
    SkinSprite cancel_btn;   // ui.a_ui.cons.canc

    bool valid() const { return background.valid(); }
};

}  // namespace opente::ui
