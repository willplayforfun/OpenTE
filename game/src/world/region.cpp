#include "world/region.h"

#include <nlohmann/json.hpp>

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

/// Decodes the `terrain.data` "base64-rle" format defined by
/// `tools/extractor/maps/region.py`'s `_encode_terrain_rle`: a flat sequence
/// of `(type_byte: uint8, run_length: uint16 LE)` pairs covering the
/// row-major `width*height` grid.
std::vector<TerrainType> decode_terrain_rle(const std::string& base64_data, int width, int height) {
    const std::vector<std::uint8_t> bytes = base64_decode(base64_data);
    const std::size_t total = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);

    std::vector<TerrainType> terrain;
    terrain.reserve(total);

    for (std::size_t i = 0; i + 3 <= bytes.size() && terrain.size() < total; i += 3) {
        const auto type = static_cast<TerrainType>(bytes[i]);
        const int run = bytes[i + 1] | (bytes[i + 2] << 8);
        const std::size_t count = std::min<std::size_t>(static_cast<std::size_t>(run), total - terrain.size());
        terrain.insert(terrain.end(), count, type);
    }

    if (terrain.size() != total) {
        throw std::runtime_error("decoded terrain grid size (" + std::to_string(terrain.size()) +
                                  ") != width*height (" + std::to_string(total) + ")");
    }
    return terrain;
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
    region.regions_ = map.regions;
    region.decorations_ = map.decorations;
    region.cities_ = map.cities;
    region.special_points_ = map.special_points;
    return region;
}

}  // namespace opente::world
