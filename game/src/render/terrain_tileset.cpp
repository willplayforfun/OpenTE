#include "render/terrain_tileset.h"

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <SDL_image.h>
#include <nlohmann/json.hpp>

#include "data/registry.h"

namespace opente::render {

namespace {

constexpr const char* kTerrainTexturesTablePath = "tables/terrain_textures.json";
constexpr const char* kShoreAtlasSpriteIds[2] = {"terrain.coa0", "terrain.coa1"};
constexpr const char* kEdgeSpriteId = "terrain.edge";
constexpr const char* kHiddSpriteId = "terrain.hidd";

/// Returns a 2×-wide + 2×-tall static texture tiling `path`'s image in a 2×2
/// grid. CPU surface blitting produces SDL_TEXTUREACCESS_STATIC, which
/// survives window resizes (unlike an SDL_TEXTUREACCESS_TARGET render target,
/// whose contents are lost when D3D resets the swap chain on resize).
Texture make_tiled(SDL_Renderer* renderer, const std::filesystem::path& path) {
    SDL_Surface* src = IMG_Load(path.string().c_str());
    if (!src) return Texture{};

    SDL_Surface* src32 = SDL_ConvertSurfaceFormat(src, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(src);
    if (!src32) return Texture{};

    const int w = src32->w;
    const int h = src32->h;
    SDL_Surface* tiled = SDL_CreateRGBSurfaceWithFormat(0, w * 2, h * 2, 32,
                                                        SDL_PIXELFORMAT_RGBA8888);
    if (!tiled) {
        SDL_FreeSurface(src32);
        return Texture{};
    }

    SDL_SetSurfaceBlendMode(src32, SDL_BLENDMODE_NONE);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            SDL_Rect dst = {col * w, row * h, w, h};
            SDL_BlitSurface(src32, nullptr, tiled, &dst);
        }
    }
    SDL_FreeSurface(src32);

    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, tiled);
    SDL_FreeSurface(tiled);
    return tex ? Texture::from_raw(tex) : Texture{};
}

}  // namespace

TerrainTileset TerrainTileset::load(SDL_Renderer* renderer,
                                    const std::filesystem::path& game_data_dir,
                                    const data::DataRegistry& registry,
                                    const std::string& culture) {
    TerrainTileset ts;

    const std::filesystem::path table_path = game_data_dir / kTerrainTexturesTablePath;
    std::ifstream file(table_path);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "No '%s' found -- terrain will render untextured.",
                    table_path.string().c_str());
        return ts;
    }

    nlohmann::json j;
    file >> j;

    if (!j.contains("cultures")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "'terrain_textures.json' has no 'cultures' key -- "
                    "re-run the extractor. Terrain will render untextured.");
        return ts;
    }

    const nlohmann::json& cultures = j["cultures"];
    if (cultures.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "'terrain_textures.json' cultures table is empty.");
        return ts;
    }

    // Select the requested culture, falling back to the first available one.
    std::string eff_culture = culture;
    if (!cultures.contains(culture)) {
        eff_culture = cultures.begin().key();
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "Culture '%s' not found in terrain_textures.json -- "
                    "falling back to '%s'.",
                    culture.c_str(), eff_culture.c_str());
    }

    const nlohmann::json& cj = cultures.at(eff_culture);
    const std::vector<std::string> pages = cj.value("pages", std::vector<std::string>{});

    auto find_sprite_file = [&](const std::string& sprite_id)
        -> std::optional<std::filesystem::path> {
        for (const data::SpriteEntry& sprite : registry.manifest().sprites) {
            if (sprite.id == sprite_id) {
                return game_data_dir / sprite.file;
            }
        }
        return std::nullopt;
    };

    // pages[0] is an empty sentinel; pages[N] (N=1..13) holds the sprite id
    // for slot N -- so use i directly as the slot index, not i+1.
    for (std::size_t i = 1; i < pages.size() && i < ts.pages_.size(); ++i) {
        if (pages[i].empty()) continue;
        if (const auto path = find_sprite_file(pages[i])) {
            ts.pages_[i] = make_tiled(renderer, *path);
            if (ts.pages_[i].valid()) {
                // Terrain tiles are fully opaque; BLENDMODE_NONE avoids SDL2
                // auto-applying BLEND from the PNG alpha channel.
                SDL_SetTextureBlendMode(ts.pages_[i].handle(), SDL_BLENDMODE_NONE);
            }
        }
    }

    for (std::size_t i = 0; i < ts.shores_.size(); ++i) {
        if (const auto path = find_sprite_file(kShoreAtlasSpriteIds[i])) {
            ts.shores_[i] = Texture::load(renderer, *path);
            if (ts.shores_[i].valid()) {
                SDL_SetTextureBlendMode(ts.shores_[i].handle(), SDL_BLENDMODE_BLEND);
            }
        }
    }

    const std::string tran_sprite_id = cj.value("tran", std::string{});
    if (!tran_sprite_id.empty()) {
        if (const auto path = find_sprite_file(tran_sprite_id)) {
            ts.tran_ = Texture::load(renderer, *path);
            if (ts.tran_.valid()) {
                SDL_SetTextureBlendMode(ts.tran_.handle(), SDL_BLENDMODE_BLEND);
            }
        }
    }

    if (const auto path = find_sprite_file(kEdgeSpriteId)) {
        ts.edge_ = Texture::load(renderer, *path);
        if (ts.edge_.valid()) {
            SDL_SetTextureBlendMode(ts.edge_.handle(), SDL_BLENDMODE_BLEND);
        }
    }

    const std::string hidd_sprite_id = cj.value("hidd", std::string{});
    if (!hidd_sprite_id.empty()) {
        if (const auto path = find_sprite_file(hidd_sprite_id)) {
            ts.hidd_ = make_tiled(renderer, *path);
            if (ts.hidd_.valid()) {
                SDL_SetTextureBlendMode(ts.hidd_.handle(), SDL_BLENDMODE_NONE);
            }
        }
    }

    return ts;
}

SDL_Texture* TerrainTileset::page(int idx) const {
    if (idx < 0 || idx >= static_cast<int>(pages_.size())) return nullptr;
    return pages_[idx].valid() ? pages_[idx].handle() : nullptr;
}

SDL_Texture* TerrainTileset::tran() const {
    return tran_.valid() ? tran_.handle() : nullptr;
}

SDL_Texture* TerrainTileset::shore(int layer) const {
    if (layer < 0 || layer >= static_cast<int>(shores_.size())) return nullptr;
    return shores_[layer].valid() ? shores_[layer].handle() : nullptr;
}

SDL_Texture* TerrainTileset::edge() const {
    return edge_.valid() ? edge_.handle() : nullptr;
}

SDL_Texture* TerrainTileset::hidd() const {
    return hidd_.valid() ? hidd_.handle() : nullptr;
}

}  // namespace opente::render
