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

/// Two-triangle quad winding shared by every terrain pass (base, edge-blend, shore overlay): 
///                             NW-NE-SE, NW-SE-SW.
constexpr int kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

/// 8-direction neighbor offsets, `world-and-maps.md` order:
/// 0=NW 1=N 2=NE 3=E 4=SE 5=S 6=SW 7=W.
constexpr int kDirDx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
constexpr int kDirDy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

/// Stage B: the `tran` edge-blend atlas is a 4x4-cell grid of 64x64px
/// dithered-dissolve cells in a 256x256 atlas, cell selection addressed with
/// a half-texel inset on the near edge (B15 Round 40: `u = (cell*64 +
/// 0.5)/256`).
constexpr float kTranCellUV = 64.0f / 256.0f;
constexpr float kTranInsetUV = 0.5f / 256.0f;

/// 2026-06-13 visual check: sampling the *full* 64px cell (as the raw EXE
/// formula does, `u1 = u0 + 63/256`) onto the same screen quad as the base
/// terrain pass made the dissolve dots look noticeably smaller/denser than
/// the base texture's features. The base pass samples a 32x16px region of
/// its 256x256 texture per tile, magnified 2x onto the 64x32 screen diamond
/// (terrain-blending-plan.md Stage A.0); to match that apparent scale, only
/// sample *half* of each `tran` cell (32x32px) per tile -- i.e. halve the
/// sampled UV span on both axes (still anchored at the cell's near corner
/// `(u0,v0)`, just covering less of the cell).
constexpr float kTranSampleUV = 32.0f / 256.0f;

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
/// float-constant array at `0x5f88f0` (`shore_uv_origin` below); cells 0-14
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

/// `0x5f88f0`'s float-constant array, as used by the UV-index lookup:
/// `float[k] = (k - 2) / 16` for `k >= 2`, and `~0` for `k < 2` (the dump's
/// `[0]`/`[1]` are denormal near-zero values). Converts a `kShoreUvIndex`
/// component to a UV-space origin in `[0, 1]` (a fraction of the 256x256
/// shore atlas).
constexpr float shore_uv_origin(int idx) {
    return idx <= 2 ? 0.0f : static_cast<float>(idx - 2) / 16.0f;
}

/// Stage C.3: UV-space size of one shore-atlas cell -- the spacing between
/// adjacent `kShoreUvIndex` entries (e.g. cells 0/1: idx0 2 -> 6, i.e.
/// `shore_uv_origin(2)`=0 -> `shore_uv_origin(6)`=0.25, over 2 grid steps of
/// `1/16` each = `2/16`).
constexpr float kShoreCellSize = 2.0f / 16.0f;

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
        }
    }

    const std::string tran_sprite_id = j.value("tran", std::string{});
    if (!tran_sprite_id.empty()) {
        if (const std::optional<std::filesystem::path> path = find_sprite_file(tran_sprite_id)) {
            tran_atlas_texture_ = Texture::load(renderer_, *path);
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
    // adjacent tiles' edges always coincide (smooth slopes, no skirt pass
    // needed -- steep height differences just become steep quads).
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

        // B.3: UV rect for the `tran` atlas cell selected by the neighbor's
        // texture-page index (`col = nbr & 3`, `row = nbr >> 2`). Per-corner
        // assignment per `kEdgeBlendK`'s doc comment above.
        const int cell_col = nbr_index & 3;
        const int cell_row = nbr_index >> 2;
        const float u0 = static_cast<float>(cell_col) * kTranCellUV + kTranInsetUV;
        const float v0 = static_cast<float>(cell_row) * kTranCellUV + kTranInsetUV;
        const float u1 = u0 + kTranSampleUV - 2.0f * kTranInsetUV;
        const float v1 = v0 + kTranSampleUV - 2.0f * kTranInsetUV;
        // corner_u/v[m] for m=0..3: (u1,v1), (u0,v1), (u0,v0), (u1,v0)
        const float corner_u[4] = {u1, u0, u0, u1};
        const float corner_v[4] = {v1, v1, v0, v0};

        const int k = kEdgeBlendK[i];
        SDL_Vertex verts[4];
        std::copy(corners, corners + 4, verts);
        for (int j = 0; j < 4; ++j) {
            const int m = ((j - k) % 4 + 4) % 4;
            verts[j].tex_coord = {corner_u[m], corner_v[m]};
        }
        SDL_RenderGeometry(renderer_, tran_atlas_texture_.handle(), verts, 4, kQuadIndices, 6);
    }
}

void TerrainRenderer::render_shore_overlays(int tx, int ty, const SDL_Vertex corners[4]) const {
    // UV rect for shore-atlas cell `cell` (`kShoreUvIndex`): the cell is a
    // `kShoreCellSize`-square region of the atlas, mapped onto the quad's
    // diamond corners in the same NW/NE/SE/SW order as `corners` (Stage C.3
    // open item: this square-to-diamond mapping is a documented
    // approximation pending visual validation against the original).
    auto draw_overlay = [&](const Texture& atlas, int cell) {
        if (!atlas.valid() || cell < 0 || cell >= static_cast<int>(std::size(kShoreUvIndex))) {
            return;
        }
        const auto [idx0, idx1] = kShoreUvIndex[cell];
        const float u0 = shore_uv_origin(idx0);
        const float v0 = shore_uv_origin(idx1);
        const float u1 = u0 + kShoreCellSize;
        const float v1 = v0 + kShoreCellSize;

        SDL_Vertex verts[4];
        std::copy(corners, corners + 4, verts);
        verts[0].tex_coord = {u0, v0};  // NW
        verts[1].tex_coord = {u1, v0};  // NE
        verts[2].tex_coord = {u1, v1};  // SE
        verts[3].tex_coord = {u0, v1};  // SW
        SDL_RenderGeometry(renderer_, atlas.handle(), verts, 4, kQuadIndices, 6);
    };

    // Overlay pass 1 (C.2): 8-direction water-neighbor mask -> LUT0.
    int mask = 0;
    for (int dir = 0; dir < 8; ++dir) {
        if (texture_index_at(tx + kDirDx[dir], ty + kDirDy[dir]) <= 2) {
            mask |= (1 << dir);
        }
    }
    const Uint8 overlay1 = kShoreLUT0[mask];
    if (overlay1 != 255) {
        if (overlay1 <= 52) {
            draw_overlay(shore_atlas_textures_[0], overlay1);
        } else {
            draw_overlay(shore_atlas_textures_[1], overlay1 - 53);
        }
    }

    // Overlay pass 2 (C.3): corner water-flags -> LUT85 -> dl -> shape.
    static constexpr int kCornerDirs[4] = {0, 2, 4, 6};  // NW, NE, SE, SW
    int flags = 0;
    for (int corner_dir : kCornerDirs) {
        if (texture_index_at(tx + kDirDx[corner_dir], ty + kDirDy[corner_dir]) <= 2) {
            flags |= (1 << corner_dir);
        }
    }
    if (flags != 0) {
        const Uint8 dl = kShoreLUT85[flags - 1];
        if (dl != 15) {
            draw_overlay(shore_atlas_textures_[0], kShoreDlToShape[dl]);
        }
    }
}

}  // namespace opente::render
