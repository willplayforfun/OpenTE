#pragma once

// Isometric tile <-> world-pixel projection. See
// OpenTE/spec/rendering.md "Isometric projection".

namespace opente::render {

/// Native tile pixel dimensions.
constexpr float kTileWidth = 64.0f;
constexpr float kTileHeight = 32.0f;

/// Source scalar for `kAltiToWorldHeight` (Runtime-adjustable via dev GUI).
inline float kAltiScaleFactor = 10.0f;

/// Converts a raw `mapp.alti` byte (0-255) to world-height units (0..9.96).
inline float kAltiToWorldHeight = kAltiScaleFactor / 256.0f;

/// Source scalar for `kPixelsPerAltiUnit` (Runtime-adjustable via dev GUI).
inline float kPixelsPerWorldHeightUnit = 45.25f;

/// World-pixels per raw `mapp.alti` byte unit, for converting a vertex's
/// heightmap byte into a vertical world-pixel offset (subtracted from its
/// projected Y so higher ground is displaced upward on screen, before the
/// camera's zoom is applied).
inline float kPixelsPerAltiUnit = kPixelsPerWorldHeightUnit * kAltiToWorldHeight;

/// 3D vector, used only for the slope-shading normal/light-direction math below.
struct Vec3 {
    float x = 0;
    float y = 0;
    float z = 0;
};

/// Lighting constants for per-vertex slope shading:

/// Height-difference multiplier for the 8 direction vectors used in the
/// cross-product normal computation: each neighbor's direction vector is
/// Vec3(dx, dy, (h_neighbor - h_center) * kSlopeGradientScale).
inline float kSlopeGradientScale = 1.6f;    // dialed in from 2.0f; [TMapView+0x238]  0x40400000

/// Fixed light direction (normalized). Computed in the EXE from elevation
/// = 0.85 rad and azimuth = 6.08 rad via sin/cos, then normalized.
constexpr Vec3 kLightDir = {0.7358256578f, 0.1516010314f, 0.6599830985f};

/// Per-channel ambient coefficients. Shadowed faces tinting.
/// Formula: byte = min(255, int(((1 - ambient) * max(0, dot) + ambient) * 2 * 255))
inline float kAmbientR = 0.116f;         // dialed in from 0.1369999945f; [TMapView+0x23c]  0x3E0C49BA
inline float kAmbientG = 0.0430000015f;  // [TMapView+0x240]  0x3D3020C5
inline float kAmbientB = 0.0f;           // [TMapView+0x244]  0x00000000

/// The original game uses D3DTOP_MODULATE2X (value 5) for the terrain base
/// pass, which doubles the result of (vertex_color × texture) at the D3D7
/// texture-stage level.
/// SDL2's SDL_RenderGeometry has no MODULATE2X, so terrain renders at ~70%
/// of the original's brightness on flat areas (dot≈0.66 → vertex ~180/255).
inline float kVertexColorScale = 1.2f; // dialed in

struct Vec2 {
    float x = 0;
    float y = 0;
};

/// Projects tile coordinates `(tx, ty)` to world pixel space (standard 2:1 "dimetric" projection).
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
