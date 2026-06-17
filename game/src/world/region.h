#pragma once

// A loaded `game_data/maps/<id>.json` -- terrain grid plus static decorations/cities/regions. 
// See OpenTE/spec/world-and-maps.md "Map data".

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "world/map_data.h"

namespace opente::world {

/// Per-tile network connectivity mask decoded from the Stage D pass.
/// Matches the 6-byte structure produced by ConnectivityHashTable::Lookup
/// in the original EXE -- see documentation/extracted/exe_trail_re_findings.md.
struct TileConnectivity {
    std::uint8_t road       = 0;  // [ebp-0x20]: bits 0x02/0x08/0x20/0x80 = N/E/S/W road
    std::uint8_t trail_extra = 0; // [ebp-0x1f]: same bits, road upgraded to trail here
    std::uint8_t canal_dir  = 0;  // [ebp-0x1e]: cardinal canal directions (same bit positions)
    std::uint8_t rail       = 0;  // [ebp-0x1d]: opaque 8-bit rail connectivity key
    std::uint8_t canal      = 0;  // [ebp-0x1c]: 0 = canal tile, 0xff = no canal
    std::uint8_t reserved   = 0;  // [ebp-0x1b]: unknown/unused
};

/// Small terrain-type enum (world-and-maps.md).
/// Values are the bytes stored in `terrain.data` after RLE decoding -- see
/// `tools/extractor/maps/region.py`'s `_TERRAIN_TYPES` (must stay in sync).
///
/// ShallowWater is reserved; the band->type mapping for it is unresolved
/// (spec-deviations.md #8) and the extractor does not emit it yet.
enum class TerrainType : std::uint8_t {
    DeepWater    = 0,
    ShallowWater = 1,  // reserved -- not yet emitted by the extractor
    Buildable    = 2,
    Impassable   = 3,
};

class Region {
public:
    /// Loads and decodes `path` (a `maps/<id>.json` file).
    static Region load(const std::filesystem::path& path);

    const std::string& id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& episode() const { return episode_; }
    const std::string& culture_set() const { return culture_set_; }
    int width() const { return width_; }
    int height() const { return height_; }

    /// Returns the terrain type at tile `(tx, ty)`. `tx`/`ty` must be in `[0, width)` / `[0, height)`.
    TerrainType terrain_at(int tx, int ty) const { return terrain_[static_cast<std::size_t>(ty) * width_ + tx]; }

    /// Returns the raw `mapp.alti` byte at tile `(tx, ty)` (`tx`/`ty` must be in `[0, width)` / `[0, height)`). 
    /// Can be converted to world height units -- the renderer instead uses `render::kPixelsPerAltiUnit` to go straight from this byte to a screen-pixel offset.
    std::uint8_t height_at(int tx, int ty) const { return heightmap_[static_cast<std::size_t>(ty) * width_ + tx]; }

    /// The map's base `alti` value, used to position the starry backdrop and build the edge skirt.
    std::uint8_t sea_level() const { return sea_level_; }

    /// Returns the texture-page index (1-13) at tile `(tx, ty)`, indexing into `tables/terrain_textures.json`.
    /// `tx`/`ty` must be in `[0, width)` / `[0, height)`.
    /// Maps with no `texture_index` field default to 1 everywhere.
    std::uint8_t texture_index_at(int tx, int ty) const {
        return texture_index_[static_cast<std::size_t>(ty) * width_ + tx];
    }

    /// Returns the network connectivity mask at tile `(tx, ty)`.
    /// `tx`/`ty` must be in `[0, width)` / `[0, height)`.
    /// All bytes are zero until connectivity data is loaded from entity/save state.
    const TileConnectivity& connectivity_at(int tx, int ty) const {
        return connectivity_[static_cast<std::size_t>(ty) * width_ + tx];
    }

    const std::vector<MapRegion>& regions() const { return regions_; }
    const std::vector<Decoration>& decorations() const { return decorations_; }
    const std::vector<City>& cities() const { return cities_; }
    const std::vector<SpecialPoint>& special_points() const { return special_points_; }

private:
    std::string id_;
    std::string name_;
    std::string episode_;
    std::string culture_set_;
    int width_ = 0;
    int height_ = 0;
    std::vector<TerrainType> terrain_;  // row-major, width_ * height_
    std::vector<std::uint8_t> heightmap_;  // row-major, width_ * height_ (raw `mapp.alti` bytes)
    std::vector<std::uint8_t> texture_index_;     // row-major, width_ * height_, values 1-13
    std::vector<TileConnectivity> connectivity_;  // row-major, width_ * height_ (zero-initialized; populated by entity load)
    std::uint8_t sea_level_ = 0;
    std::vector<MapRegion> regions_;
    std::vector<Decoration> decorations_;
    std::vector<City> cities_;
    std::vector<SpecialPoint> special_points_;
};

}  // namespace opente::world
