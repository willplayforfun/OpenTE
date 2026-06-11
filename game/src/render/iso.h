#pragma once

// Isometric tile <-> world-pixel projection. See
// OpenTE/spec/rendering.md "Isometric projection".

namespace opente::render {

/// Native tile pixel dimensions, confirmed from the original EXE's
/// `TMapView` projection constants (`0x5fc738` = 64.0, `0x5fc73c` = 32.0).
constexpr float kTileWidth = 64.0f;
constexpr float kTileHeight = 32.0f;

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
