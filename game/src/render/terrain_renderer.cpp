#include "render/terrain_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iterator>
#include <utility>

#include <nlohmann/json.hpp>

#include "render/iso.h"

namespace opente::render {

namespace {

/// `tables/terrain_textures.json`'s path, relative to `game_data/` 
/// (written by `tools/extractor/sprites/terrain.py`)
constexpr const char* kTerrainTexturesTablePath = "tables/terrain_textures.json";

/// Shore-overlay atlas sprite ids
constexpr const char* kShoreAtlasSpriteIds[2] = {"terrain.coa0", "terrain.coa1"};

/// Map-edge skirt texture sprite id
constexpr const char* kEdgeSpriteId = "terrain.edge";

/// Extra alti units the skirt extends below sea level, so even flat water
/// edges get a visible cliff face.
constexpr float kSkirtExtension = 16.0f;

/// Skirt vertex color attenuation: cliff faces are less directly lit than
/// the terrain surface. The texture gradient provides most of the shading;
/// these factors add a slight additional dim so the skirt doesn't pop
/// against the terrain edge it's attached to.
constexpr float kSkirtTopShade = 0.80f;
constexpr float kSkirtBottomShade = 0.55f;

/// Skirt UV tiling: the edge texture repeats every `kSkirtUPeriod` edge
/// segments along the edge (U axis), matching the terrain base pass's
/// 8-tile U period. V uses the same world-pixel scale for square texels.
constexpr float kSkirtUPeriod = 8.0f;

/// Two-triangle quad winding shared by every terrain pass (base, edge-blend, shore overlay): 
///                             NW-NE-SE, NW-SE-SW.
constexpr int kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

/// 8-direction neighbor offsets, `world-and-maps.md` order:
/// 0=NW 1=N 2=NE 3=E 4=SE 5=S 6=SW 7=W.
constexpr int kDirDx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
constexpr int kDirDy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

/// Stage B: the `tran` edge-blend atlas is a 4x4-cell grid of 64x64px
/// dithered-dissolve cells in a 256x256 atlas, cell selection addressed with
/// a half-texel inset on each edge (B15 Round 40: `u_near = (cell*64 +
/// 0.5)/256`, `u_far = u_near + 63/256`).  The EXE samples the full cell;
/// a previous "half-cell" tweak was caused by confounding UV-interpolation
/// artifacts from the missing center vertex (see kTranFanIndices).
constexpr float kTranCellUV = 64.0f / 256.0f;
constexpr float kTranInsetUV = 0.5f / 256.0f;

/// Stage B: 5-vertex triangle fan emulating D3DPT_TRIANGLEFAN.  Vertex 0 is
/// the tile center (with midpoint UVs), vertices 1-4 are the NW/NE/SE/SW
/// corners.  The EXE's 6-vertex fan (center, 4 corners, closing repeat) is
/// equivalent to these 4 triangles:
///   (center, NW, NE), (center, NE, SE), (center, SE, SW), (center, SW, NW).
constexpr int kTranFanIndices[12] = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};

/// Stage B: the 4 edge-blend directions, in `world-and-maps.md`'s 8-direction
/// order (1=N, 3=E, 5=S, 7=W -- the cardinal directions of `0x42acf0`'s
/// `for (dir = 1; dir < 8; dir += 2)` loop), and the per-direction cell-corner
/// rotation index `k` used by `m = (j - k) mod 4` (tile corner `j`,
/// NW=0,NE=1,SE=2,SW=3, gets `tran`-cell corner `corner_u/corner_v[m]`, where
/// `corner_u/v[m]` for m=0..3 is `(u1,v1),(u0,v1),(u0,v0),(u1,v0)` -- see
/// `render_edge_blends`).
///
/// The raw disassembly of `0x42acf0` (`exe_blend_draw_dump.txt`, offsets
/// 0x42ae0b-0x42aec3) gives `k_raw = ((dir+1)&7)>>1` = `{1,2,3,0}` for
/// dirs {1,3,5,7} (N,E,S,W) -- this assumes the EXE's 4-corner vertex slots
/// 1-4 map onto screen corners NW,NE,SE,SW in that same order (`j` =
/// vertex-slot index directly). 2026-06-13 visual checks found `k_raw`
/// rotated 90 degrees from correct, and the first correction tried
/// (`k_raw - 1 = {0,1,2,3}`) was off by 90 degrees the *other* way.
/// `kEdgeBlendK` below (`k_raw + 1 mod 4 = {2,3,0,1}`) is the value that
/// renders correctly -- i.e. vertex slot `v` corresponds to screen corner
/// `j=(v-1) mod 4`, which folds into `m=(j-k)mod4` as `k_eff = k_raw + 1`.
constexpr int kEdgeBlendDirs[4] = {1, 3, 5, 7};
constexpr int kEdgeBlendK[4] = {2, 3, 0, 1};

/// Stage C.2: 8-bit water-neighbor mask (bit `d` set iff direction-`d`
/// neighbor is water-class, `texture_index <= 2`) -> overlay1 cell code.
/// `255` = no overlay. `0-52` index `terrain.coa0`'s cells via
/// `kShoreUvIndex`; `53-59` index `terrain.coa1`'s cells `0-6`
/// (`exe_b15_round37_shore_tables.txt`, LUT0).
constexpr Uint8 kShoreLUT0[256] = {
    15, 15, 15, 49, 15, 15, 50, 36, 15, 15, 15, 28, 51, 51, 16, 28,
    15, 15, 15, 49, 15, 15, 50, 36, 52, 52, 33, 24, 37, 37, 33, 24,
    15, 15, 15, 49, 15, 15, 50, 15, 17, 17, 22, 45, 29, 29, 22, 45,
    54, 54, 54, 15, 54, 54, 15, 15, 17, 17, 22, 45, 29, 29, 22, 45,
    15, 15, 15, 49, 15, 15, 50, 36, 15, 15, 16, 28, 51, 51, 16, 28,
    15, 15, 15, 49, 15, 15, 50, 36, 52, 52, 33, 24, 37, 37, 33, 24,
    53, 53, 53, 15, 54, 54, 15, 15, 34, 34, 57, 41, 25, 25, 57, 41,
    38, 38, 38, 15, 38, 38, 15, 15, 34, 34, 57, 41, 25, 25, 57, 41,
    15, 48, 19, 19, 15, 48, 32, 32, 15, 48, 21, 21, 51, 15, 21, 21,
    15, 48, 21, 21, 15, 48, 32, 32, 52, 15, 56, 56, 37, 15, 56, 56,
    18, 35, 20, 20, 18, 35, 59, 59, 23, 58, 255, 255, 46, 42, 255, 255,
    30, 26, 20, 47, 30, 27, 43, 43, 23, 58, 255, 255, 46, 42, 255, 255,
    55, 39, 31, 31, 55, 39, 27, 27, 55, 39, 44, 44, 15, 15, 44, 44,
    55, 39, 31, 31, 55, 39, 27, 27, 15, 15, 40, 40, 15, 15, 40, 40,
    18, 35, 20, 20, 18, 35, 59, 59, 23, 58, 255, 255, 46, 42, 255, 255,
    30, 26, 47, 47, 30, 26, 43, 43, 23, 58, 255, 255, 46, 42, 255, 255,
};

/// Stage C.3: corner water-flags (sum of `1 << corner_dir` for
/// `corner_dir` in {0=NW,2=NE,4=SE,6=SW} where that corner neighbor is
/// water-class) -> `dl` index, keyed by `flags - 1` (`flags == 0` means no
/// corner is water, so overlay2 is skipped before indexing). `15` = no
/// overlay2 (LUT85).
constexpr Uint8 kShoreLUT85[85] = {
    0,  15, 15, 1,  2,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 3,  4,
    15, 15, 5,  6,  15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 7,  8,  15, 15, 9,
    10, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 12, 15, 15, 13, 14,
};

/// Stage C.3: `dl` (0-14) -> `shape` (0-14), indexing the first 15 cells of
/// `kShoreUvIndex`.
constexpr int kShoreDlToShape[15] = {1, 2, 3, 0, 5, 4, 6, 9, 8, 7, 10, 11, 12, 13, 14};

/// Stage C.3: 53-cell UV-index array (`exe_b15_round37_shore_tables.txt`,
/// "UV-index array @ 0x5f8940"). Each `(idx0, idx1)` is an index into the
/// float-constant array at `0x5f88f0` (`shore_uv` below); cells 0-14
/// are the `shape` cells used by overlay2, the full 53 are used by
/// overlay1's `kShoreLUT0` codes 0-52.
constexpr std::pair<int, int> kShoreUvIndex[53] = {
    {2, 1},   {6, 1},   {10, 1},  {14, 1},  {4, 2},   {8, 2},   {12, 2},
    {2, 3},   {6, 3},   {10, 3},  {14, 3},  {4, 4},   {8, 4},   {12, 4},
    {2, 5},   {6, 5},   {10, 5},  {14, 5},  {4, 6},   {8, 6},   {12, 6},
    {2, 7},   {6, 7},   {10, 7},  {14, 7},  {4, 8},   {8, 8},   {12, 8},
    {2, 9},   {6, 9},   {10, 9},  {14, 9},  {4, 10},  {8, 10},  {12, 10},
    {2, 11},  {6, 11},  {10, 11}, {14, 11}, {4, 12},  {8, 12},  {12, 12},
    {2, 13},  {6, 13},  {10, 13}, {14, 13}, {4, 14},  {8, 14},  {12, 14},
    {2, 15},  {6, 15},  {10, 15}, {14, 15},
};

/// Shore-atlas UV float array (`0x5f88f0`): `float[k] = (k - 2) / 16` for
/// `k >= 2`, `~0` for `k < 2`.  Each cell in the atlas is a 64x32-pixel
/// isometric DIAMOND (U span = 4/16 = 0.25, V span = 2/16 = 0.125), laid out
/// in a staggered brick grid.  For a cell with `kShoreUvIndex` = `(idx0, idx1)`:
///   LEFT_U  = float_arr[idx0]   = (idx0 - 2) / 16
///   MID_U   = float_arr[idx0+2] = idx0 / 16
///   RIGHT_U = float_arr[idx0+4] = (idx0 + 2) / 16
///   TOP_V   = float_arr[idx1+1] = (idx1 - 1) / 16
///   MID_V   = float_arr[idx1+2] = idx1 / 16
///   BOT_V   = float_arr[idx1+3] = (idx1 + 1) / 16
/// The 4-corner quad maps: NW=(MID_U,TOP_V), NE=(RIGHT_U,MID_V),
///                          SE=(MID_U,BOT_V), SW=(LEFT_U,MID_V).
constexpr float shore_uv(int raw_idx) {
    return raw_idx <= 2 ? 0.0f : static_cast<float>(raw_idx - 2) / 16.0f;
}

}  // namespace

TerrainRenderer::TerrainRenderer(SDL_Renderer* renderer, const world::Region& region)
    : renderer_(renderer), region_(&region) {
    const int width = region_->width();   // num tiles along width
    const int height = region_->height(); // num tiles along height
    const int vw = width + 1;  // num verts along width
    const int vh = height + 1; // num verts along height

    // A tile's height for mesh purposes:
    // Water tiles are pinned to the region's sea level, so water renders as a flat plane.
    // Out-of-bound coordinates use the nearest map edge.
    auto tile_height = [&](int tx, int ty) -> float {
        tx = std::clamp(tx, 0, width - 1);
        ty = std::clamp(ty, 0, height - 1);
        const world::TerrainType terrain = region_->terrain_at(tx, ty);
        if (terrain == world::TerrainType::DeepWater || terrain == world::TerrainType::ShallowWater) {
            return static_cast<float>(region_->sea_level());
        }
        return static_cast<float>(region_->height_at(tx, ty));
    };

    // Each grid vertex (col, row) is shared by up to 4 tiles
    // ((col-1,row-1)..(col,row)); averaging their heights gives every shared
    // vertex a single height (continuous mesh, no per-tile seams) and smooths
    // out `alti`'s ~1-unit tile-to-tile noise.
    terrain_vertex_height_.assign(static_cast<std::size_t>(vw * vh), 0.0f);
    for (int row = 0; row < vh; ++row) {
        for (int col = 0; col < vw; ++col) {
            float sum = 0.0f;
            for (int dy = -1; dy <= 0; ++dy) {
                for (int dx = -1; dx <= 0; ++dx) {
                    sum += tile_height(col + dx, row + dy);
                }
            }
            terrain_vertex_height_[static_cast<std::size_t>(row * vw + col)] = sum / 4.0f;
        }
    }

    // Per-vertex slope shading: TEMPORARY approximatation of the EXE's per-vertex diffuse lighting
    auto vertex_height = [&](int col, int row) -> float {
        col = std::clamp(col, 0, vw - 1);
        row = std::clamp(row, 0, vh - 1);
        return terrain_vertex_height_[static_cast<std::size_t>(row) * vw + col];
    };

    terrain_vertex_color_.assign(static_cast<std::size_t>(vw) * vh, SDL_Color{255, 255, 255, 255});
    for (int row = 0; row < vh; ++row) {
        for (int col = 0; col < vw; ++col) {
            const float dx = (vertex_height(col + 1, row) - vertex_height(col - 1, row)) * kAltiToWorldHeight;
            const float dy = (vertex_height(col, row + 1) - vertex_height(col, row - 1)) * kAltiToWorldHeight;
            const Vec3 normal{-dx, -dy, kSlopeNormalZ};
            const float len = std::sqrt(normal.x * normal.x + normal.y * normal.y + normal.z * normal.z);
            const float diffuse =
                std::max(0.0f, (normal.x * kLightDir.x + normal.y * kLightDir.y + normal.z * kLightDir.z) / len);
            const float brightness = kAmbient + (1.0f - kAmbient) * diffuse;
            const auto c = static_cast<Uint8>(std::clamp(brightness, 0.0f, 1.0f) * 255.0f);
            terrain_vertex_color_[static_cast<std::size_t>(row) * vw + col] = SDL_Color{c, c, c, 255};
        }
    }
}

void TerrainRenderer::load_textures(const std::filesystem::path& game_data_dir,
                                     const data::DataRegistry& registry) {
    const std::filesystem::path table_path = game_data_dir / kTerrainTexturesTablePath;
    std::ifstream file(table_path);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "No '%s' found -- terrain will render untextured.",
                     table_path.string().c_str());
        return;
    }

    nlohmann::json j;
    file >> j;
    const std::vector<std::string> pages = j.value("pages", std::vector<std::string>{});

    auto find_sprite_file = [&](const std::string& sprite_id) -> std::optional<std::filesystem::path> {
        for (const data::SpriteEntry& sprite : registry.manifest().sprites) {
            if (sprite.id == sprite_id) {
                return game_data_dir / sprite.file;
            }
        }
        return std::nullopt;
    };

    for (std::size_t i = 0; i < pages.size() && i < terrain_page_textures_.size() - 1; ++i) {
        if (pages[i].empty()) {
            continue;
        }
        if (const std::optional<std::filesystem::path> path = find_sprite_file(pages[i])) {
            terrain_page_textures_[i + 1] = Texture::load(renderer_, *path);
        }
    }

    for (std::size_t i = 0; i < shore_atlas_textures_.size(); ++i) {
        if (const std::optional<std::filesystem::path> path = find_sprite_file(kShoreAtlasSpriteIds[i])) {
            shore_atlas_textures_[i] = Texture::load(renderer_, *path);
            if (shore_atlas_textures_[i].valid()) {
                SDL_SetTextureBlendMode(shore_atlas_textures_[i].handle(), SDL_BLENDMODE_BLEND);
            }
        }
    }

    const std::string tran_sprite_id = j.value("tran", std::string{});
    if (!tran_sprite_id.empty()) {
        if (const std::optional<std::filesystem::path> path = find_sprite_file(tran_sprite_id)) {
            tran_atlas_texture_ = Texture::load(renderer_, *path);
            if (tran_atlas_texture_.valid()) {
                SDL_SetTextureBlendMode(tran_atlas_texture_.handle(), SDL_BLENDMODE_BLEND);
            }
        }
    }

    if (const std::optional<std::filesystem::path> path = find_sprite_file(kEdgeSpriteId)) {
        edge_texture_ = Texture::load(renderer_, *path);
        if (edge_texture_.valid()) {
            SDL_SetTextureBlendMode(edge_texture_.handle(), SDL_BLENDMODE_BLEND);
        }
    }
}

int TerrainRenderer::texture_index_at(int tx, int ty) const {
    tx = std::clamp(tx, 0, region_->width() - 1);
    ty = std::clamp(ty, 0, region_->height() - 1);
    return region_->texture_index_at(tx, ty);
}

float TerrainRenderer::sample_height(double x, double y) const {
    const int vw = region_->width() + 1;
    const int vh = region_->height() + 1;
    const int col = std::clamp(static_cast<int>(std::lround(x)), 0, vw - 1);
    const int row = std::clamp(static_cast<int>(std::lround(y)), 0, vh - 1);
    return terrain_vertex_height_[static_cast<std::size_t>(row) * vw + col];
}

void TerrainRenderer::render(const Camera& camera) const {
    const int width = region_->width();
    const int height = region_->height();
    const int vw = width + 1;

    // Render the terrain as a per-vertex height-displaced, slope-shaded mesh
    // (B15 Rounds 31-37): each tile is a quad whose 4 corners are shared
    // vertices from `terrain_vertex_height_`/`terrain_vertex_color_`, so
    // adjacent tiles' edges always coincide (smooth slopes -- steep height
    // differences between interior tiles just become steep quads; the
    // map-edge skirt is a separate pass rendered before this loop).
    //
    // UV mapping: the original's continuous `uv = (col/8, row/16)` scheme
    // (B15 Round 34, terrain-blending-plan.md Stage A.0) -- each texture page
    // is a 256x256 image that tiles seamlessly across an 8x16-tile region of
    // the map (32x16px per tile, 2x-magnified onto the 64x32 screen diamond).
    // UV origin is `fmod(tx,8)/8` / `fmod(ty,16)/16` per *tile*, with each
    // corner adding a fixed 1/8 or 1/16 step (see below) -- this avoids
    // SDL2's lack of a wrap-mode API (the source textures are seamlessly
    // tileable, so a [0,1] UV span per tile is equivalent to true wrapping).
    // Per-vertex `fmod`'d UVs would wrap to 0 on the "+1" corners at every
    // period boundary, stretching the whole texture across that tile.
    //
    // Corner order is fixed as NW, NE, SE, SW for every pass below (base,
    // Stage C shore overlays).
    static constexpr int kCornerCol[4] = {0, 1, 1, 0};
    static constexpr int kCornerRow[4] = {0, 0, 1, 1};
    static constexpr float kUPeriod = 8.0f;
    static constexpr float kVPeriod = 16.0f;

    if (terrain_skirts_enabled) {
        render_skirts(camera);
    }

    for (int ty = 0; ty < height; ++ty) {
        for (int tx = 0; tx < width; ++tx) {
            const int own_index = texture_index_at(tx, ty);

            // UV origin for this *tile* (not per-vertex): each corner adds a
            // fixed 1/period step from here, so a single quad always spans
            // exactly one texture cell. Computing fmod(col,period) per vertex
            // instead would make the "+1" corners wrap to 0 at every period
            // boundary (col/row a multiple of the period), stretching the
            // whole texture across that tile.
            const float u0 = std::fmod(static_cast<float>(tx), kUPeriod) / kUPeriod;
            const float v0 = std::fmod(static_cast<float>(ty), kVPeriod) / kVPeriod;

            SDL_Vertex verts[4];
            for (int c = 0; c < 4; ++c) {
                const int col = tx + kCornerCol[c];
                const int row = ty + kCornerRow[c];
                const std::size_t vidx = static_cast<std::size_t>(row) * vw + col;
                const float height_offset = terrain_vertex_height_[vidx] * kPixelsPerAltiUnit;

                Vec2 world_pos = tile_to_world(static_cast<float>(col), static_cast<float>(row));
                world_pos.y -= height_offset;
                const Vec2 screen_pos = camera.world_to_screen(world_pos);

                verts[c].position = {screen_pos.x, screen_pos.y};
                verts[c].color = terrain_vertex_color_[vidx];
                verts[c].tex_coord = {u0 + static_cast<float>(kCornerCol[c]) / kUPeriod,
                                       v0 + static_cast<float>(kCornerRow[c]) / kVPeriod};
            }

            // Base pass (Stage A.5 / E): the tile's own texture page, or flat
            // slope-shaded color if textures are disabled.
            if (terrain_textures_enabled && terrain_page_textures_[own_index].valid()) {
                SDL_RenderGeometry(renderer_, terrain_page_textures_[own_index].handle(), verts, 4, kQuadIndices, 6);
            } else {
                SDL_RenderGeometry(renderer_, nullptr, verts, 4, kQuadIndices, 6);
            }

            // Stage B: edge-blend passes between same-class,
            // differently-textured neighboring tiles.
            if (terrain_blending_enabled && terrain_textures_enabled) {
                render_edge_blends(tx, ty, verts);
            }

            // Stage C: shore-overlay passes at water/land boundaries.
            if (shore_overlays_enabled && terrain_textures_enabled) {
                render_shore_overlays(tx, ty, verts);
            }
        }
    }
}

void TerrainRenderer::render_edge_blends(int tx, int ty, const SDL_Vertex corners[4]) const {
    if (!tran_atlas_texture_.valid()) {
        return;
    }

    const int own_index = texture_index_at(tx, ty);
    const bool own_is_water = own_index <= 2;

    // B.2: for each of the 4 cardinal directions, queue an edge-blend pass
    // if the neighbor is in the same water/land class but uses a different
    // texture page. Map-edge tiles (no neighbor) are skipped.
    for (int i = 0; i < 4; ++i) {
        const int dir = kEdgeBlendDirs[i];
        const int nx = tx + kDirDx[dir];
        const int ny = ty + kDirDy[dir];
        if (nx < 0 || nx >= region_->width() || ny < 0 || ny >= region_->height()) {
            continue;
        }

        const int nbr_index = texture_index_at(nx, ny);
        if (nbr_index == own_index || (nbr_index <= 2) != own_is_water) {
            continue;
        }

        // B.3: UV rect for the `tran` atlas cell, full 64px cell with
        // half-texel inset (EXE: u_far = u_near + 63/256).
        const int cell_col = nbr_index & 3;
        const int cell_row = nbr_index >> 2;
        const float u0 = static_cast<float>(cell_col) * kTranCellUV + kTranInsetUV;
        const float v0 = static_cast<float>(cell_row) * kTranCellUV + kTranInsetUV;
        const float u1 = u0 + kTranCellUV - 2.0f * kTranInsetUV;
        const float v1 = v0 + kTranCellUV - 2.0f * kTranInsetUV;
        const float u_mid = (u0 + u1) * 0.5f;
        const float v_mid = (v0 + v1) * 0.5f;
        // corner_u/v[m] for m=0..3: (u1,v1), (u0,v1), (u0,v0), (u1,v0)
        const float corner_u[4] = {u1, u0, u0, u1};
        const float corner_v[4] = {v1, v1, v0, v0};

        const int k = kEdgeBlendK[i];

        // 5-vertex triangle fan: V0=center, V1..V4=NW/NE/SE/SW corners.
        // Matches the EXE's D3DPT_TRIANGLEFAN(6 verts) geometry exactly.
        SDL_Vertex fan[5];
        fan[0].position = {
            (corners[0].position.x + corners[1].position.x +
             corners[2].position.x + corners[3].position.x) * 0.25f,
            (corners[0].position.y + corners[1].position.y +
             corners[2].position.y + corners[3].position.y) * 0.25f};
        fan[0].color = corners[0].color;
        fan[0].tex_coord = {u_mid, v_mid};
        for (int j = 0; j < 4; ++j) {
            const int m = ((j - k) % 4 + 4) % 4;
            fan[1 + j].position = corners[j].position;
            fan[1 + j].color = corners[j].color;
            fan[1 + j].tex_coord = {corner_u[m], corner_v[m]};
        }
        SDL_RenderGeometry(renderer_, tran_atlas_texture_.handle(),
                           fan, 5, kTranFanIndices, 12);
    }
}

void TerrainRenderer::render_shore_overlays(int tx, int ty, const SDL_Vertex corners[4]) const {
    // Each shore-atlas cell is a 64x32 px diamond in a staggered grid.
    // The UV mapping places diamond vertices at the 4 quad corners:
    //   corners[0] NW (screen top)   -> (MID_U,  TOP_V)
    //   corners[1] NE (screen right) -> (RIGHT_U, MID_V)
    //   corners[2] SE (screen bottom)-> (MID_U,  BOT_V)
    //   corners[3] SW (screen left)  -> (LEFT_U,  MID_V)
    auto draw_overlay = [&](const Texture& atlas, int cell) {
        if (!atlas.valid() || cell < 0 || cell >= static_cast<int>(std::size(kShoreUvIndex))) {
            return;
        }
        const auto [idx0, idx1] = kShoreUvIndex[cell];
        const float left_u  = shore_uv(idx0);
        const float mid_u   = shore_uv(idx0 + 2);
        const float right_u = shore_uv(idx0 + 4);
        const float top_v   = shore_uv(idx1 + 1);
        const float mid_v   = shore_uv(idx1 + 2);
        const float bot_v   = shore_uv(idx1 + 3);

        SDL_Vertex verts[4];
        std::copy(corners, corners + 4, verts);
        verts[0].tex_coord = {mid_u,   top_v};   // NW = screen top
        verts[1].tex_coord = {right_u, mid_v};    // NE = screen right
        verts[2].tex_coord = {mid_u,   bot_v};    // SE = screen bottom
        verts[3].tex_coord = {left_u,  mid_v};    // SW = screen left
        SDL_RenderGeometry(renderer_, atlas.handle(), verts, 4, kQuadIndices, 6);
    };

    // Build the 8-direction water-neighbor mask (shared by both passes).
    int mask = 0;
    for (int dir = 0; dir < 8; ++dir) {
        if (texture_index_at(tx + kDirDx[dir], ty + kDirDy[dir]) <= 2) {
            mask |= (1 << dir);
        }
    }
    if (mask == 0xFF) {
        return;
    }

    // Overlay pass 1 (C.2): 8-direction mask -> LUT0 -> cell code.
    const Uint8 overlay1 = kShoreLUT0[mask];
    if (overlay1 != 255) {
        if (overlay1 <= 52) {
            draw_overlay(shore_atlas_textures_[0], overlay1);
        } else {
            draw_overlay(shore_atlas_textures_[1], overlay1 - 53);
        }
    }

    // Overlay pass 2 (C.3): concave-corner flags derived from the same mask.
    // A corner flag fires when BOTH adjacent cardinal neighbors are water AND
    // the diagonal between them is NOT water — the "inner coastline corner"
    // that the edge-based overlay1 doesn't cover.
    //   NW corner: (mask & 0x83) == 0x82  (W+N water, NW not)
    //   NE corner: (mask & 0x0E) == 0x0A  (N+E water, NE not)
    //   SE corner: (mask & 0x38) == 0x28  (E+S water, SE not)
    //   SW corner: (mask & 0xE0) == 0xA0  (S+W water, SW not)
    int flags = 0;
    if ((mask & 0x83) == 0x82) flags |= 0x01;
    if ((mask & 0x0E) == 0x0A) flags |= 0x04;
    if ((mask & 0x38) == 0x28) flags |= 0x10;
    if ((mask & 0xE0) == 0xA0) flags |= 0x40;
    if (flags != 0) {
        const Uint8 dl = kShoreLUT85[flags - 1];
        if (dl != 15) {
            draw_overlay(shore_atlas_textures_[0], kShoreDlToShape[dl]);
        }
    }
}

void TerrainRenderer::render_skirts(const Camera& camera) const {
    const int width = region_->width();
    const int height = region_->height();
    const int vw = width + 1;
    const float sea_height = static_cast<float>(region_->sea_level());
    const float bottom_height = std::max(0.0f, sea_height - kSkirtExtension);

    SDL_Texture* tex = (terrain_textures_enabled && edge_texture_.valid())
                           ? edge_texture_.handle()
                           : nullptr;

    // World-pixel diagonal of one isometric edge segment (same for south
    // and east edges): sqrt((W/2)^2 + (H/2)^2).
    const float seg_len = std::sqrt(kTileWidth * kTileWidth / 4.0f +
                                    kTileHeight * kTileHeight / 4.0f);

    // V scale: alti units -> UV, matched to U's world-pixel scale so the
    // texture maps with square texels (no distortion).
    const float v_per_alti = kPixelsPerAltiUnit / (kSkirtUPeriod * seg_len);

    auto dim_color = [](SDL_Color c, float f) -> SDL_Color {
        return {static_cast<Uint8>(c.r * f), static_cast<Uint8>(c.g * f),
                static_cast<Uint8>(c.b * f), c.a};
    };

    // Emit one skirt quad between two adjacent edge vertices.
    // `idx` is the edge-parallel index (col for south, row for east).
    auto emit_quad = [&](int col0, int row0, int col1, int row1, int idx) {
        const auto vidx0 = static_cast<std::size_t>(row0) * vw + col0;
        const auto vidx1 = static_cast<std::size_t>(row1) * vw + col1;

        const float h0 = terrain_vertex_height_[vidx0];
        const float h1 = terrain_vertex_height_[vidx1];

        if (h0 <= bottom_height && h1 <= bottom_height) {
            return;
        }

        // Top vertices: terrain surface
        Vec2 wp0 = tile_to_world(static_cast<float>(col0), static_cast<float>(row0));
        wp0.y -= h0 * kPixelsPerAltiUnit;

        Vec2 wp1 = tile_to_world(static_cast<float>(col1), static_cast<float>(row1));
        wp1.y -= h1 * kPixelsPerAltiUnit;

        // Bottom vertices: below sea level by kSkirtExtension
        Vec2 bwp0 = tile_to_world(static_cast<float>(col0), static_cast<float>(row0));
        bwp0.y -= bottom_height * kPixelsPerAltiUnit;

        Vec2 bwp1 = tile_to_world(static_cast<float>(col1), static_cast<float>(row1));
        bwp1.y -= bottom_height * kPixelsPerAltiUnit;

        const Vec2 sp0 = camera.world_to_screen(wp0);
        const Vec2 sp1 = camera.world_to_screen(wp1);
        const Vec2 bsp0 = camera.world_to_screen(bwp0);
        const Vec2 bsp1 = camera.world_to_screen(bwp1);

        const SDL_Color tc0 = dim_color(terrain_vertex_color_[vidx0], kSkirtTopShade);
        const SDL_Color tc1 = dim_color(terrain_vertex_color_[vidx1], kSkirtTopShade);
        const SDL_Color bc0 = dim_color(terrain_vertex_color_[vidx0], kSkirtBottomShade);
        const SDL_Color bc1 = dim_color(terrain_vertex_color_[vidx1], kSkirtBottomShade);

        // U: fmod-based tiling along the edge, same period as the terrain
        // base pass, so each segment advances 1/kSkirtUPeriod in U space.
        const float u0 = std::fmod(static_cast<float>(idx), kSkirtUPeriod) / kSkirtUPeriod;
        const float u1 = u0 + 1.0f / kSkirtUPeriod;

        // V: proportional to height at the same world-pixel scale as U
        // (square texels). V=0 (top of texture, dark) at skirt bottom,
        // V>0 (toward bottom of texture, brown) at terrain surface.
        // Clamped to [0,1] to avoid wrap-seam artifacts on very tall skirts.
        const float v_top0 = std::min(1.0f, (h0 - bottom_height) * v_per_alti);
        const float v_top1 = std::min(1.0f, (h1 - bottom_height) * v_per_alti);

        SDL_Vertex verts[4] = {
            {{sp0.x, sp0.y}, tc0, {u0, v_top0}},
            {{sp1.x, sp1.y}, tc1, {u1, v_top1}},
            {{bsp1.x, bsp1.y}, bc1, {u1, 0.0f}},
            {{bsp0.x, bsp0.y}, bc0, {u0, 0.0f}},
        };
        SDL_RenderGeometry(renderer_, tex, verts, 4, kQuadIndices, 6);
    };

    // South edge: row = height, col varies. Faces toward the camera in
    // isometric view (the bottom-left diagonal of the map diamond).
    for (int col = 0; col < width; ++col) {
        emit_quad(col, height, col + 1, height, col);
    }

    // East edge: col = width, row varies. Faces toward the camera
    // (the bottom-right diagonal of the map diamond).
    for (int row = 0; row < height; ++row) {
        emit_quad(width, row, width, row + 1, row);
    }
}

}  // namespace opente::render
