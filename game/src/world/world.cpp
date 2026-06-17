#include "world/world.h"

#include <algorithm>

namespace opente::world {

World World::load_episode(const data::DataRegistry& registry,
                          const std::string& episode_id) {
    const data::Episode& ep = registry.episode(episode_id);
    if (ep.regions.empty())
        throw data::DataError("episode '" + episode_id + "' has no regions");

    const auto& maps = registry.manifest().maps;
    World world;
    for (const data::EpisodeRegion& er : ep.regions) {
        const std::string map_id = episode_id + "_" + er.id;
        const auto it = std::find_if(maps.begin(), maps.end(),
                                     [&](const data::MapEntry& m) { return m.id == map_id; });
        if (it == maps.end())
            throw data::DataError("no manifest entry for map '" + map_id + "'");
        world.regions_.push_back(Region::load(registry.game_data_dir() / it->file));
    }
    return world;
}

}  // namespace opente::world
