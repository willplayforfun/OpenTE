#pragma once

// Typed structs for `game_data/tables/*.json`, populated via nlohmann::json
// `from_json` overloads. See `OpenTE/spec/data-model.md` ("Table catalog",
// "Loader design (C++)") for the schema these mirror.
//
// References to other tables (building ids, commodity ids, ...) are kept as
// plain `std::string` ids, not resolved pointers/indices -- see
// data-model.md's "Cross-reference resolution".
//
// `episodes.json`'s per-episode `commodities`/`demand`/`movement_costs` maps
// aren't needed for Stage 1 (static map rendering, no simulation) and are
// kept as raw `nlohmann::json` for now; give them typed structs once the
// simulation system needs them.

#include <nlohmann/json.hpp>

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "data/json_util.h"

namespace opente::data {

// --- commodities.json -------------------------------------------------

struct Commodity {
    std::string name;
    int base_price = 0;
};

inline void from_json(const nlohmann::json& j, Commodity& c) {
    c.name = json_string_or(j, "name", "");
    c.base_price = j.value("base", 0);
}

// --- buildings.json ------------------------------------------------------

struct Footprint {
    int width = 1;
    int height = 1;
};

inline void from_json(const nlohmann::json& j, Footprint& f) {
    f.width = j.value("width", 1);
    f.height = j.value("height", 1);
}

struct Building {
    std::string name;
    std::string desc;
    std::string culture_set;
    int build_cost = 0;
    Footprint footprint;
    std::string category;  // see json_to_string_any: source field is int or 4cc
    std::string type;
    bool military = false;
    bool religion = false;
    std::optional<std::string> look;
    std::string default_sprite;  // sprites.default
};

inline void from_json(const nlohmann::json& j, Building& b) {
    j.at("name").get_to(b.name);
    j.at("desc").get_to(b.desc);
    j.at("culture_set").get_to(b.culture_set);
    b.build_cost = j.value("build_cost", 0);
    j.at("footprint").get_to(b.footprint);
    b.category = json_to_string_any(j.at("category"));
    b.type = json_to_string_any(j.at("type"));
    b.military = j.value("military", false);
    b.religion = j.value("religion", false);
    if (j.at("look").is_null()) {
        b.look = std::nullopt;
    } else {
        b.look = j.at("look").get<std::string>();
    }
    b.default_sprite = j.at("sprites").value("default", "");
}

// --- transporters.json -----------------------------------------------------

struct TransporterSpeed {
    int none = 0;
    int road = 0;
    int trai = 0;
    int rail = 0;
    int cana = 0;
    int deep = 0;
    int dese = 0;
};

inline void from_json(const nlohmann::json& j, TransporterSpeed& s) {
    s.none = j.value("none", 0);
    s.road = j.value("road", 0);
    s.trai = j.value("trai", 0);
    s.rail = j.value("rail", 0);
    s.cana = j.value("cana", 0);
    s.deep = j.value("deep", 0);
    s.dese = j.value("dese", 0);
}

struct TransporterEpisodeStats {
    int capacity = 0;
    int cost = 0;
    std::string path_type;
    std::string tech;
    TransporterSpeed speed;
};

inline void from_json(const nlohmann::json& j, TransporterEpisodeStats& s) {
    s.capacity = j.value("capacity", 0);
    s.cost = j.value("cost", 0);
    s.path_type = j.value("path_type", "");
    s.tech = j.value("tech", "");
    j.at("speed").get_to(s.speed);
}

struct Transporter {
    std::string name;
    std::string depot_class;
    bool is_boat = false;
    std::map<std::string, TransporterEpisodeStats> by_episode;
};

inline void from_json(const nlohmann::json& j, Transporter& t) {
    j.at("name").get_to(t.name);
    t.depot_class = j.value("depot_class", "");
    t.is_boat = j.value("is_boat", false);
    t.by_episode.clear();
    for (const auto& [episode, stats] : j.at("by_episode").items()) {
        t.by_episode.emplace(episode, stats.get<TransporterEpisodeStats>());
    }
}

// --- bandits.json / guards.json --------------------------------------------

struct CombatStats {
    std::string name;
    std::string desc;
    int attack = 0;
    int defense = 0;
    int force = 0;
    int range = 0;
    int speed = 0;
    bool amphibious = false;
};

inline void from_json(const nlohmann::json& j, CombatStats& c) {
    c.name = j.value("name", "");
    c.desc = j.value("desc", "");
    c.attack = j.value("atta", 0);
    c.defense = j.value("defe", 0);
    c.force = j.value("forc", 0);
    c.range = j.value("rang", 0);
    c.speed = j.value("spee", 0);
    c.amphibious = j.value("amphibious", false);
}

using Bandit = CombatStats;

struct Guard {
    CombatStats combat;
    int cost = 0;
    std::string produced_by_building;
    std::vector<std::string> required_weapon;
};

inline void from_json(const nlohmann::json& j, Guard& g) {
    j.get_to(g.combat);
    g.cost = j.value("cost", 0);
    g.produced_by_building = json_string_or(j, "produced_by_building", "");
    g.required_weapon = json_to_string_list(j.at("required_weapon"));
}

// --- abilities.json (array, indexed 0-12) -----------------------------------

struct Ability {
    int id = 0;
    std::string name;
    std::string desc;
    int bonus = 0;
    int rank = 0;
};

inline void from_json(const nlohmann::json& j, Ability& a) {
    a.id = j.value("id", 0);
    a.name = j.value("name", "");
    a.desc = j.value("desc", "");
    a.bonus = j.value("bonus", 0);
    a.rank = j.value("rank", 0);
}

// --- technologies.json -------------------------------------------------------

struct TechnologyUnlocks {
    std::vector<std::string> buildings;
    std::vector<std::string> transporters;
    std::vector<std::string> pathways;
};

inline void from_json(const nlohmann::json& j, TechnologyUnlocks& u) {
    u.buildings = json_to_string_list_any(j.value("buildings", nlohmann::json::array()));
    u.transporters = json_to_string_list_any(j.value("transporters", nlohmann::json::array()));
    u.pathways = json_to_string_list_any(j.value("pathways", nlohmann::json::array()));
}

struct Technology {
    std::string name;
    std::string flavor_text;
    std::string episode;
    int cost = 0;
    int available_at_tick = 0;
    TechnologyUnlocks unlocks;
};

inline void from_json(const nlohmann::json& j, Technology& t) {
    t.name = j.value("name", "");
    t.flavor_text = j.value("flavor_text", "");
    t.episode = j.value("episode", "");
    t.cost = j.value("cost", 0);
    t.available_at_tick = j.value("available_at_tick", 0);
    j.at("unlocks").get_to(t.unlocks);
}

// --- episodes.json -------------------------------------------------------------

struct EpisodeRegion {
    std::string id;
    std::string name;
    std::string file;
    std::string music;
    int x = 0;
    int y = 0;
};

inline void from_json(const nlohmann::json& j, EpisodeRegion& r) {
    r.id = j.value("id", "");
    r.name = j.value("name", "");
    r.file = j.value("file", "");
    r.music = j.value("music", "");
    r.x = j.value("x", 0);
    r.y = j.value("y", 0);
}

struct Merchant {
    std::string name;
    int hire_cost = 0;
    int ability = 0;
};

inline void from_json(const nlohmann::json& j, Merchant& m) {
    m.name = json_string_or(j, "name", "");
    m.hire_cost = j.value("hire_cost", 0);
    m.ability = j.value("ability", 0);
}

struct Episode {
    std::string name;
    std::string desc;
    std::vector<EpisodeRegion> regions;
    std::vector<Merchant> merchants;
    std::vector<std::string> technologies;

    // Not yet needed for Stage 1 (static rendering, no simulation) -- raw
    // passthrough until the simulation system needs typed access.
    nlohmann::json commodities;
    nlohmann::json demand;
    nlohmann::json movement_costs;
};

inline void from_json(const nlohmann::json& j, Episode& e) {
    e.name = j.value("name", "");
    e.desc = j.value("desc", "");
    j.at("regions").get_to(e.regions);
    j.at("merchants").get_to(e.merchants);
    j.at("technologies").get_to(e.technologies);
    e.commodities = j.value("commodities", nlohmann::json::object());
    e.demand = j.value("demand", nlohmann::json::object());
    e.movement_costs = j.value("movement_costs", nlohmann::json::object());
}

// --- events.json -----------------------------------------------------------------

struct Event {
    std::string name;
    nlohmann::json params;
};

inline void from_json(const nlohmann::json& j, Event& e) {
    e.name = json_string_or(j, "name", "");
    e.params = j.value("params", nlohmann::json::object());
}

}  // namespace opente::data
