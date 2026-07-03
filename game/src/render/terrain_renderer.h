#pragma once

// Builds and renders the height-displaced, slope-shaded terrain mesh for a
// single `world::Region`. Texture ownership lives in `TerrainTileset`.
// See OpenTE/spec/rendering.md "Terrain".

#include <SDL.h>
#include <imgui.h>

#include <string>
#include <utility>
#include <vector>

#include "render/camera.h"
#include "render/terrain_tileset.h"
#include "world/region.h"

namespace opente::render {

/// Builds a per-vertex height-displaced, slope-shaded terrain mesh from a
/// `world::Region`'s heightmap (once, at construction -- the heightmap is
/// immutable at runtime) and renders it using a `TerrainTileset`.
///
/// Episodes with multiple regions: construct one `TerrainRenderer` per region
/// (each keeps its own mesh) and render whichever is currently active.
/// Tilesets are owned by the caller and can be shared or swapped independently.
class TerrainRenderer {
public:
    /// Builds the terrain mesh from `region`'s heightmap. `region` must outlive this object.
    TerrainRenderer(SDL_Renderer* renderer, const world::Region& region);

    /// Sets the active tileset. The tileset must outlive this renderer.
    void set_tileset(const TerrainTileset* tileset);

    /// Draws the full terrain:
    /// base texture pass, shore-overlay pass, and blending for every tile in the region.
    void render(const Camera& camera) const;

    /// Renders trail/road/canal/rail preview sprites for a construction-mode path.
    /// Rasterizes each waypoint→waypoint segment and the live last-waypoint→cursor
    /// segment, then draws the appropriate atlas sprite at each affected tile using
    /// the same LUT/UV machinery as the normal network decal pass.
    /// Must be called after `render()` so it composites on top of the terrain.
    /// `path_type` is one of "trai", "road", "cana", "rail".
    void render_preview_path(const std::vector<std::pair<int,int>>& waypoints,
                             int cursor_tx, int cursor_ty,
                             const std::string& path_type,
                             const Camera& camera) const;

    /// Recomputes the per-vertex slope-shading colors (`terrain_vertex_color_`)
    /// from the (immutable) vertex heightmap and the current values of the
    /// `render::kSlopeGradientScale`/`kAmbient*`/`kVertexColorScale` globals.
    /// Called once at construction; the dev GUI calls it again whenever those
    /// lighting values are adjusted at runtime.
    void rebuild_vertex_colors();

    /// Returns the (averaged, vertex-grid) `mapp.alti` height in raw byte
    /// units at the grid vertex nearest tile coordinate `(x, y)`, clamped to
    /// the vertex grid's bounds. Multiply by `render::kPixelsPerAltiUnit` for
    /// a world-pixel vertical offset. Used to place decorations/buildings on
    /// the terrain mesh.
    float sample_height(double x, double y) const;

    // Terrain-rendering debug toggles.

    /// "Beaches": when false, skip the shore-overlay passes at water/land boundaries.
    bool shore_overlays_enabled = true;

    /// "Slope shading": when false, vertex colors are
    /// forced to white (flat lighting) -- the height-displaced mesh is still
    /// drawn, but without the per-vertex directional tint.
    bool slope_shading_enabled = true;

    /// "Terrain blending": when false, skip the
    /// edge-blend passes between same-class,
    /// differently-textured neighboring tiles.
    bool terrain_blending_enabled = true;

    /// Draw the palette-field 4cc code over each tile's center.
    bool terrain_debug_labels_enabled = false;

    /// "Network overlays": when false, skip the trail/road/canal/rail decal pass.
    bool network_overlays_enabled = true;

private:
    /// Returns `region_->texture_index_at(tx, ty)`
    int texture_index_at(int tx, int ty) const;

    /// Draws up-to-4 edge-blend passes (one per N/E/S/W neighbor
    /// in the same water/land class but with a different texture index) for
    /// tile `(tx, ty)`, reusing `corners` screen positions/colors (only UVs
    /// differ from the base pass).
    void render_edge_blends(int tx, int ty, const SDL_Vertex corners[4]) const;

    /// Draws up-to-2 shore-overlay passes for tile `(tx, ty)`,
    /// reusing `corners`' screen positions/colors (only UVs differ from the base pass).
    void render_shore_overlays(int tx, int ty, const SDL_Vertex corners[4]) const;

    /// Draws the trail/road/canal/rail network decal (Stage D) for tile `(tx, ty)`
    /// using the tile's `TileConnectivity` mask and the three runtime-decoded LUTs.
    void render_network_decal(int tx, int ty, const SDL_Vertex corners[4]) const;

    /// Draws the map-edge skirt (south and east edges of the map diamond).
    void render_skirts(const Camera& camera) const;

    /// Core network-decal drawing logic: applies `conn` to the decal LUTs, selects
    /// the atlas cell, and draws the sprite fan onto `corners` (NW/NE/SE/SW order).
    /// `tx`/`ty` locate the tile so canals can test their water neighbors (a canal
    /// endpoint at the shore draws a sea-mouth cell). Called by both
    /// `render_network_decal` (real connectivity from Region) and
    /// `render_preview_path` (synthesized preview).
    void draw_network_conn(const world::TileConnectivity& conn,
                           int tx, int ty,
                           const SDL_Vertex corners[4]) const;

    /// Bitmask over the 8 neighbors of `(tx, ty)`: bit d is CLEARED when
    /// neighbor d is shore-water (the "seas" texture page), so 0xff = no water
    /// adjacent. Shared by the shore-overlay pass and the canal sea-mouth test --
    /// the original engine uses the identical mask + LUT for both.
    std::uint8_t water_neighbor_mask(int tx, int ty) const;

    /// Draws `hidd` (starfield) textured tile fans for all viewport-visible
    /// tiles outside the map bounds.
    /// (same geometry and UV scheme as terrain tiles, at flat sea-level height).
    void render_background(const Camera& camera) const;

    SDL_Renderer* renderer_ = nullptr;
    const world::Region* region_ = nullptr;

    const TerrainTileset* tileset_ = nullptr;

    /// Per-vertex terrain mesh data, built at construction. Both are
    /// `(width+1) * (height+1)` grids, row-major, indexed
    /// `[row * (width+1) + col]` -- one vertex per tile-grid corner, shared
    /// between up to 4 adjacent tiles (see render/iso.h).
    std::vector<float> terrain_vertex_height_;     // raw `mapp.alti` byte units, averaged
    std::vector<SDL_Color> terrain_vertex_color_;  // slope-shading tint
};

}  // namespace opente::render
