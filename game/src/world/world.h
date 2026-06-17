#pragma once

// World: owns all map regions for the running episode.
// See OpenTE/spec/world-and-maps.md.

#include <string>
#include <vector>

#include "data/registry.h"
#include "world/region.h"

namespace opente::world {

class World {
public:
    /// Loads all map regions listed for `episode_id` in the registry.
    /// Throws `data::DataError` if the episode is unknown or any region map
    /// isn't found in the manifest.
    static World load_episode(const data::DataRegistry& registry,
                              const std::string& episode_id);

    int region_count() const { return static_cast<int>(regions_.size()); }
    const Region& region(int index) const { return regions_[index]; }
    const std::vector<Region>& regions() const { return regions_; }

private:
    std::vector<Region> regions_;
};

}  // namespace opente::world
