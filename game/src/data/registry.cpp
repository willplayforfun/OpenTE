#include "data/registry.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace opente::data {

// Note: these must be plain functions in `opente::data` (not nested in an
// anonymous namespace) so that ADL finds them when nlohmann::json
// instantiates `j.value<std::vector<SpriteEntry>>(...)` etc. for `Manifest`
// below -- MSVC's two-phase lookup doesn't see into nested unnamed
// namespaces for that.
void from_json(const nlohmann::json& j, SpriteEntry& s) {
    s.id = j.value("id", "");
    s.file = j.value("file", "");
    s.width = j.value("width", 0);
    s.height = j.value("height", 0);
    s.anchor_x = j.value("anchor_x", 0);
    s.anchor_y = j.value("anchor_y", 0);
}

void from_json(const nlohmann::json& j, TableEntry& t) {
    t.id = j.value("id", "");
    t.file = j.value("file", "");
}

void from_json(const nlohmann::json& j, MapEntry& m) {
    m.id = j.value("id", "");
    m.file = j.value("file", "");
}

void from_json(const nlohmann::json& j, Manifest& m) {
    m.sprites = j.value("sprites", std::vector<SpriteEntry>{});
    m.tables = j.value("tables", std::vector<TableEntry>{});
    m.maps = j.value("maps", std::vector<MapEntry>{});
    m.format_version = j.value("format_version", 0);
}

namespace {

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path);
    if (!file) {
        throw DataError("could not open '" + path.string() + "'");
    }
    nlohmann::json j;
    try {
        file >> j;
    } catch (const std::exception& e) {
        throw DataError("failed to parse '" + path.string() + "': " + e.what());
    }
    return j;
}

}  // namespace

DataRegistry DataRegistry::load(const std::filesystem::path& game_data_dir) {
    DataRegistry registry;
    registry.game_data_dir_ = game_data_dir;

    const std::filesystem::path manifest_path = game_data_dir / "manifest.json";
    registry.manifest_ = read_json(manifest_path).get<Manifest>();

    for (const TableEntry& entry : registry.manifest_.tables) {
        const nlohmann::json table = read_json(game_data_dir / entry.file);

        if (entry.id == "commodities") {
            registry.commodities_ = table.get<std::map<std::string, Commodity>>();
        } else if (entry.id == "buildings") {
            registry.buildings_ = table.get<std::map<std::string, Building>>();
        } else if (entry.id == "transporters") {
            registry.transporters_ = table.get<std::map<std::string, Transporter>>();
        } else if (entry.id == "bandits") {
            registry.bandits_ = table.get<std::map<std::string, Bandit>>();
        } else if (entry.id == "guards") {
            registry.guards_ = table.get<std::map<std::string, Guard>>();
        } else if (entry.id == "abilities") {
            registry.abilities_ = table.get<std::vector<Ability>>();
        } else if (entry.id == "technologies") {
            registry.technologies_ = table.get<std::map<std::string, Technology>>();
        } else if (entry.id == "episodes") {
            registry.episodes_ = table.get<std::map<std::string, Episode>>();
        } else if (entry.id == "events") {
            registry.events_ = table.get<std::map<std::string, Event>>();
        }
        // Unknown table ids (e.g. future tables not yet given a typed
        // struct) are simply skipped.
    }

    return registry;
}

namespace {

template <typename T>
const T& lookup(const std::map<std::string, T>& table, const std::string& id, const char* table_name) {
    auto it = table.find(id);
    if (it == table.end()) {
        throw DataError(std::string("unknown ") + table_name + " id '" + id + "'");
    }
    return it->second;
}

}  // namespace

const Commodity& DataRegistry::commodity(const std::string& id) const {
    return lookup(commodities_, id, "commodity");
}

const Building& DataRegistry::building(const std::string& id) const {
    return lookup(buildings_, id, "building");
}

const Transporter& DataRegistry::transporter(const std::string& id) const {
    return lookup(transporters_, id, "transporter");
}

const Bandit& DataRegistry::bandit(const std::string& id) const {
    return lookup(bandits_, id, "bandit");
}

const Guard& DataRegistry::guard(const std::string& id) const {
    return lookup(guards_, id, "guard");
}

const Ability& DataRegistry::ability(int index) const {
    if (index < 0 || static_cast<std::size_t>(index) >= abilities_.size()) {
        throw DataError("unknown ability index " + std::to_string(index));
    }
    return abilities_[static_cast<std::size_t>(index)];
}

const Technology& DataRegistry::technology(const std::string& id) const {
    return lookup(technologies_, id, "technology");
}

const Episode& DataRegistry::episode(const std::string& id) const {
    return lookup(episodes_, id, "episode");
}

const Event& DataRegistry::event(const std::string& id) const {
    return lookup(events_, id, "event");
}

}  // namespace opente::data
