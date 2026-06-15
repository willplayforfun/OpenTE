#pragma once

#include <filesystem>
#include <map>
#include <memory>
#include <string>

#include <SDL.h>

#include "render/font.h"

namespace opente::render {

/// Shared, lazily-populated cache of bitmap font atlases.
///
/// The original game selects a specific font per UI widget via a font-spec
/// string ("font/<family>/<pt>", e.g. "font/seri/9" for construction-panel
/// category rows). Those map 1:1 to the atlases the OpenTE extractor writes:
///   game_data/fonts/<family>_<pt>pt.json   (clea, seri, sans, cour).
///
/// A single FontCache is owned by UIManager and passed to every widget's
/// render(), so any dialog can request the exact face/size it needs without
/// loading or owning atlases itself. Atlases are loaded on first use and kept
/// for the cache's lifetime.
class FontCache {
public:
    /// `fonts_dir` is the `game_data/fonts` directory. Loading is deferred
    /// until the first get(); init only records where to look.
    void init(SDL_Renderer* renderer, std::filesystem::path fonts_dir);
    void clear();

    /// Returns the atlas for `family` (e.g. "seri") at `pt`, loading it on
    /// first request. Returns nullptr if the atlas file is missing or fails to
    /// load (a failed lookup is cached so it is attempted only once).
    BitmapFont* get(const std::string& family, int pt);

    /// Accepts a Trade Empires font spec: "font/clea/10", "clea/10", or
    /// "clea_10pt". Returns nullptr on parse failure / missing atlas.
    BitmapFont* get(const std::string& spec);

    /// Default UI font (Clean 12pt) for generic widgets that don't care.
    BitmapFont* ui() { return get("clea", 12); }

private:
    SDL_Renderer* renderer_ = nullptr;
    std::filesystem::path dir_;
    std::map<std::string, std::unique_ptr<BitmapFont>> cache_;  // key "family_pt"
};

}  // namespace opente::render
