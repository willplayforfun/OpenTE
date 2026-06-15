#pragma once

// Camera: pan/zoom state for the world viewport. See OpenTE/spec/rendering.md "Camera". 
// Per-viewport state, not part of World/Region.

#include "render/iso.h"

namespace opente::render {

struct Camera {
    /// Top-left of the viewport, in world pixel space.
    Vec2 world_pixel_offset;

    /// Zoom factor; clamped to `[kMinZoom, kMaxZoom]` by `set_zoom`.
    float zoom = 1.0f;

    static constexpr float kMinZoom = 0.5f;
    static constexpr float kMaxZoom = 2.0f;

    void set_zoom(float new_zoom) {
        zoom = new_zoom < kMinZoom ? kMinZoom : (new_zoom > kMaxZoom ? kMaxZoom : new_zoom);
    }

    /// Converts a world pixel position to a screen pixel position.
    Vec2 world_to_screen(Vec2 world) const {
        return {(world.x - world_pixel_offset.x) * zoom, (world.y - world_pixel_offset.y) * zoom};
    }
};

}  // namespace opente::render
