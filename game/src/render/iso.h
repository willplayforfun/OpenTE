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

/// Lighting constants for the per-vertex slope shading pass (B15 Rounds
/// 28/31-37: the original computes a per-vertex normal from neighboring
/// heights and a diffuse term against a fixed light direction). The
/// original's exact ambient/light constants weren't fully recovered (B15
/// "still open" item 2) -- these are reasonable stand-ins that produce the
/// same qualitative effect (flat ground evenly lit, slopes shaded by
/// orientation). Tune here if the in-game look needs adjusting.

/// "Spacing" term for the per-vertex normal's Z component -- larger values
/// make slopes look gentler (less dramatic shading contrast) for the same
/// height difference.
constexpr float kSlopeNormalZ = 2.0f;

/// Fixed light direction (already normalized), pointing down and slightly
/// from the north-west -- an arbitrary but typical isometric "sun" angle.
constexpr Vec3 kLightDir = {-0.3713906764f, -0.5570860146f, 0.7427813528f};

/// Minimum brightness multiplier for faces sloping fully away from the
/// light, so shadowed slopes stay readable rather than going to black.
constexpr float kAmbient = 0.55f;

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
