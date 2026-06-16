#pragma once

#include <string>
#include <vector>

namespace opente::gameplay {

enum class ConstructionPhase {
    None,
    BuildingAiming,   // footprint follows cursor; no confirm yet
    BuildingPinned,   // footprint locked; confirm button active
    TrailPlacing,     // click-by-click waypoint placement
};

struct TrailMarker {
    int tx = 0;
    int ty = 0;
};

/// Tracks the active construction-mode state and cursor position.
/// Purely logical — no rendering, no SDL, no registry access.
class ConstructionMode {
public:
    ConstructionPhase phase()       const noexcept { return phase_; }
    const std::string& selected_id() const noexcept { return selected_id_; }
    bool is_active()                const noexcept { return phase_ != ConstructionPhase::None; }

    int cursor_tx() const noexcept { return cursor_tx_; }
    int cursor_ty() const noexcept { return cursor_ty_; }
    int pinned_tx() const noexcept { return pinned_tx_; }
    int pinned_ty() const noexcept { return pinned_ty_; }

    const std::vector<TrailMarker>& trail_markers() const noexcept { return trail_markers_; }

    // Enter building-placement mode. Phase: None → BuildingAiming.
    void enter_building(const std::string& building_id);

    // Enter trail-placement mode. Phase: None → TrailPlacing.
    void enter_trail(const std::string& path_id);

    // Update cursor tile (call on SDL_MOUSEMOTION).
    void on_mouse_move(int new_tx, int new_ty) noexcept;

    // Left click:
    //   BuildingAiming → BuildingPinned (locks preview)
    //   BuildingPinned → None           (confirms; returns true so caller can spawn)
    //   TrailPlacing   → adds waypoint marker; returns false
    bool on_left_click();

    // Right click:
    //   BuildingAiming → None (exit mode)
    //   BuildingPinned → BuildingAiming (unpin)
    //   TrailPlacing   → pop last marker; if empty → None
    void on_right_click();

    // Hard exit (ESC or "Exit" button).
    void exit();

private:
    ConstructionPhase        phase_       = ConstructionPhase::None;
    std::string              selected_id_;
    int                      cursor_tx_   = 0;
    int                      cursor_ty_   = 0;
    int                      pinned_tx_   = 0;
    int                      pinned_ty_   = 0;
    std::vector<TrailMarker> trail_markers_;
};

}  // namespace opente::gameplay
