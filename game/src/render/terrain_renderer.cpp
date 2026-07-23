#include "render/terrain_renderer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <iterator>
#include <string>
#include <unordered_map>
#include <utility>

#include "render/iso.h"

namespace opente::render {

namespace {

/// Palette-field 4cc labels for texture indices 0-13 (generic names from the
/// original `terr/sets` palette structure, not culture-specific resolved tags).
constexpr const char* kTerrainIndexLabels[14] = {
    "",     "deep", "seas", "alps", "bld0", "bld1", "bld2",
    "hill", "mntn", "undr", "soil", "?",    "dsr0", "dsr1",
};

/// Extra alti units the skirt extends below sea level, so even flat water
/// edges get a visible cliff face.
constexpr float kSkirtExtension = 16.0f;

/// Skirt vertex color: cliff faces use a fixed top/bottom gradient
constexpr float kSkirtTopShade = 0.80f;
constexpr float kSkirtBottomShade = 0.55f;

/// Skirt UV tiling: the edge texture repeats every `kSkirtUPeriod` edge
/// segments along the edge (U axis), matching the terrain base pass's
/// 8-tile U period. V uses the same world-pixel scale for square texels.
constexpr float kSkirtUPeriod = 8.0f;

/// Two-triangle quad winding for rectangular geometry (skirt pass only).
constexpr int kQuadIndices[6] = {0, 1, 2, 0, 2, 3};

/// 4-triangle fan winding for diamond terrain tiles: vertex 0 is the tile
/// center, vertices 1-4 are the NW/NE/SE/SW corners.
constexpr int kFanIndices[12] = {0, 1, 2, 0, 2, 3, 0, 3, 4, 0, 4, 1};

/// 8-direction neighbor offsets, `world-and-maps.md` order:
/// 0=NW 1=N 2=NE 3=E 4=SE 5=S 6=SW 7=W.
constexpr int kDirDx[8] = {-1, 0, 1, 1, 1, 0, -1, -1};
constexpr int kDirDy[8] = {-1, -1, -1, 0, 1, 1, 1, 0};

/// UV cell size and half-texel edge inset for sampling the edge-blend dissolve atlas without bleeding into adjacent cells.
constexpr float kTranCellUV = 64.0f / 256.0f;
constexpr float kTranInsetUV = 0.5f / 256.0f;

/// The 4 edge-blend directions, in `world-and-maps.md`'s 8-direction
/// order (1=N, 3=E, 5=S, 7=W -- see `render_edge_blends`).
constexpr int kEdgeBlendDirs[4] = {1, 3, 5, 7};
constexpr int kEdgeBlendK[4] = {2, 3, 0, 1};

/// Stage D network-overlay LUTs, decoded from static seed data in the original EXE's .rdata
/// (seeds @ 0x5fc594 / 0x5fc5e8 / 0x5fc60c).  Each LUT maps an 8-bit key to a cell index
/// (0-based) into the corresponding atlas page, or 0xff meaning "draw nothing".
/// See documentation/extracted/exe_trail_re_findings.md for the full derivation.
///
/// kTrailLUT: accumulated connectivity code → cell_code (0-80).
///   code < 53  → trail1 atlas (page 0xf4), atlas_cell = code.
///   code >= 53 → trail2 atlas (page 0xf5), atlas_cell = code - 53.
constexpr std::uint8_t kTrailLUT[256] = {
    0xff, 0x30, 0x2e, 0x05, 0x32, 0x03, 0x02, 0x09, 0x2c, 0x00, 0x04, 0x08, 0x01, 0x07, 0x06, 0x0a,
    0x31, 0xff, 0x47, 0xff, 0x4f, 0xff, 0x40, 0xff, 0x4c, 0xff, 0x38, 0xff, 0x3c, 0xff, 0x42, 0xff,
    0x2f, 0x46, 0xff, 0xff, 0x49, 0x36, 0xff, 0xff, 0x4d, 0x41, 0xff, 0xff, 0x3d, 0x3a, 0xff, 0xff,
    0x10, 0xff, 0xff, 0xff, 0x1f, 0xff, 0xff, 0xff, 0x1e, 0xff, 0xff, 0xff, 0x2b, 0xff, 0xff, 0xff,
    0x33, 0x50, 0x4a, 0x3e, 0xff, 0xff, 0xff, 0xff, 0x45, 0x43, 0x37, 0x3b, 0xff, 0xff, 0xff, 0xff,
    0x0e, 0xff, 0x17, 0xff, 0xff, 0xff, 0xff, 0xff, 0x18, 0xff, 0x16, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0d, 0x23, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x22, 0x2a, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x14, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x24, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x2d, 0x4b, 0x4e, 0x3f, 0x48, 0x35, 0x44, 0x39, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0b, 0xff, 0x21, 0xff, 0x20, 0xff, 0x29, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0f, 0x1b, 0xff, 0xff, 0x1a, 0x19, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x13, 0xff, 0xff, 0xff, 0x27, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x0c, 0x1c, 0x1d, 0x28, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x12, 0xff, 0x25, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x11, 0x26, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x15, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x34,
};

/// kRailLUT: accumulated connectivity code → cell (0-40, all on the rail atlas,
/// page 0xf6). Rail shares the accumulated-code space with trail/road: pure-rail
/// cells 0-10 (straights/turns/tees/cross) and endpoints 37-40 key on the low
/// nibble; cells 11-36 key on rail low-nibble + road high-nibble combinations
/// (road-over-rail level crossings). Rail is cardinal-only.
constexpr std::uint8_t kRailLUT[256] = {
    0xff, 0x28, 0x25, 0x05, 0x26, 0x00, 0x03, 0x09, 0x27, 0x02, 0x01, 0x06, 0x04, 0x08, 0x07, 0x0a,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x1c, 0x1e, 0xff, 0xff, 0x1f, 0x1a, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x0e, 0xff, 0xff, 0x24, 0xff, 0xff, 0x23, 0x13, 0xff, 0x11, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x0c, 0xff, 0x1b, 0xff, 0xff, 0xff, 0xff, 0x18, 0xff, 0x10, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x0b, 0xff, 0x16, 0xff, 0xff, 0xff, 0xff, 0x19, 0xff, 0x0f, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0x0d, 0xff, 0x15, 0x21, 0xff, 0xff, 0x22, 0xff, 0xff, 0x12, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0x17, 0x20, 0xff, 0xff, 0x1d, 0x14, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// kCanalMouthLUT: canal connectivity byte → sea-mouth cell (45-52) on the
/// canal atlas, used when a canal tile touches shore-water (see the canal
/// branch of draw_network_conn). Decoded byte-exact from the EXE's dispatch
/// table 0x46c3c8 + stub jump table 0x46c3a4. The 8 valid keys are exactly
/// the 8 single-connection (endpoint) configurations — a canal opening into
/// the sea: cardinal endpoints 0x02/0x08/0x20/0x80 → cells 49/45/47/51 and
/// diagonal endpoints 0x0e/0x38/0x83/0xe0 (NE/SE/NW/SW) → 48/46/52/50.
constexpr std::uint8_t kCanalMouthLUT[256] = {
    0xff, 0xff, 0x31, 0xff, 0xff, 0xff, 0xff, 0xff, 0x2d, 0xff, 0xff, 0xff, 0xff, 0xff, 0x30, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x2f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x2e, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x33, 0xff, 0xff, 0x34, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x32, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// kCanalLUT: canal connectivity byte → cell (0-32) on the canal atlas
/// (page 0xfa). Canal is the only 8-directional network; a diagonal
/// connection is keyed as its diagonal bit plus BOTH flanking cardinal bits
/// (NE = 0x0e, SE = 0x38, SW = 0xe0, NW = 0x83). The 33 valid keys are: 8
/// single-connection endpoints, 2 cardinal + 2 diagonal straights, all
/// two-connection bends (cardinal-cardinal, cardinal-diagonal,
/// diagonal-diagonal), and the cardinal tees + 4-way cross.
constexpr std::uint8_t kCanalLUT[256] = {
    0xff, 0xff, 0x16, 0xff, 0xff, 0xff, 0xff, 0xff, 0x15, 0xff, 0x1c, 0xff, 0xff, 0xff, 0x04, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x17, 0xff, 0x0a, 0xff, 0xff, 0xff, 0xff, 0xff, 0x19, 0xff, 0x20, 0xff, 0xff, 0xff, 0x12, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0c, 0xff, 0x03, 0xff, 0xff, 0xff, 0x07, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x18, 0xff, 0x1a, 0x0d, 0xff, 0xff, 0xff, 0xff, 0x08, 0xff, 0x1d, 0x01, 0xff, 0xff, 0x0f, 0x11,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x1b, 0xff, 0x1e, 0x00, 0xff, 0xff, 0xff, 0xff, 0x1f, 0xff, 0x14, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0xff, 0xff, 0x06, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    0x05, 0xff, 0x0e, 0x10, 0xff, 0xff, 0xff, 0xff, 0x13, 0xff, 0xff, 0xff, 0xff, 0xff, 0x0b, 0xff,
    0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x09, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
};

/// Picks which shore-edge sprite to draw based on which of a tile's 8 neighbors are water. 255 means draw nothing.
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

/// Picks a concave-corner shore sprite based on which diagonal (corner) neighbors are water. 15 means draw nothing.
constexpr Uint8 kShoreLUT85[85] = {
     0, 15, 15,  1,  2, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  3,  4,
    15, 15,  5,  6, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,
    15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15,  7,  8, 15, 15,  9,
    10, 15, 15, 15, 15, 15, 15, 15, 15, 15, 15, 11, 12, 15, 15, 13, 14,
};

/// Translates the corner-overlay index from kShoreLUT85 into a sprite cell for the concave-corner overlay.
constexpr int kShoreDlToShape[15] = {1, 2, 7, 3, 5, 9, 11, 0, 6, 4, 10, 8, 13, 12, 14};

/// UV coordinates for each sprite cell in the shore overlay atlas, used by both edge and corner overlays.
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

/// Converts a raw atlas index into the UV float value used to sample a shore overlay sprite.
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

    auto tile_height = [&](int tx, int ty) -> float {
        tx = std::clamp(tx, 0, width - 1);
        ty = std::clamp(ty, 0, height - 1);
        return static_cast<float>(region_->height_at(tx, ty));
    };

    // Each vertex at integer grid position gets its height
    // from the single tile at that coordinate — no 4-tile averaging.
    // Smooth visual appearance comes from slope-based vertex shading
    // (8-neighbor normals), not height smoothing.
    terrain_vertex_height_.assign(static_cast<std::size_t>(vw * vh), 0.0f);
    for (int row = 0; row < vh; ++row) {
        for (int col = 0; col < vw; ++col) {
            terrain_vertex_height_[static_cast<std::size_t>(row * vw + col)] = tile_height(col, row);
        }
    }

    rebuild_vertex_colors();
}

void TerrainRenderer::rebuild_vertex_colors() {
    const int vw = region_->width() + 1;   // num verts along width
    const int vh = region_->height() + 1;  // num verts along height

    // Per-vertex slope shading: 8-neighbor cross-product normal, per-channel ambient.
    auto vertex_height = [&](int col, int row) -> float {
        col = std::clamp(col, 0, vw - 1);
        row = std::clamp(row, 0, vh - 1);
        return terrain_vertex_height_[static_cast<std::size_t>(row) * vw + col];
    };

    terrain_vertex_color_.assign(static_cast<std::size_t>(vw) * vh, SDL_Color{255, 255, 255, 255});
    for (int row = 0; row < vh; ++row) {
        for (int col = 0; col < vw; ++col) {
            const float hc = vertex_height(col, row);

            // 8 direction vectors (kDirDx/kDirDy order).
            Vec3 dirs[8];
            for (int d = 0; d < 8; ++d) {
                const float hn = vertex_height(col + kDirDx[d], row + kDirDy[d]);
                dirs[d] = {static_cast<float>(kDirDx[d]),
                           static_cast<float>(kDirDy[d]),
                           (hn - hc) * kAltiToWorldHeight * kSlopeGradientScale};
            }

            // Sum of 8 cross products of adjacent direction pairs (full fan).
            Vec3 accum{0, 0, 0};
            for (int i = 0; i < 8; ++i) {
                const Vec3& a = dirs[i];
                const Vec3& b = dirs[(i + 1) & 7];
                accum.x += a.y * b.z - a.z * b.y;
                accum.y += a.z * b.x - a.x * b.z;
                accum.z += a.x * b.y - a.y * b.x;
            }

            const float len = std::sqrt(accum.x * accum.x + accum.y * accum.y + accum.z * accum.z);
            if (len < 1e-12f) {
                terrain_vertex_color_[static_cast<std::size_t>(row) * vw + col] = SDL_Color{255, 255, 255, 255};
                continue;
            }
            const float inv_len = 1.0f / len;
            const float dot = std::max(0.0f,
                accum.x * inv_len * kLightDir.x +
                accum.y * inv_len * kLightDir.y +
                accum.z * inv_len * kLightDir.z);

            const auto r = static_cast<Uint8>(std::min(255.0f, ((1.0f - kAmbientR) * dot + kAmbientR) * kVertexColorScale * 255.0f));
            const auto g = static_cast<Uint8>(std::min(255.0f, ((1.0f - kAmbientG) * dot + kAmbientG) * kVertexColorScale * 255.0f));
            const auto b_ch = static_cast<Uint8>(std::min(255.0f, ((1.0f - kAmbientB) * dot + kAmbientB) * kVertexColorScale * 255.0f));
            terrain_vertex_color_[static_cast<std::size_t>(row) * vw + col] = SDL_Color{r, g, b_ch, 255};
        }
    }
}

void TerrainRenderer::set_tileset(const TerrainTileset* tileset) {
    tileset_ = tileset;
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
    // Each tile is a quad whose 4 corners are shared
    // vertices from `terrain_vertex_height_`/`terrain_vertex_color_`, so
    // adjacent tiles' edges always coincide.
    //
    // UV mapping: screen-axis-aligned "diamond" UV, where U varies along the
    // screen-horizontal axis (tx-ty) and V along the screen-depth axis (tx+ty).
    //
    // Per-corner offsets (du, dv) relative to the tile's NW-corner base:
    //   NW (dc=0,dr=0): du=0,  dv=0
    //   NE (dc=1,dr=0): du=+1, dv=+1
    //   SE (dc=1,dr=1): du=0,  dv=+2
    //   SW (dc=0,dr=1): du=-1, dv=+1
    // i.e. du = (dc-dr)/kU, dv = (dc+dr)/kV.
    //
    // SDL2 sets GL_CLAMP_TO_EDGE on every texture (no GL_REPEAT), so raw UVs
    // outside [0,1] must be avoided. The terrain page textures are 2×-wide +
    // 2×-tall tiled copies (built by TerrainTileset::load), so the
    // safe addressable range is wider.  The UV formulas are:
    //
    //   u_tex = (u_raw + 1.0) * 0.5    // u_raw ∈ [-1/8, 1] → u_tex ∈ [7/16, 1]
    //   v_tex = v_raw * 0.5             // v_raw ∈ [0, 1+2/16] → v_tex ∈ [0, ~0.56]
    //
    // Corner order is fixed as NW, NE, SE, SW for every pass below.
    static constexpr int kCornerCol[4] = {0, 1, 1, 0};
    static constexpr int kCornerRow[4] = {0, 0, 1, 1};
    static constexpr float kUPeriod = 8.0f;
    static constexpr float kVPeriod = 16.0f;

    render_background(camera);

    render_skirts(camera);

    for (int ty = 0; ty < height; ++ty) {
        for (int tx = 0; tx < width; ++tx) {
            const int own_index = texture_index_at(tx, ty);

            // UV base for this tile's NW corner (tx, ty) in screen-axis-aligned
            // diamond UV space: u = (tx-ty)/kU, v = (tx+ty)/kV.
            // Positive fmod keeps the base in [0,1); per-corner offsets then
            // push NE/SE slightly above or SW slightly below that range.
            const float u_nw = std::fmod(static_cast<float>(tx - ty) + kUPeriod * 1000.0f, kUPeriod) / kUPeriod;
            const float v_nw = std::fmod(static_cast<float>(tx + ty) + kVPeriod * 1000.0f, kVPeriod) / kVPeriod;

            // 5-vertex fan: verts[0] = tile center, verts[1..4] = NW/NE/SE/SW corners.
            SDL_Vertex verts[5];
            for (int c = 0; c < 4; ++c) {
                const int col = tx + kCornerCol[c];
                const int row = ty + kCornerRow[c];
                const std::size_t vidx = static_cast<std::size_t>(row) * vw + col;
                const float height_offset = terrain_vertex_height_[vidx] * kPixelsPerAltiUnit;

                Vec2 world_pos = tile_to_world(static_cast<float>(col), static_cast<float>(row));
                world_pos.y -= height_offset;
                const Vec2 screen_pos = camera.world_to_screen(world_pos);

                verts[1 + c].position = {screen_pos.x, screen_pos.y};
                verts[1 + c].color = slope_shading_enabled
                    ? terrain_vertex_color_[vidx]
                    : SDL_Color{255, 255, 255, 255};
                // u_raw + 1.0 shifts the [-1/8, 1] range into [7/8, 2], then *0.5
                // maps to [7/16, 1] in the 2×-wide tiled texture — always in range.
                // v_raw is always ≥ 0, and *0.5 maps [0, 1+2/16] to [0, ~0.56]
                // in the 2×-tall tiled texture — also always in range.
                verts[1 + c].tex_coord = {
                    (u_nw + static_cast<float>(kCornerCol[c] - kCornerRow[c]) / kUPeriod + 1.0f) * 0.5f,
                    (v_nw + static_cast<float>(kCornerCol[c] + kCornerRow[c]) / kVPeriod) * 0.5f};
            }

            // Center vertex height: bilinear interpolation at (tx+0.5, ty+0.5)
            // = average of the 4 corner vertex heights, matching the original
            // game's bilinear sampler (fcn.00463420) at non-integer positions.
            {
                const std::size_t nw = static_cast<std::size_t>(ty) * vw + tx;
                const float center_h_offset =
                    (terrain_vertex_height_[nw] +
                     terrain_vertex_height_[nw + 1] +
                     terrain_vertex_height_[nw + vw] +
                     terrain_vertex_height_[nw + vw + 1]) * 0.25f
                    * kPixelsPerAltiUnit;
                Vec2 center_world = tile_to_world(
                    static_cast<float>(tx) + 0.5f,
                    static_cast<float>(ty) + 0.5f);
                center_world.y -= center_h_offset;
                const Vec2 center_screen = camera.world_to_screen(center_world);
                verts[0].position = {center_screen.x, center_screen.y};
            }
            verts[0].color = {
                static_cast<Uint8>((verts[1].color.r + verts[2].color.r +
                                    verts[3].color.r + verts[4].color.r) / 4),
                static_cast<Uint8>((verts[1].color.g + verts[2].color.g +
                                    verts[3].color.g + verts[4].color.g) / 4),
                static_cast<Uint8>((verts[1].color.b + verts[2].color.b +
                                    verts[3].color.b + verts[4].color.b) / 4),
                static_cast<Uint8>((verts[1].color.a + verts[2].color.a +
                                    verts[3].color.a + verts[4].color.a) / 4)};
            // Center UV: at (tx+0.5, ty+0.5), u_raw = u_nw, v_raw = v_nw + 1/kV.
            verts[0].tex_coord = {(u_nw + 1.0f) * 0.5f, (v_nw + 1.0f / kVPeriod) * 0.5f};

            // Base pass: the tile's own texture page, or flat slope-shaded color if textures are disabled / no tileset set.
            SDL_RenderGeometry(renderer_,
                               tileset_ ? tileset_->page(own_index) : nullptr,
                               verts, 5, kFanIndices, 12);

            // Edge-blend passes between same-class, differently-textured neighboring tiles.
            if (terrain_blending_enabled) {
                render_edge_blends(tx, ty, verts + 1);
            }

            // Shore-overlay passes at water/land boundaries.
            if (shore_overlays_enabled) {
                render_shore_overlays(tx, ty, verts + 1);
            }

            // Network-overlay pass: trail/road/canal/rail decals (Stage D).
            if (network_overlays_enabled) {
                render_network_decal(tx, ty, verts + 1);
            }

            // DEBUG-only visualization
            if (terrain_debug_labels_enabled && own_index >= 0 && own_index < 14) {
                const char* label = kTerrainIndexLabels[own_index];
                if (label[0] != '\0') {
                    const ImVec2 text_size = ImGui::CalcTextSize(label);
                    const ImVec2 text_pos(verts[0].position.x - text_size.x * 0.5f,
                                          verts[0].position.y - text_size.y * 0.5f);
                    ImDrawList* dl = ImGui::GetBackgroundDrawList();
                    dl->AddRectFilled(text_pos, ImVec2(text_pos.x + text_size.x, text_pos.y + text_size.y),
                                      IM_COL32(0, 0, 0, 128));
                    dl->AddText(text_pos, IM_COL32(255, 255, 0, 255), label);
                }
            }
        }
    }
}

void TerrainRenderer::render_edge_blends(int tx, int ty, const SDL_Vertex corners[4]) const {
    if (!tileset_ || !tileset_->tran()) {
        return;
    }

    const int own_index = texture_index_at(tx, ty);
    const bool own_is_water = own_index <= 2;

    // For each of the 4 cardinal directions, queue an edge-blend pass
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

        // UV rect for the `tran` atlas cell
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
        SDL_RenderGeometry(renderer_, tileset_->tran(),
                           fan, 5, kFanIndices, 12);
    }
}

void TerrainRenderer::render_shore_overlays(int tx, int ty, const SDL_Vertex corners[4]) const {
    SDL_Texture* sh0 = tileset_ ? tileset_->shore(0) : nullptr;
    SDL_Texture* sh1 = tileset_ ? tileset_->shore(1) : nullptr;

    auto draw_overlay = [&](SDL_Texture* atlas, int cell) {
        if (!atlas || cell < 0 || cell >= static_cast<int>(std::size(kShoreUvIndex))) {
            return;
        }
        // Small UV insets and a vertical nudge to prevent neighboring atlas cells from bleeding into the seams.
        constexpr float kShoreInsetU = 1.0f / 128.0f;
        constexpr float kShoreInsetV = 1.0f / 256.0f;
        constexpr float kShoreMidVBias = 0.002f;

        const auto [idx0, idx1] = kShoreUvIndex[cell];
        const float left_u  = shore_uv(idx0)     + kShoreInsetU;
        const float mid_u   = shore_uv(idx0 + 2);
        const float right_u = shore_uv(idx0 + 4) - kShoreInsetU;
        const float top_v   = shore_uv(idx1 + 1) + kShoreInsetV;
        const float mid_v   = shore_uv(idx1 + 2) + kShoreMidVBias;
        const float bot_v   = shore_uv(idx1 + 3) - kShoreInsetV;

        SDL_Vertex fan[5];
        fan[0].position = {
            (corners[0].position.x + corners[1].position.x +
             corners[2].position.x + corners[3].position.x) * 0.25f,
            (corners[0].position.y + corners[1].position.y +
             corners[2].position.y + corners[3].position.y) * 0.25f};
        fan[0].color = corners[0].color;
        fan[0].tex_coord = {mid_u, mid_v};
        for (int j = 0; j < 4; ++j) {
            fan[1 + j] = corners[j];
        }
        fan[1].tex_coord = {mid_u,   top_v};   // NW = screen top
        fan[2].tex_coord = {right_u, mid_v};    // NE = screen right
        fan[3].tex_coord = {mid_u,   bot_v};    // SE = screen bottom
        fan[4].tex_coord = {left_u,  mid_v};    // SW = screen left
        SDL_RenderGeometry(renderer_, atlas, fan, 5, kFanIndices, 12);
    };

    // Shore overlays only appear on land tiles, not water.
    // texture_index <= 2 ("deep"/"seas") catches tiles whose terrain type is
    // stale/wrong in the data but whose texture reveals they are in the water zone.
    if (region_->terrain_at(tx, ty) == world::TerrainType::DeepWater ||
        region_->terrain_at(tx, ty) == world::TerrainType::ShallowWater) {
        return;
    }

    const std::uint8_t mask = water_neighbor_mask(tx, ty);
    if (mask == 0xFF) {
        return;
    }

    // Draw the shore-edge sprite first, then the concave-corner sprite on top.
    const Uint8 overlay1 = kShoreLUT0[mask];
    if (overlay1 != 255) {
        if (overlay1 <= 52) {
            draw_overlay(sh0, overlay1);
        } else {
            draw_overlay(sh1, overlay1 - 53);
        }
    }

    // A corner flag fires when a diagonal neighbor is water but both adjacent cardinal neighbors are not.
    int flags = 0;
    if ((mask & 0x83) == 0x82) flags |= 0x01;
    if ((mask & 0x0E) == 0x0A) flags |= 0x04;
    if ((mask & 0x38) == 0x28) flags |= 0x10;
    if ((mask & 0xE0) == 0xA0) flags |= 0x40;
    if (flags != 0) {
        const Uint8 dl = kShoreLUT85[flags - 1];
        if (dl != 15) {
            draw_overlay(sh0, kShoreDlToShape[dl]);
        }
    }
}

void TerrainRenderer::render_skirts(const Camera& camera) const {
    const int width = region_->width();
    const int height = region_->height();
    const int vw = width + 1;
    const float sea_height = static_cast<float>(region_->sea_level());
    const float bottom_height = std::max(0.0f, sea_height - kSkirtExtension);

    SDL_Texture* tex = tileset_ ? tileset_->edge() : nullptr;

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

        const SDL_Color tc = dim_color({255, 255, 255, 255}, kSkirtTopShade);
        const SDL_Color bc = dim_color({255, 255, 255, 255}, kSkirtBottomShade);

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
            {{sp0.x, sp0.y}, tc, {u0, v_top0}},
            {{sp1.x, sp1.y}, tc, {u1, v_top1}},
            {{bsp1.x, bsp1.y}, bc, {u1, 0.0f}},
            {{bsp0.x, bsp0.y}, bc, {u0, 0.0f}},
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

void TerrainRenderer::render_network_decal(int tx, int ty, const SDL_Vertex corners[4]) const {
    draw_network_conn(region_->connectivity_at(tx, ty), tx, ty, corners);
}

std::uint8_t TerrainRenderer::water_neighbor_mask(int tx, int ty) const {
    const int w = region_->width();
    const int h = region_->height();
    std::uint8_t mask = 0xFF;
    for (int dir = 0; dir < 8; ++dir) {
        const int nx = tx + kDirDx[dir];
        const int ny = ty + kDirDy[dir];
        if (nx >= 0 && nx < w && ny >= 0 && ny < h &&
            texture_index_at(nx, ny) == 2) {
            mask = static_cast<std::uint8_t>(mask & ~(1 << dir));
        }
    }
    return mask;
}

void TerrainRenderer::draw_network_conn(const world::TileConnectivity& conn,
                                        int tx, int ty,
                                        const SDL_Vertex corners[4]) const {
    if (!tileset_) return;

    // UV draw helper: reuses kShoreUvIndex (same 53-cell atlas layout as shore overlays).
    auto draw_cell = [&](SDL_Texture* atlas, int cell) {
        if (!atlas || cell < 0 || cell >= static_cast<int>(std::size(kShoreUvIndex))) return;

        // EXE-exact UVs -- the original applies no insets or bias, and none are
        // safe here: at 1:1 zoom pixel centers sample at u=(k+0.5)/cellWidth,
        // so even a half-texel inset pushes EVERY sample across a texel
        // boundary and shifts the whole cell's art by a full pixel (this
        // visibly misaligned adjacent rail segments). Nearest-neighbor
        // interpolation at pixel centers never reaches the exact cell
        // boundary, so no anti-bleed guard is needed either.
        const auto [idx0, idx1] = kShoreUvIndex[cell];
        const float left_u  = shore_uv(idx0);
        const float mid_u   = shore_uv(idx0 + 2);
        const float right_u = shore_uv(idx0 + 4);
        const float top_v   = shore_uv(idx1 + 1);
        const float mid_v   = shore_uv(idx1 + 2);
        const float bot_v   = shore_uv(idx1 + 3);

        SDL_Vertex fan[5];
        fan[0].position = {
            (corners[0].position.x + corners[1].position.x +
             corners[2].position.x + corners[3].position.x) * 0.25f,
            (corners[0].position.y + corners[1].position.y +
             corners[2].position.y + corners[3].position.y) * 0.25f};
        fan[0].color = corners[0].color;
        fan[0].tex_coord = {mid_u, mid_v};
        for (int j = 0; j < 4; ++j) fan[1 + j] = corners[j];
        fan[1].tex_coord = {mid_u,   top_v};
        fan[2].tex_coord = {right_u, mid_v};
        fan[3].tex_coord = {mid_u,   bot_v};
        fan[4].tex_coord = {left_u,  mid_v};
        SDL_RenderGeometry(renderer_, atlas, fan, 5, kFanIndices, 12);
    };

    // extract4: packs bits 1,3,5,7 of a raw connectivity byte into bits 0,1,2,3.
    // Bit layout in raw byte: 0x02=N, 0x08=E, 0x20=S, 0x80=W.
    // After extraction: bit0=N, bit1=E, bit2=S, bit3=W.
    auto extract4 = [](std::uint8_t raw) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            ((raw >> 1) & 0x01u) |
            ((raw >> 2) & 0x02u) |
            ((raw >> 3) & 0x04u) |
            ((raw >> 4) & 0x08u));
    };

    // Priority 1: canal (byte 3 -- tested first in the EXE builder, 0x46bf08).
    if (conn.canal != 0) {
        // A canal endpoint at shore-water opens into the sea: the shore
        // water-neighbor mask is fed through kShoreLUT0 (EXE 0x46bf13) and
        // any hit selects the sea-mouth dispatch on the canal key. No
        // fallback to the land LUT on a dispatch miss.
        if (kShoreLUT0[water_neighbor_mask(tx, ty)] != 0xff) {
            const std::uint8_t cell = kCanalMouthLUT[conn.canal];
            if (cell != 0xff) draw_cell(tileset_->network(2), cell);
            return;
        }
        const std::uint8_t cell = kCanalLUT[conn.canal];
        if (cell != 0xff) draw_cell(tileset_->network(2), cell);
        return;
    }

    // Bridge tiles draw no trail/road/rail decal (EXE 0x46c082-0x46c0e0):
    // the bridge visual is a separate sprite pass (GameplayScene::render_bridges,
    // bridge-plan.md WP5), so Stage D must not draw the network segment
    // underneath it. The suppression value computed here is the SAME number the
    // deck sprite's variant id uses -- keep the two in sync.
    if (conn.bridge != 0xff) {
        const std::uint8_t dirs = conn.trail ? conn.trail
                                 : conn.road ? conn.road
                                             : conn.rail;
        if (extract4(dirs) + conn.bridge_aux != 0) return;
    }

    // Build the 8-bit accumulated connectivity code (3-phase, EXE 0x46c0e6–0x46c17d).
    //   Phase 1: pack trail byte cardinal bits into bits 0-3 (low nibble).
    //   Phase 2: road bits clear the matching trail bit and set bits 4-7
    //            (a road replaces that direction's trail sprite -- roads are
    //            the "upgrade" network).
    //   Phase 3: rail bits OR into bits 0-3 (UNCONDITIONAL in the EXE).
    std::uint8_t code = extract4(conn.trail);
    const std::uint8_t r4 = extract4(conn.road);
    code = static_cast<std::uint8_t>((code & ~r4) | (r4 << 4));
    code |= extract4(conn.rail);

    if (code == 0) return;

    // Priority 2: rail, selected iff the rail byte is non-zero (EXE 0x46c185).
    if (conn.rail != 0) {
        const std::uint8_t cell = kRailLUT[code];
        if (cell != 0xff) draw_cell(tileset_->network(3), cell);
        return;
    }

    // Priority 3: trail / road (trail1 atlas if cell_code < 53, else trail2).
    const std::uint8_t cell_code = kTrailLUT[code];
    if (cell_code == 0xff) return;
    if (cell_code < 53) {
        draw_cell(tileset_->network(0), cell_code);
    } else {
        draw_cell(tileset_->network(1), cell_code - 53);
    }
}

void TerrainRenderer::render_preview_path(
    const std::vector<std::pair<int,int>>& waypoints,
    int cursor_tx, int cursor_ty,
    const std::string& path_type,
    const Camera& camera) const
{
    if (!tileset_ || waypoints.empty()) return;

    const int map_w = region_->width();
    const int map_h = region_->height();
    const int vw    = map_w + 1;

    // Direction bits for a one-tile step (dx, dy), matching the connectivity
    // byte encoding (see TileConnectivity): cardinals 0x02=N, 0x08=E, 0x20=S,
    // 0x80=W; a diagonal step contributes its diagonal bit PLUS both flanking
    // cardinals (the canal-key encoding, e.g. NE = 0x04|0x02|0x08 = 0x0e).
    // The opposite direction is dir_bits(-dx, -dy).
    auto dir_bits = [](int dx, int dy) -> std::uint8_t {
        std::uint8_t b = 0;
        if (dy < 0) b |= 0x02;
        if (dx > 0) b |= 0x08;
        if (dy > 0) b |= 0x20;
        if (dx < 0) b |= 0x80;
        if (dx != 0 && dy != 0) {
            if (dx > 0) b |= (dy < 0) ? 0x04 : 0x10;  // NE : SE
            else        b |= (dy < 0) ? 0x01 : 0x40;  // NW : SW
        }
        return b;
    };

    // Segments: committed waypoint[i-1]→waypoint[i], then live last→cursor.
    struct Seg { int x0, y0, x1, y1; };
    std::vector<Seg> segments;
    segments.reserve(waypoints.size());
    for (int i = 1; i < static_cast<int>(waypoints.size()); ++i)
        segments.push_back({waypoints[i-1].first, waypoints[i-1].second,
                            waypoints[i].first,   waypoints[i].second});
    segments.push_back({waypoints.back().first, waypoints.back().second,
                        cursor_tx, cursor_ty});

    // Build per-tile connectivity by rasterizing each segment. Trails, roads,
    // and rails are cardinal-only networks (the EXE's code-builder never reads
    // their diagonal bits, confirmed against authored mapp.path data), so
    // their segments step ONE axis per tile -- a diagonal drag becomes a
    // staircase of corner tiles. Canals are 8-directional and may step
    // diagonally, producing true diagonal channel cells.
    // key = ty * map_w + tx
    std::unordered_map<int, world::TileConnectivity> tile_conns;

    auto apply_bits_to = [&](int tx, int ty, std::uint8_t bits) {
        if (tx < 0 || tx >= map_w || ty < 0 || ty >= map_h) return;
        world::TileConnectivity& c = tile_conns[ty * map_w + tx];
        if      (path_type == "trai") { c.trail |= bits; }
        else if (path_type == "road") { c.road  |= bits; }
        else if (path_type == "cana") { c.canal |= bits; }
        else if (path_type == "rail") { c.rail  |= bits; }
    };

    const bool diagonal_steps = (path_type == "cana");

    for (const Seg& seg : segments) {
        int x = seg.x0, y = seg.y0, px = -1, py = -1;
        while (true) {
            if (px != -1) {
                const int dx = x - px;
                const int dy = y - py;
                apply_bits_to(px, py, dir_bits(dx, dy));
                apply_bits_to(x, y, dir_bits(-dx, -dy));
                if (dx != 0 && dy != 0) {
                    // A diagonal step also writes the two tiles flanking the
                    // shared corner, each with a 3-bit fan on the PERPENDICULAR
                    // diagonal pointing at the other flank -- so flanks render
                    // diagonal end-cap cells that widen the channel, not corner
                    // bends. Decoded byte-exact from the original's pathway
                    // applier (EXE 0x4997eb-0x499877 and 0x49996f: flank fans
                    // {d+1,d+2,d+3} and {d+5,d+6,d+7} around step direction d).
                    apply_bits_to(px + dx, py, dir_bits(-dx, dy));
                    apply_bits_to(px, py + dy, dir_bits(dx, -dy));
                }
            }
            px = x; py = y;
            if (x == seg.x1 && y == seg.y1) break;
            if (diagonal_steps && x != seg.x1 && y != seg.y1) {
                // Canal: step both axes while both differ (45-degree run).
                x += (seg.x1 > x) ? 1 : -1;
                y += (seg.y1 > y) ? 1 : -1;
            } else if (std::abs(seg.x1 - x) >= std::abs(seg.y1 - y) && x != seg.x1) {
                x += (seg.x1 > x) ? 1 : -1;
            } else {
                y += (seg.y1 > y) ? 1 : -1;
            }
        }
    }

    // For each affected tile, compute its 4 screen-space corners and draw the decal.
    static constexpr int kCornerCol[4] = {0, 1, 1, 0};
    static constexpr int kCornerRow[4] = {0, 0, 1, 1};

    for (const auto& [key, conn] : tile_conns) {
        const int tx = key % map_w;
        const int ty = key / map_w;

        SDL_Vertex corners[4];
        for (int c = 0; c < 4; ++c) {
            const int col = tx + kCornerCol[c];
            const int row = ty + kCornerRow[c];
            const std::size_t vidx = static_cast<std::size_t>(row) * vw + col;
            const float h_off = terrain_vertex_height_[vidx] * kPixelsPerAltiUnit;
            Vec2 wp = tile_to_world(static_cast<float>(col), static_cast<float>(row));
            wp.y -= h_off;
            const Vec2 sp = camera.world_to_screen(wp);
            corners[c].position  = {sp.x, sp.y};
            corners[c].color     = slope_shading_enabled
                                       ? terrain_vertex_color_[vidx]
                                       : SDL_Color{255, 255, 255, 255};
            corners[c].tex_coord = {0.0f, 0.0f};
        }

        draw_network_conn(conn, tx, ty, corners);
    }
}

void TerrainRenderer::render_background(const Camera& camera) const {
    if (!tileset_ || !tileset_->hidd()) {
        return;
    }

    const int map_w = region_->width();
    const int map_h = region_->height();
    const float sea_h = static_cast<float>(region_->sea_level()) * kPixelsPerAltiUnit;

    // Compute the visible tile range from the viewport corners.
    int vp_w = 0;
    int vp_h = 0;
    SDL_GetRendererOutputSize(renderer_, &vp_w, &vp_h);

    const Vec2 screen_corners[4] = {{0, 0},
                                    {static_cast<float>(vp_w), 0},
                                    {static_cast<float>(vp_w), static_cast<float>(vp_h)},
                                    {0, static_cast<float>(vp_h)}};

    float min_tx = 1e9f, max_tx = -1e9f;
    float min_ty = 1e9f, max_ty = -1e9f;
    for (const Vec2& sc : screen_corners) {
        const Vec2 world = {sc.x / camera.zoom + camera.world_pixel_offset.x,
                            sc.y / camera.zoom + camera.world_pixel_offset.y};
        const Vec2 tile = world_to_tile(world.x, world.y);
        min_tx = std::min(min_tx, tile.x);
        max_tx = std::max(max_tx, tile.x);
        min_ty = std::min(min_ty, tile.y);
        max_ty = std::max(max_ty, tile.y);
    }

    const int pad = 4;
    const int t_x0 = static_cast<int>(std::floor(min_tx)) - pad;
    const int t_y0 = static_cast<int>(std::floor(min_ty)) - pad;
    const int t_x1 = static_cast<int>(std::ceil(max_tx)) + pad;
    const int t_y1 = static_cast<int>(std::ceil(max_ty)) + pad;

    static constexpr int kCornerCol[4] = {0, 1, 1, 0};
    static constexpr int kCornerRow[4] = {0, 0, 1, 1};
    static constexpr float kUPeriod = 8.0f;
    static constexpr float kVPeriod = 16.0f;

    for (int ty = t_y0; ty < t_y1; ++ty) {
        for (int tx = t_x0; tx < t_x1; ++tx) {
            if (tx >= 0 && tx < map_w && ty >= 0 && ty < map_h) {
                continue;
            }

            // Screen-axis-aligned diamond UV: u=(tx-ty)/kU, v=(tx+ty)/kV
            const float u_nw = static_cast<float>(((((tx - ty) % 8) + 8) % 8)) / kUPeriod;
            const float v_nw = static_cast<float>(((((tx + ty) % 16) + 16) % 16)) / kVPeriod;

            SDL_Vertex verts[5];
            for (int c = 0; c < 4; ++c) {
                const int col = tx + kCornerCol[c];
                const int row = ty + kCornerRow[c];

                Vec2 world_pos = tile_to_world(static_cast<float>(col), static_cast<float>(row));
                world_pos.y -= sea_h;
                const Vec2 screen_pos = camera.world_to_screen(world_pos);

                verts[1 + c].position = {screen_pos.x, screen_pos.y};
                verts[1 + c].color = {255, 255, 255, 255};
                verts[1 + c].tex_coord = {
                    (u_nw + static_cast<float>(kCornerCol[c] - kCornerRow[c]) / kUPeriod + 1.0f) * 0.5f,
                    (v_nw + static_cast<float>(kCornerCol[c] + kCornerRow[c]) / kVPeriod) * 0.5f};
            }

            verts[0].position = {
                (verts[1].position.x + verts[2].position.x +
                 verts[3].position.x + verts[4].position.x) * 0.25f,
                (verts[1].position.y + verts[2].position.y +
                 verts[3].position.y + verts[4].position.y) * 0.25f};
            verts[0].color = {255, 255, 255, 255};
            verts[0].tex_coord = {(u_nw + 1.0f) * 0.5f, (v_nw + 1.0f / kVPeriod) * 0.5f};

            SDL_RenderGeometry(renderer_, tileset_->hidd(),
                               verts, 5, kFanIndices, 12);
        }
    }
}

}  // namespace opente::render
