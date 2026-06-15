#pragma once

// Typed structs mirroring `game_data/maps/<id>.json`, populated via
// nlohmann::json `from_json`. See OpenTE/spec/world-and-maps.md "Map data".
//
// `Region` (world/region.h) decodes `terrain` (base64-rle) into a flat
// per-tile grid; this header just mirrors the on-disk JSON shape.

#include <nlohmann/json.hpp>

#include <string>
#include <vector>

namespace opente::world {

struct Headquarters {
    double x = 0;
    double y = 0;
    int rotation = 0;
};

inline void from_json(const nlohmann::json& j, Headquarters& h) {
    h.x = j.value("x", 0.0);
    h.y = j.value("y", 0.0);
    h.rotation = j.value("rotation", 0);
}

struct MapRegion {
    std::string id;
    std::string culture_set;
    int starting_player = 0;
    Headquarters headquarters;
};

inline void from_json(const nlohmann::json& j, MapRegion& r) {
    r.id = j.value("id", "");
    r.culture_set = j.value("culture_set", "");
    r.starting_player = j.value("starting_player", 0);
    j.at("headquarters").get_to(r.headquarters);
}

struct Decoration {
    std::string type;
    std::string culture;
    int decoration_id = 0;
    std::string sprite;
    double x = 0;
    double y = 0;
};

inline void from_json(const nlohmann::json& j, Decoration& d) {
    d.type = j.value("type", "");
    d.culture = j.value("culture", "");
    d.decoration_id = j.value("decoration_id", 0);
    d.sprite = j.value("sprite", "");
    d.x = j.value("x", 0.0);
    d.y = j.value("y", 0.0);
}

struct City {
    std::string name;
    double x = 0;
    double y = 0;
    std::string culture_set;
};

inline void from_json(const nlohmann::json& j, City& c) {
    c.name = j.value("name", "");
    c.x = j.value("x", 0.0);
    c.y = j.value("y", 0.0);
    c.culture_set = j.value("culture_set", "");
}

struct SpecialPoint {
    std::string id;
    std::string kind;
    double x = 0;
    double y = 0;
    std::string culture;
};

inline void from_json(const nlohmann::json& j, SpecialPoint& p) {
    p.id = j.value("id", "");
    p.kind = j.value("kind", "");
    p.x = j.value("x", 0.0);
    p.y = j.value("y", 0.0);
    p.culture = j.value("culture", "");
}

struct TerrainData {
    std::string encoding;
    std::string data;  // base64, encoding-dependent (see region.h)
};

inline void from_json(const nlohmann::json& j, TerrainData& t) {
    t.encoding = j.value("encoding", "");
    t.data = j.value("data", "");
}

struct HeightmapData {
    std::string encoding;
    std::string data;  // base64, encoding-dependent (see region.h)
};

inline void from_json(const nlohmann::json& j, HeightmapData& h) {
    h.encoding = j.value("encoding", "");
    h.data = j.value("data", "");
}

/// Per-tile texture-page index grid (B15 Round 35/36, terrain-blending-plan.md
/// Stage A.1): same "base64-rle" convention as `TerrainData`, values 1-13
/// indexing into `tables/terrain_textures.json`. Optional -- older extracted
/// maps without this field fall back to texture-page 1 everywhere
/// (see region.cpp).
struct TextureIndexData {
    std::string encoding;
    std::string data;
};

inline void from_json(const nlohmann::json& j, TextureIndexData& t) {
    t.encoding = j.value("encoding", "");
    t.data = j.value("data", "");
}

/// Mirrors the full `maps/<id>.json` document.
struct MapFile {
    std::string id;
    std::string name;
    std::string episode;
    std::string culture_set;  // primary region's palette (e.g. "chi1", "eur1")
    int width = 0;
    int height = 0;
    TerrainData terrain;
    HeightmapData heightmap;
    TextureIndexData texture_index;
    std::vector<MapRegion> regions;
    std::vector<Decoration> decorations;
    std::vector<City> cities;
    std::vector<SpecialPoint> special_points;
};

inline void from_json(const nlohmann::json& j, MapFile& m) {
    m.id = j.value("id", "");
    m.name = j.value("name", "");
    m.episode = j.value("episode", "");
    m.culture_set = j.value("culture_set", "");
    m.width = j.value("width", 0);
    m.height = j.value("height", 0);
    j.at("terrain").get_to(m.terrain);
    if (j.contains("heightmap")) {
        j.at("heightmap").get_to(m.heightmap);
    }
    if (j.contains("texture_index")) {
        j.at("texture_index").get_to(m.texture_index);
    }
    m.regions = j.value("regions", std::vector<MapRegion>{});
    m.decorations = j.value("decorations", std::vector<Decoration>{});
    m.cities = j.value("cities", std::vector<City>{});
    m.special_points = j.value("special_points", std::vector<SpecialPoint>{});
}

}  // namespace opente::world
