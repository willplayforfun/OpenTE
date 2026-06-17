#pragma once

// Owns and manages all terrain texture slots for one region/culture tileset.
// See OpenTE/spec/rendering.md "Terrain".

#include <SDL.h>

#include <array>
#include <filesystem>

#include "render/texture.h"

namespace opente::data { class DataRegistry; }

namespace opente::render {

class TerrainTileset {
public:
    /// Reads `tables/terrain_textures.json` from `game_data_dir`, selects the
    /// sub-table for `culture` (e.g. "chi1", "eur1"), and loads terrain page
    /// textures, the `tran` edge-blend atlas, shore-overlay atlases, edge skirt
    /// texture, and starfield background.  Falls back to the first available
    /// culture if `culture` is not found; warns and returns untextured if the
    /// file is missing or has no "cultures" key.
    static TerrainTileset load(SDL_Renderer* renderer,
                               const std::filesystem::path& game_data_dir,
                               const data::DataRegistry& registry,
                               const std::string& culture);

    /// Terrain page texture for slot `idx` (1–13); null if not loaded.
    SDL_Texture* page(int idx) const;

    /// Edge-blend atlas (`tran`); null if not loaded.
    SDL_Texture* tran() const;

    /// Shore-overlay atlas: `layer` 0 = `terrain.coa0`, 1 = `terrain.coa1`; null if not loaded.
    SDL_Texture* shore(int layer) const;

    /// Network-overlay atlas (Stage D): `layer` 0 = trail1 (0xf4), 1 = trail2 (0xf5),
    /// 2 = canal (0xf6), 3 = rail (0xfa); null if not loaded.
    SDL_Texture* network(int layer) const;

    /// Map-edge skirt texture (`terrain.edge`); null if not loaded.
    SDL_Texture* edge() const;

    /// Starfield background texture (`terrain.hidd`); null if not loaded.
    SDL_Texture* hidd() const;

private:
    std::array<Texture, 14> pages_;     // [0] unused, [1–13] terrain pages (2×2 tiled)
    Texture tran_;
    std::array<Texture, 2> shores_;     // [0]=coa0, [1]=coa1
    std::array<Texture, 4> networks_;   // [0]=trail1, [1]=trail2, [2]=canal, [3]=rail
    Texture edge_;
    Texture hidd_;
};

}  // namespace opente::render
