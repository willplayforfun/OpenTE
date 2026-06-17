#pragma once

// Loads `game_data/manifest.json` and `game_data/tables/*.json` into typed
// structs (data/types.h). See OpenTE/spec/data-model.md "Loader design
// (C++)".
//
// Stage 1 note: the spec's `load(search_paths)` signature supports mod
// overlays merged by id across multiple `game_data/`-shaped directories.
// That's not needed yet (no mod support in Stage 1), so this loads a single
// directory; extend to a search-path list + per-table overlay merge when
// mod support is implemented.

#include <filesystem>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

#include "data/types.h"

namespace opente::data {

struct SpriteEntry {
    std::string id;
    std::string file;
    int width = 0;
    int height = 0;
    int anchor_x = 0;
    int anchor_y = 0;
};

struct TableEntry {
    std::string id;
    std::string file;
};

struct MapEntry {
    std::string id;
    std::string file;
};

struct Manifest {
    std::vector<SpriteEntry> sprites;
    std::vector<TableEntry> tables;
    std::vector<MapEntry> maps;
    int format_version = 0;
};

/// Thrown by `DataRegistry::load` if `game_data_dir` doesn't contain a valid
/// `manifest.json`, or by an accessor when given an unknown id.
class DataError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

class DataRegistry {
public:
    /// Loads `manifest.json` and every table listed in it from
    /// `game_data_dir`. Throws `DataError` on missing/unparsable files.
    static DataRegistry load(const std::filesystem::path& game_data_dir);

    const std::filesystem::path& game_data_dir() const { return game_data_dir_; }
    const Manifest& manifest() const { return manifest_; }

    const Commodity& commodity(const std::string& id) const;
    const Building& building(const std::string& id) const;
    const Transporter& transporter(const std::string& id) const;
    const Bandit& bandit(const std::string& id) const;
    const Guard& guard(const std::string& id) const;
    const Ability& ability(int index) const;
    const Technology& technology(const std::string& id) const;
    const Episode& episode(const std::string& id) const;
    const Event& event(const std::string& id) const;

    /// Looks up a UI/localization string by its dotted key (e.g.
    /// "cons.labl.titl"), as exported from `text.{}` into the `strings` table.
    /// Returns `fallback` if the key is absent. Placeholder tokens like
    /// "[1 cost]" are left intact for the caller to substitute. (Returns by
    /// value: `fallback` is a parameter, so a reference to it would dangle.)
    std::string text(const std::string& key, const std::string& fallback = "") const;

    const std::map<std::string, Commodity>& commodities() const { return commodities_; }
    const std::map<std::string, Building>& buildings() const { return buildings_; }
    const std::map<std::string, Episode>& episodes() const { return episodes_; }
    const std::map<std::string, std::string>& strings() const { return strings_; }

private:
    std::filesystem::path game_data_dir_;
    Manifest manifest_;

    std::map<std::string, Commodity> commodities_;
    std::map<std::string, Building> buildings_;
    std::map<std::string, Transporter> transporters_;
    std::map<std::string, Bandit> bandits_;
    std::map<std::string, Guard> guards_;
    std::vector<Ability> abilities_;
    std::map<std::string, Technology> technologies_;
    std::map<std::string, Episode> episodes_;
    std::map<std::string, Event> events_;
    std::map<std::string, std::string> strings_;
};

}  // namespace opente::data
