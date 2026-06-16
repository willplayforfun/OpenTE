#include "render/area_overlay.h"

#include "render/iso.h"

namespace opente::render {

// ---------------------------------------------------------------------------
// RE-confirmed shape tables. Row r of shape_id N covers dy = r - N
// (i.e. dy ranges from -N to N-1 across 2N rows).
// ---------------------------------------------------------------------------
const AreaOverlayRenderer::DiamondExtent AreaOverlayRenderer::kShapeExtents[4][20] = {
    // shape_id 4 — 8 rows, dy in [-4, +3]
    {{-2,+1},{-3,+2},{-4,+3},{-4,+3},{-4,+3},{-4,+3},{-3,+2},{-2,+1}},
    // shape_id 6 — 12 rows, dy in [-6, +5]
    {{-2,+1},{-4,+3},{-5,+4},{-5,+4},{-6,+5},{-6,+5},
     {-6,+5},{-6,+5},{-5,+4},{-5,+4},{-4,+3},{-2,+1}},
    // shape_id 8 — 16 rows, dy in [-8, +7]
    {{-3,+2},{-5,+4},{-6,+5},{-7,+6},{-7,+6},{-8,+7},{-8,+7},{-8,+7},
     {-8,+7},{-8,+7},{-8,+7},{-7,+6},{-7,+6},{-6,+5},{-5,+4},{-3,+2}},
    // shape_id 10 — 20 rows, dy in [-10, +9]
    {{-3,+2},{-5,+4},{-7,+6},{-8,+7},{-8,+7},{-9,+8},{-9,+8},{-10,+9},
     {-10,+9},{-10,+9},{-10,+9},{-10,+9},{-10,+9},{-9,+8},{-9,+8},{-8,+7},
     {-8,+7},{-7,+6},{-5,+4},{-3,+2}},
};

const int AreaOverlayRenderer::kShapeHalfHeights[4] = {4, 6, 8, 10};

int AreaOverlayRenderer::shape_index(int shape_id) {
    if (shape_id == 4)  return 0;
    if (shape_id == 6)  return 1;
    if (shape_id == 8)  return 2;
    if (shape_id == 10) return 3;
    return -1;
}

// ---------------------------------------------------------------------------
// render_tile_quad — one colored isometric tile quad with height displacement.
// Uses the same vertex layout as TerrainRenderer: the 4 grid-corner vertices
// of tile (tx,ty) are at grid positions (tx,ty),(tx+1,ty),(tx+1,ty+1),(tx,ty+1).
// ---------------------------------------------------------------------------
void AreaOverlayRenderer::render_tile_quad(SDL_Renderer* renderer,
                                           const Camera& camera,
                                           const TerrainRenderer& terrain,
                                           int tx, int ty,
                                           SDL_Color color) {
    const int gx[4] = {tx,   tx+1, tx+1, tx  };
    const int gy[4] = {ty,   ty,   ty+1, ty+1};

    SDL_Vertex verts[4];
    for (int i = 0; i < 4; ++i) {
        Vec2 wp = tile_to_world(static_cast<float>(gx[i]),
                                static_cast<float>(gy[i]));
        wp.y -= terrain.sample_height(gx[i], gy[i]) * kPixelsPerAltiUnit;
        Vec2 sp = camera.world_to_screen(wp);
        verts[i].position  = {sp.x, sp.y};
        verts[i].color     = color;
        verts[i].tex_coord = {0.0f, 0.0f};
    }

    const int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_RenderGeometry(renderer, nullptr, verts, 4, indices, 6);
}

// ---------------------------------------------------------------------------
// render
// ---------------------------------------------------------------------------
void AreaOverlayRenderer::render(SDL_Renderer* renderer,
                                 const Camera& camera,
                                 const TerrainRenderer& terrain,
                                 const std::vector<OverlayTileSet>& overlays) {
    for (const OverlayTileSet& ots : overlays) {
        // 1. Exclusion diamond (drawn first so the footprint renders on top).
        //    Center = anchor + half footprint, so for 1×1 market buildings = anchor.
        const int ex_cx = ots.anchor_tx + ots.footprint_w / 2;
        const int ex_cy = ots.anchor_ty + ots.footprint_h / 2;
        const int si = shape_index(ots.exclusion_shape_id);
        if (si >= 0) {
            const int half   = kShapeHalfHeights[si];
            const int n_rows = half * 2;
            for (int r = 0; r < n_rows; ++r) {
                const int dy = r - half;
                const DiamondExtent& ext = kShapeExtents[si][r];
                for (int dx = ext.min_dx; dx <= ext.max_dx; ++dx) {
                    render_tile_quad(renderer, camera, terrain,
                                     ex_cx + dx, ex_cy + dy,
                                     kOverlayExclusion);
                }
            }
        }

        // 2. Footprint rectangle on top (anchor = top-left corner).
        const SDL_Color fp_color = (ots.color_override.a > 0)
                                       ? ots.color_override
                                       : (ots.is_valid ? kOverlayValid : kOverlayInvalid);
        for (int dfy = 0; dfy < ots.footprint_h; ++dfy) {
            for (int dfx = 0; dfx < ots.footprint_w; ++dfx) {
                render_tile_quad(renderer, camera, terrain,
                                 ots.anchor_tx + dfx, ots.anchor_ty + dfy,
                                 fp_color);
            }
        }
    }
}

}  // namespace opente::render
