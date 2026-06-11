#pragma once

// A loaded `game_data/maps/<id>.json` -- terrain grid plus static
// decorations/cities/regions. See OpenTE/spec/world-and-maps.md "Map data".

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "world/map_data.h"

namespace opente::world {

/// The clone's own small terrain-type enum (world-and-maps.md). Values are
/// the bytes stored in `terrain.data` after RLE decoding -- see
/// `tools/extractor/maps/region.py`'s `_TERRAIN_TYPES` (must stay in sync).
enum class TerrainType : std::uint8_t {
    DeepWater = 0,
    ShallowWater = 1,
    Plains = 2,
    Hills = 3,
    Mountains = 4,
    Desert = 5,
    Forest = 6,
};

class Region {
public:
    /// Loads and decodes `path` (a `maps/<id>.json` file).
    static Region load(const std::filesystem::path& path);

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& episode() const { return episode_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// Returns the terrain type at tile `(tx, ty)`. `tx`/`ty` must be in
    /// `[0, width)` / `[0, height)`.
    TerrainType terrain_at(int tx, int ty) const { return terrain_[static_cast<std::size_t>(ty) * width_ + tx]; }

    const std::vector<MapRegion>& regions() const { return regions_; }
    const std::vector<Decoration>& decorations() const { return decorations_; }
    const std::vector<City>& cities() const { return cities_; }
    const std::vector<SpecialPoint>& special_points() const { return special_points_; }

private:
    std::string id_;
    std::string name_;
    std::string episode_;
    int width_ = 0;
    int height_ = 0;
    std::vector<TerrainType> terrain_;  // row-major, width_ * height_
    std::vector<MapRegion> regions_;
    std::vector<Decoration> decorations_;
    std::vector<City> cities_;
    std::vector<SpecialPoint> special_points_;
};

}  // namespace opente::world
