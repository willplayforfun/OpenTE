#include "world/region.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <stdexcept>

namespace opente::world {

namespace {

/// Decodes standard base64 (RFC 4648, with '=' padding) to raw bytes.
std::vector<std::uint8_t> base64_decode(const std::string& input) {
    static constexpr std::array<int, 256> table = [] {
        std::array<int, 256> t{};
        t.fill(-1);
        const char* alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (int i = 0; i < 64; ++i) {
            t[static_cast<unsigned char>(alphabet[i])] = i;
        }
        return t;
    }();

    std::vector<std::uint8_t> out;
    out.reserve(input.size() / 4 * 3);

    int buffer = 0;
    int bits = 0;
    for (char c : input) {
        if (c == '=' || c == '\n' || c == '\r') {
            continue;
        }
        const int value = table[static_cast<unsigned char>(c)];
        if (value < 0) {
            continue;
        }
        buffer = (buffer << 6) | value;
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out.push_back(static_cast<std::uint8_t>((buffer >> bits) & 0xFF));
        }
    }
    return out;
}

/// Decodes the "base64-rle" format defined by
/// `tools/extractor/maps/region.py`'s `_encode_terrain_rle`: a flat sequence
/// of `(type_byte: uint8, run_length: uint16 LE)` pairs covering the
/// row-major `width*height` grid. Used by both `terrain.data` and
/// `texture_index.data` (terrain-blending-plan.md Stage A.1).
std::vector<std::uint8_t> decode_byte_rle(const std::string& base64_data, int width, int height) {
    const std::vector<std::uint8_t> bytes = base64_decode(base64_data);
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    std::vector<std::uint8_t> out;
    out.reserve(total);

    for (std::size_t i = 0; i + 3 <= bytes.size() && out.size() < total; i += 3) {
        const std::uint8_t value = bytes[i];
        const int run = bytes[i + 1] | (bytes[i + 2] << 8);
        const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(run), total - out.size());
        out.insert(out.end(), count, value);
    }

    if (out.size() != total) {
        throw std::runtime_error("decoded RLE grid size (" + std::to_string(out.size()) +
                                  ") != width*height (" + std::to_string(total) + ")");
    }
    return out;
}

std::vector<TerrainType> decode_terrain_rle(const std::string& base64_data, int width, int height) {
    const std::vector<std::uint8_t> bytes = decode_byte_rle(base64_data, width, height);
    std::vector<TerrainType> terrain;
    terrain.reserve(bytes.size());
    for (std::uint8_t b : bytes) {
        terrain.push_back(static_cast<TerrainType>(b));
    }
    return terrain;
}

/// Decodes `texture_index.data` (Stage A.1), clamping to the valid
/// `terrain_textures.json` index range `[1, 13]` (per B15 Round 35, a
/// `mapp.terr & 0xf` value of 0 or >13 shouldn't occur but isn't exhaustively
/// verified -- clamp rather than crash). Returns an all-`1` grid if
/// `base64_data` is empty (older extracted maps without this field).
std::vector<std::uint8_t> decode_texture_index_rle(const std::string& base64_data, int width, int height) {
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (base64_data.empty()) {
        return std::vector<std::uint8_t>(total, 1);
    }
    std::vector<std::uint8_t> indices = decode_byte_rle(base64_data, width, height);
    for (std::uint8_t& index : indices) {
        index = static_cast<std::uint8_t>(std::clamp<int>(index, 1, 13));
    }
    return indices;
}

/// Decodes the `heightmap.data` "raw-base64" format defined by
/// `tools/extractor/maps/region.py`: the row-major `mapp.alti` byte grid,
/// base64-encoded verbatim. Returns an all-zero grid if `base64_data` is
/// empty (older extracted maps without a `heightmap` field).
std::vector<std::uint8_t> decode_heightmap(const std::string& base64_data, int width, int height) {
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (base64_data.empty()) {
        return std::vector<std::uint8_t>(total, 0);
    }

    std::vector<std::uint8_t> heights = base64_decode(base64_data);
    if (heights.size() != total) {
        throw std::runtime_error("decoded heightmap grid size (" + std::to_string(heights.size()) +
                                  ") != width*height (" + std::to_string(total) + ")");
    }
    return heights;
}

}  // namespace

Region Region::load(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("could not open '" + path.string() + "'");
    }
    nlohmann::json j;
    file >> j;
    const MapFile map = j.get<MapFile>();

    Region region;
    region.id_ = map.id;
    region.name_ = map.name;
    region.episode_ = map.episode;
    region.width_ = map.width;
    region.height_ = map.height;
    region.terrain_ = decode_terrain_rle(map.terrain.data, map.width, map.height);
    region.heightmap_ = decode_heightmap(map.heightmap.data, map.width, map.height);
    region.texture_index_ = decode_texture_index_rle(map.texture_index.data, map.width, map.height);
    region.sea_level_ = region.heightmap_.empty()
                             ? 0
                             : *std::min_element(region.heightmap_.begin(), region.heightmap_.end());
    region.regions_ = map.regions;
    region.decorations_ = map.decorations;
    region.cities_ = map.cities;
    region.special_points_ = map.special_points;
    return region;
}

}  // namespace opente::world
