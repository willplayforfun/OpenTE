#pragma once

#include <SDL.h>
#include <vector>

#include "render/camera.h"
#include "render/terrain_renderer.h"

namespace opente::render {

/// Description of one colored tile region to overlay on the terrain surface.
struct OverlayTileSet {
    int center_tx = 0;
    int center_ty = 0;
    int footprint_w = 1;        // footprint rectangle width  (tiles)
    int footprint_h = 1;        // footprint rectangle height (tiles)
    int exclusion_shape_id = 0; // 0 = footprint only; 4/6/8/10 = add exclusion diamond
    SDL_Color exclusion_color = {0, 0, 0, 0};
    SDL_Color footprint_color = {0, 0, 0, 0};
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
