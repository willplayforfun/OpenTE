#pragma once

// World: owns the loaded map region(s) for the running game.
// See OpenTE/spec/world-and-maps.md.

#include <string>

#include "data/registry.h"
#include "world/region.h"

namespace opente::world {

class World {
public:
    /// Loads the map listed in `registry`'s manifest under `map_id` (e.g.
    /// "ep01_chin"). Throws `data::DataError` if `map_id` isn't in the
    /// manifest, or `std::runtime_error` if the map file can't be parsed.
    static World load(const data::DataRegistry& registry, const std::string& map_id);

    const Region& region() const { return region_; }

private:
    Region region_;
};

}  // namespace opente::world
