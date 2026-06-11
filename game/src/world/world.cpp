#include "world/world.h"

#include <algorithm>

namespace opente::world {

World World::load(const data::DataRegistry& registry, const std::string& map_id) {
    const auto& maps = registry.manifest().maps;
    const auto it = std::find_if(maps.begin(), maps.end(),
                                  [&](const data::MapEntry& m) { return m.id == map_id; });
    if (it == maps.end()) {
        throw data::DataError("unknown map id '" + map_id + "'");
    }

    World world;
    world.region_ = Region::load(registry.game_data_dir() / it->file);
    return world;
}

}  // namespace opente::world
