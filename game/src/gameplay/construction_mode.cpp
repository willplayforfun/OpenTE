#include "gameplay/construction_mode.h"

namespace opente::gameplay {

void ConstructionMode::enter_building(const std::string& building_id) {
    selected_id_   = building_id;
    trail_markers_.clear();
    phase_         = ConstructionPhase::BuildingAiming;
}

void ConstructionMode::enter_trail(const std::string& path_id) {
    selected_id_   = path_id;
    trail_markers_.clear();
    phase_         = ConstructionPhase::TrailPlacing;
}

void ConstructionMode::on_mouse_move(int new_tx, int new_ty) noexcept {
    cursor_tx_ = new_tx;
    cursor_ty_ = new_ty;
}

bool ConstructionMode::on_left_click() {
    switch (phase_) {
        case ConstructionPhase::BuildingAiming:
            pinned_tx_ = cursor_tx_;
            pinned_ty_ = cursor_ty_;
            phase_     = ConstructionPhase::BuildingPinned;
            return false;
        case ConstructionPhase::BuildingPinned:
            phase_ = ConstructionPhase::None;
            selected_id_.clear();
            return true;
        case ConstructionPhase::TrailPlacing:
            trail_markers_.push_back({cursor_tx_, cursor_ty_});
            return false;
        default:
            return false;
    }
}

void ConstructionMode::on_right_click() {
    switch (phase_) {
        case ConstructionPhase::BuildingAiming:
            phase_ = ConstructionPhase::None;
            selected_id_.clear();
            break;
        case ConstructionPhase::BuildingPinned:
            phase_ = ConstructionPhase::BuildingAiming;
            break;
        case ConstructionPhase::TrailPlacing:
            if (!trail_markers_.empty())
                trail_markers_.pop_back();
            if (trail_markers_.empty()) {
                phase_ = ConstructionPhase::None;
                selected_id_.clear();
            }
            break;
        default:
            break;
    }
}

void ConstructionMode::exit() {
    phase_ = ConstructionPhase::None;
    selected_id_.clear();
    trail_markers_.clear();
}

}  // namespace opente::gameplay
