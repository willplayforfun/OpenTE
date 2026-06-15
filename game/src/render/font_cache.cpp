#include "render/font_cache.h"

#include <cstdlib>

#include <SDL_log.h>

namespace opente::render {

void FontCache::init(SDL_Renderer* renderer, std::filesystem::path fonts_dir) {
    renderer_ = renderer;
    dir_      = std::move(fonts_dir);
}

void FontCache::clear() {
    cache_.clear();
}

BitmapFont* FontCache::get(const std::string& family, int pt) {
    std::string key = family + "_" + std::to_string(pt);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.get();

    std::filesystem::path path = dir_ / (key + "pt.json");
    std::unique_ptr<BitmapFont> font = renderer_
        ? BitmapFont::load(renderer_, path)
        : nullptr;
    if (!font) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "FontCache: could not load font atlas '%s'",
                    path.string().c_str());
    }
    BitmapFont* raw = font.get();
    cache_.emplace(std::move(key), std::move(font));  // cache nullptr on failure
    return raw;
}

BitmapFont* FontCache::get(const std::string& spec) {
    // Accept "font/<fam>/<pt>", "<fam>/<pt>", or "<fam>_<pt>pt".
    std::string fam;
    int pt = 0;

    const auto last_slash = spec.rfind('/');
    if (last_slash != std::string::npos) {
        pt = std::atoi(spec.c_str() + last_slash + 1);
        const auto prev_slash = spec.rfind('/', last_slash - 1);
        const auto fam_start = (prev_slash == std::string::npos) ? 0 : prev_slash + 1;
        fam = spec.substr(fam_start, last_slash - fam_start);
    } else {
        const auto us = spec.find('_');
        if (us != std::string::npos) {
            fam = spec.substr(0, us);
            pt  = std::atoi(spec.c_str() + us + 1);
        }
    }

    if (fam.empty() || pt <= 0) return nullptr;
    return get(fam, pt);
}

}  // namespace opente::render
