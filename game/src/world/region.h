#pragma once

// A loaded `game_data/maps/<id>.json` -- terrain grid plus static decorations/cities/regions. 
// See OpenTE/spec/world-and-maps.md "Map data".

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "world/map_data.h"

namespace opente::world {

/// Per-tile network connectivity mask -- the 6-byte record served by the
/// original engine's ConnectivityHashTable::Lookup (EXE 0x463a80). Byte
/// semantics per documentation/extracted/exe_trail_re_findings.md (§0
/// corrections are authoritative; some older tables in that doc still use
/// the first-pass byte names, which had trail/road AND rail/canal reversed).
///
/// Direction bits, clockwise from NW: 0x01=NW, 0x02=N, 0x04=NE, 0x08=E,
/// 0x10=SE, 0x20=S, 0x40=SW, 0x80=W. Trail/road/rail are CARDINAL-ONLY
/// (odd bits; a diagonal route is a staircase of cardinal segments). Canal
/// is the only 8-directional network: a diagonal canal connection is its
/// diagonal bit PLUS both flanking cardinal bits (NE = 0x04|0x02|0x08 =
/// 0x0e), because a diagonal canal run is a two-tile-wide BRAID -- the
/// tiles flanking each step's shared corner carry corner-bend channel
/// pieces (see exe_trail_re_findings.md §0 corrections 12-13).
struct TileConnectivity {
    std::uint8_t trail  = 0;    // byte 0: trail connections (low nibble of the decal LUT code)
    std::uint8_t road   = 0;    // byte 1: road connections (high nibble; a road replaces the
                                //         trail decal per-direction -- roads are the "upgrade")
    std::uint8_t rail   = 0;    // byte 2: rail connections (cardinal-only; shares the decal
                                //         LUT code space with trail/road -> level-crossing cells)
    std::uint8_t canal  = 0;    // byte 3: canal connections (8-directional, see encoding above;
                                //         canal endpoints at shore-water draw sea-mouth cells)
    std::uint8_t bridge = 0xff; // byte 4: 0xff = no bridge; any other value marks a bridge tile
                                //         AND is the crossing's DEPTH -- the averaged water
                                //         depth/level, used as the deck's elevation (the EXE
                                //         treats it exactly as an alti byte; see
                                //         GameplayScene::render_bridges). It SUPPRESSES the
                                //         tile's network decal: the bridge visual is a separate
                                //         sprite pass, not a Stage-D decal. 0 is a valid depth.
                                //         MUST default to 0xff -- a 0 default would flag every
                                //         tile as a bridge and hide all trail/road/rail decals.
    std::uint8_t bridge_aux = 0; // byte 5: the bridge's DIRECTION, `0x10 << cardinal_index`
                                 //         (0x10=N / 0x20=E / 0x40=S / 0x80=W; EXE table
                                 //         0x5fea00). Added to pack4(dirs) to form the deck
                                 //         sprite's variant id, and feeds the same suppression
                                 //         test as `bridge`.
};

/// Small terrain-type enum (world-and-maps.md).
/// Values are the bytes stored in `terrain.data` after RLE decoding -- see
/// `tools/extractor/maps/region.py`'s `_TERRAIN_TYPES` (must stay in sync).
///
/// DeepWater/ShallowWater are derived from the tile's texture page
/// (`mapp.terr` low nibble: page 1 = deep, page 2 = seas/shallow), so they
/// agree with the renderer's `texture_index <= 2` water test. Buildable vs
/// Impassable is still a coarse band placeholder (spec-deviations.md #8).
enum class TerrainType : std::uint8_t {
    DeepWater    = 0,
    ShallowWater = 1,
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
    /// Seeded at load from the map's authored `mapp.path`/`mapp.brid` arrays
    /// (see `maps/<id>.json` `connectivity`); tiles with no authored network
    /// keep the default mask (`bridge == 0xff`, all other bytes 0).
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
    std::vector<TileConnectivity> connectivity_;  // row-major, width_ * height_ (seeded from authored mapp.path/mapp.brid)
    std::uint8_t sea_level_ = 0;
    std::vector<MapRegion> regions_;
    std::vector<Decoration> decorations_;
    std::vector<City> cities_;
    std::vector<SpecialPoint> special_points_;
};

}  // namespace opente::world
