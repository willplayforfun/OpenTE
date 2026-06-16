#pragma once

#include <SDL.h>
#include <vector>

#include "render/camera.h"
#include "render/terrain_renderer.h"

namespace opente::render {

// Placement-highlight colors (observed directly in the original game).
// The prior RE analysis found wrong constants for valid/exclusion.
// Alphas (0x33 for footprint, 0x66 for exclusion) are still from the EXE.
//   Footprint valid:   green        — D3D ARGB 0x3300ff00 (approx)
//   Footprint invalid: red          — D3D ARGB 0x33ff0000 (RE-confirmed)
//   Exclusion diamond: orange/gold  — D3D ARGB 0x66ffc000 (approx)
inline constexpr SDL_Color kOverlayExclusion = {255, 192,   0, 102};  // orange-gold at 40%
inline constexpr SDL_Color kOverlayValid     = {  0, 210,   0,  51};  // green at 20%
inline constexpr SDL_Color kOverlayInvalid   = {255,   0,   0,  51};  // red   at 20%

/// Description of one colored tile region to overlay on the terrain surface.
struct OverlayTileSet {
    // Top-left corner of the building footprint (cursor tile = placement anchor).
    int anchor_tx = 0;
    int anchor_ty = 0;
    int footprint_w = 1;         // footprint rectangle width  (tiles)
    int footprint_h = 1;         // footprint rectangle height (tiles)
    int exclusion_shape_id = 0;  // 0 = footprint only; 4/6/8/10 = add exclusion diamond

    // Controls footprint tile color when color_override.a == 0:
    //   true  → kOverlayValid   (cyan, A=51)   — RE-confirmed HiliteSquare valid path
    //   false → kOverlayInvalid (red,  A=51)   — RE-confirmed HiliteSquare blocked path
    bool is_valid = true;

    // Optional explicit footprint color. When .a > 0, overrides the is_valid color.
    // Use for trail-marker tiles or other non-validity-based highlights.
    SDL_Color color_override = {0, 0, 0, 0};
};

/// Draws semi-transparent tile-footprint and exclusion-diamond overlays on the
/// terrain surface. Call render() once per frame after terrain but before
/// buildings/UI. Height-displacement matches terrain_renderer's own mesh.
class AreaOverlayRenderer {
public:
    void render(SDL_Renderer* renderer,
                const Camera& camera,
                const TerrainRenderer& terrain,
                const std::vector<OverlayTileSet>& overlays);

private:
    struct DiamondExtent { int min_dx, max_dx; };

    // RE-confirmed shape tables (EXE VA 0x632ab4..0x632bd4).
    // kShapeExtents[i] is indexed by shape_id_index = (shape_id - 4) / 2.
    static const DiamondExtent kShapeExtents[4][20];
    static const int           kShapeHalfHeights[4];  // {4, 6, 8, 10}

    // Returns the table index (0-3) for shape_id 4/6/8/10, or -1 otherwise.
    static int shape_index(int shape_id);

    // Draws one isometric tile quad with height displacement, no texture.
    void render_tile_quad(SDL_Renderer* renderer,
                          const Camera& camera,
                          const TerrainRenderer& terrain,
                          int tx, int ty,
                          SDL_Color color);
};

}  // namespace opente::render
