#pragma once

// Isometric tile <-> world-pixel projection. See
// OpenTE/spec/rendering.md "Isometric projection".

namespace opente::render {

/// Native tile pixel dimensions, confirmed from the original EXE's
/// `TMapView` projection constants (`0x5fc738` = 64.0, `0x5fc73c` = 32.0).
constexpr float kTileWidth = 64.0f;
constexpr float kTileHeight = 32.0f;

/// Converts a raw `mapp.alti` byte (0-255) to world-height units (0..9.96).
/// EXE-confirmed (`documentation/08-investigation-needed.md` B15, Rounds
/// 25-27): `height_world_units = alti_byte * 10.0 / 256.0`.
constexpr float kAltiToWorldHeight = 10.0f / 256.0f;

/// World-pixels per raw `mapp.alti` byte unit, for converting a vertex's
/// heightmap byte into a vertical world-pixel offset (subtracted from its
/// projected Y so higher ground is displaced upward on screen, before the
/// camera's zoom is applied).
///
/// EXE-confirmed (`documentation/08-investigation-needed.md` B15, Round 31):
/// the renderer's height->pixel setter `fcn.00468700` gives
/// `pixels_per_world_height_unit ~= 45.25 * zoom`. Folding in
/// `kAltiToWorldHeight` and dropping the `zoom` factor (applied uniformly by
/// `Camera::world_to_screen` afterwards) gives a zoom-independent
/// world-pixel-space constant: `45.25 * 10.0 / 256.0 ~= 1.7676`.
constexpr float kPixelsPerAltiUnit = 45.25f * kAltiToWorldHeight;

/// 3D vector, used only for the slope-shading normal/light-direction math
/// below.
struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

/// Lighting constants for per-vertex slope shading, EXE-confirmed from
/// TMapView ctor @ 0x467500 and virtual_252 @ 0x468b50.

/// Height-difference multiplier for the 8 direction vectors used in the
/// cross-product normal computation: each neighbor's direction vector is
/// Vec3(dx, dy, (h_neighbor - h_center) * kSlopeGradientScale).
constexpr float kSlopeGradientScale = 3.0f;  // [TMapView+0x238]  0x40400000

/// Fixed light direction (normalized). Computed in the EXE from elevation
/// = 0.85 rad and azimuth = 6.08 rad via sin/cos, then normalized.
constexpr Vec3 kLightDir = {-0.7358256578f, -0.1516010314f, 0.6599830985f};

/// Per-channel ambient coefficients. Shadowed faces get a warm reddish
/// tint (higher R ambient) rather than uniform gray.
/// Formula: byte = min(255, int(((1 - ambient) * max(0, dot) + ambient) * 2 * 255))
constexpr float kAmbientR = 0.1369999945f;  // [TMapView+0x23c]  0x3E0C49BA
constexpr float kAmbientG = 0.0430000015f;  // [TMapView+0x240]  0x3D3020C5
constexpr float kAmbientB = 0.0f;           // [TMapView+0x244]  0x00000000

/// The original game uses D3DTOP_MODULATE2X (value 5) for the terrain base
/// pass, which doubles vertex color before multiplying with the texture.
/// SDL2's SDL_RenderGeometry uses plain modulate, so we bake the 2x factor
/// into the vertex colors.  Evidence: flat terrain produces dot≈0.66 →
/// vertex color ~180/255 (70.6%), which requires 2x overbright to reach
/// full brightness.  The edge-blend function (0x42acf0) uses MODULATE4X.
constexpr float kVertexColorScale = 2.0f;

struct Vec2 {
    float x = 0;
    float y = 0;
};

/// Projects tile coordinates `(tx, ty)` to world pixel space (standard 2:1
/// "dimetric" projection).
inline Vec2 tile_to_world(float tx, float ty) {
    return {(tx - ty) * (kTileWidth / 2.0f), (tx + ty) * (kTileHeight / 2.0f)};
}

/// Inverse of `tile_to_world`: world pixel space -> tile coordinates.
inline Vec2 world_to_tile(float wx, float wy) {
    const float a = wx / (kTileWidth / 2.0f);
    const float b = wy / (kTileHeight / 2.0f);
    return {(a + b) / 2.0f, (b - a) / 2.0f};
}

}  // namespace opente::render
