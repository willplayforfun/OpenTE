#pragma once

// Owns and draws the terrain mesh, texture pages, and shore-overlay atlases
// for a single `world::Region`. See OpenTE/spec/rendering.md "Terrain".

#include <SDL.h>

#include <array>
#include <filesystem>
#include <vector>

#include "data/registry.h"
#include "render/camera.h"
#include "render/texture.h"
#include "world/region.h"

namespace opente::render {

/// Builds a per-vertex height-displaced, slope-shaded terrain mesh from a
/// `world::Region`'s heightmap (once, at construction -- the heightmap is
/// immutable at runtime) and renders it with a base terrain texture and
/// shore-overlay passes.
///
/// Episodes with multiple regions: construct one `TerrainRenderer` per
/// region (each keeps its own mesh/textures) and render whichever is
/// currently active.
class TerrainRenderer {
public:
    /// Builds the terrain mesh from `region`'s heightmap. `region` must outlive this object.
    TerrainRenderer(SDL_Renderer* renderer, const world::Region& region);

    /// Loads `tables/terrain_textures.json` and the texture pages it references from `registry`, 
    /// plus the `terrain.coa0`/`terrain.coa1` shore-overlay atlases. 
    /// Call once after construction.
    void load_textures(const std::filesystem::path& game_data_dir, const data::DataRegistry& registry);

    /// Draws the full terrain:
    /// base texture pass, shore-overlay passes, for every tile in the region.
    void render(const Camera& camera) const;

    /// Returns the (averaged, vertex-grid) `mapp.alti` height in raw byte
    /// units at the grid vertex nearest tile coordinate `(x, y)`, clamped to
    /// the vertex grid's bounds. Multiply by `render::kPixelsPerAltiUnit` for
    /// a world-pixel vertical offset. Used to place decorations/buildings on
    /// the terrain mesh.
    float sample_height(double x, double y) const;

    // Terrain-rendering debug toggles, mirroring the original's `TMapView`
    // `+0x249`/`+0x24b` options (terrain-blending-plan.md Stages
    // C.4/E). Not exposed in UI yet -- code-level only.

    /// "Terrain textures" (`+0x249`, Stage E): when false, terrain is drawn
    /// as flat-shaded (untextured) slope-shaded quads.
    bool terrain_textures_enabled = true;

    /// "Beaches" (`+0x24b`, Stage C.4): when false, skip the shore-overlay
    /// passes at water/land boundaries.
    bool shore_overlays_enabled = false;

    /// "Terrain blending" (`+0x24a`, Stage B.5): when false, skip the
    /// edge-blend (`tran` atlas) passes between same-class,
    /// differently-textured neighboring tiles.
    bool terrain_blending_enabled = true;

    /// Map-edge skirts: vertical quads that drop from the terrain mesh edge
    /// down to sea level, giving mountains a visible cross-section.  Uses
    /// the `terrain.edge` gradient texture extracted from `terr/edge`.
    bool terrain_skirts_enabled = true;

private:
    /// Returns `region_->texture_index_at(tx, ty)`
    int texture_index_at(int tx, int ty) const;

    /// Draws Stage B's up-to-4 edge-blend passes (one per N/E/S/W neighbor
    /// in the same water/land class but with a different texture index) for
    /// tile `(tx, ty)`, reusing `corners`' screen positions/colors (only UVs
    /// differ from the base pass).
    void render_edge_blends(int tx, int ty, const SDL_Vertex corners[4]) const;

    /// Draws Stage C's up-to-2 shore-overlay passes for tile `(tx, ty)`,
    /// reusing `corners`' screen positions/colors (only UVs differ from the
    /// base pass).
    void render_shore_overlays(int tx, int ty, const SDL_Vertex corners[4]) const;

    /// Draws the map-edge skirt (south and east edges of the map diamond).
    void render_skirts(const Camera& camera) const;

    SDL_Renderer* renderer_ = nullptr;
    const world::Region* region_ = nullptr;

    /// Per-texture-page tile textures (Stage A.1/A.3), indexed 1-13 by
    /// `world::Region::texture_index_at`; index 0 is unused. Loaded from
    /// `tables/terrain_textures.json` by `load_textures`.
    std::array<Texture, 14> terrain_page_textures_;

    /// Edge-blend atlas (Stage B, B15 Round 40): a 4x4-cell dithered-dissolve
    /// atlas (`terrain.tran`), one per culture palette. Loaded from
    /// `tables/terrain_textures.json`'s `"tran"` entry by `load_textures`.
    Texture tran_atlas_texture_;

    /// Shore-overlay atlases (Stage C.1): `[0]` = `terrain.coa0`, `[1]` =
    /// `terrain.coa1`, both full 256x256 native-resolution textures.
    std::array<Texture, 2> shore_atlas_textures_;

    /// Map-edge skirt texture (`terrain.edge`): a 256x256 vertical gradient
    /// (dark at top, earthy brown at bottom) used for the cliff-face quads.
    Texture edge_texture_;

    /// Per-vertex terrain mesh data, built at construction. Both are
    /// `(width+1) * (height+1)` grids, row-major, indexed
    /// `[row * (width+1) + col]` -- one vertex per tile-grid corner, shared
    /// between up to 4 adjacent tiles (see render/iso.h and B15).
    std::vector<float> terrain_vertex_height_;     // raw `mapp.alti` byte units, averaged
    std::vector<SDL_Color> terrain_vertex_color_;  // slope-shading tint
};

}  // namespace opente::render
