#include "render/terrain_tileset.h"

#include <fstream>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "data/registry.h"

namespace opente::render {

namespace {

constexpr const char* kTerrainTexturesTablePath = "tables/terrain_textures.json";
constexpr const char* kShoreAtlasSpriteIds[2] = {"terrain.coa0", "terrain.coa1"};
constexpr const char* kEdgeSpriteId = "terrain.edge";
constexpr const char* kHiddSpriteId = "terrain.hidd";

/// Returns a 2× wide + 2× tall render-target texture tiling `src` in a 2×2
/// grid. The terrain UV formulas index this wider range so that UVs slightly
/// outside [0,1] land in the adjacent copy instead of clamping.
Texture make_tiled(SDL_Renderer* renderer, const Texture& src) {
    if (!src.valid()) return Texture{};
    const int w = src.width();
    const int h = src.height();

    Uint32 fmt = SDL_PIXELFORMAT_RGBA8888;
    SDL_QueryTexture(src.handle(), &fmt, nullptr, nullptr, nullptr);

    SDL_Texture* tiled = SDL_CreateTexture(renderer, fmt,
                                           SDL_TEXTUREACCESS_TARGET, w * 2, h * 2);
    if (tiled == nullptr) return Texture{};

    // Temporarily use BLENDMODE_NONE on the source so alpha is copied
    // directly (not pre-multiplied into RGB by alpha-blending).
    SDL_BlendMode orig_blend;
    SDL_GetTextureBlendMode(src.handle(), &orig_blend);
    SDL_SetTextureBlendMode(src.handle(), SDL_BLENDMODE_NONE);

    SDL_SetRenderTarget(renderer, tiled);
    for (int row = 0; row < 2; ++row) {
        for (int col = 0; col < 2; ++col) {
            const SDL_Rect dst = {col * w, row * h, w, h};
            SDL_RenderCopy(renderer, src.handle(), nullptr, &dst);
        }
    }
    SDL_SetRenderTarget(renderer, nullptr);

    SDL_SetTextureBlendMode(src.handle(), orig_blend);
    return Texture::from_raw(tiled);
}

}  // namespace

TerrainTileset TerrainTileset::load(SDL_Renderer* renderer,
                                    const std::filesystem::path& game_data_dir,
                                    const data::DataRegistry& registry) {
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
    const std::vector<std::string> pages = j.value("pages", std::vector<std::string>{});

    auto find_sprite_file = [&](const std::string& sprite_id)
        -> std::optional<std::filesystem::path> {
        for (const data::SpriteEntry& sprite : registry.manifest().sprites) {
            if (sprite.id == sprite_id) {
                return game_data_dir / sprite.file;
            }
        }
        return std::nullopt;
    };

    for (std::size_t i = 0; i < pages.size() && i < ts.pages_.size() - 1; ++i) {
        if (pages[i].empty()) continue;
        if (const auto path = find_sprite_file(pages[i])) {
            const Texture src = Texture::load(renderer, *path);
            ts.pages_[i + 1] = make_tiled(renderer, src);
            if (ts.pages_[i + 1].valid()) {
                // Terrain tiles are fully opaque; BLENDMODE_NONE avoids SDL2
                // auto-applying BLEND from the PNG alpha channel.
                SDL_SetTextureBlendMode(ts.pages_[i + 1].handle(), SDL_BLENDMODE_NONE);
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

    const std::string tran_sprite_id = j.value("tran", std::string{});
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

    const std::string hidd_sprite_id = j.value("hidd", std::string{});
    if (!hidd_sprite_id.empty()) {
        if (const auto path = find_sprite_file(hidd_sprite_id)) {
            const Texture src = Texture::load(renderer, *path);
            ts.hidd_ = make_tiled(renderer, src);
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
