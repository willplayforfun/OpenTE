#include "render/font.h"

#include <SDL_image.h>
#include <SDL_log.h>

#include <fstream>

#include <nlohmann/json.hpp>

namespace opente::render {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

namespace {

// Decode the next UTF-8 codepoint from `s`, advancing `s` past it.
// Returns 0xFFFD on encoding error; returns 0 at end-of-string.
uint32_t next_codepoint(const char*& s) {
    const auto b0 = static_cast<uint8_t>(*s);
    if (b0 == 0) return 0;
    ++s;

    if (b0 < 0x80) return b0;

    // Determine byte count and initial bits.
    int extra = 0;
    uint32_t cp = 0;
    if ((b0 & 0xE0) == 0xC0)      { extra = 1; cp = b0 & 0x1F; }
    else if ((b0 & 0xF0) == 0xE0) { extra = 2; cp = b0 & 0x0F; }
    else if ((b0 & 0xF8) == 0xF0) { extra = 3; cp = b0 & 0x07; }
    else return 0xFFFD;

    for (int i = 0; i < extra; ++i) {
        const auto b = static_cast<uint8_t>(*s);
        if ((b & 0xC0) != 0x80) return 0xFFFD;
        cp = (cp << 6) | (b & 0x3F);
        ++s;
    }
    return cp;
}

}  // namespace

// ---------------------------------------------------------------------------
// BitmapFont
// ---------------------------------------------------------------------------

BitmapFont::~BitmapFont() {
    if (atlas_) SDL_DestroyTexture(atlas_);
}

int BitmapFont::codepoint_to_glyph(uint32_t cp) const noexcept {
    if (cp >= cmap_.size()) return 0;
    const int gi = cmap_[cp];
    return (gi < static_cast<int>(glyphs_.size())) ? gi : 0;
}

std::unique_ptr<BitmapFont> BitmapFont::load(SDL_Renderer* renderer,
                                              const std::filesystem::path& json_path) {
    // Read JSON.
    std::ifstream ifs(json_path);
    if (!ifs) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BitmapFont: cannot open '%s'", json_path.string().c_str());
        return nullptr;
    }

    nlohmann::json j;
    try {
        ifs >> j;
    } catch (const std::exception& e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BitmapFont: JSON parse error in '%s': %s",
                     json_path.string().c_str(), e.what());
        return nullptr;
    }

    // Build atlas texture from the companion PNG.
    const std::string stem = json_path.stem().string();
    const std::filesystem::path png_path = json_path.parent_path() / (stem + "_atlas.png");
    SDL_Surface* surf = IMG_Load(png_path.string().c_str());
    if (!surf) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BitmapFont: IMG_Load('%s') failed: %s",
                     png_path.string().c_str(), IMG_GetError());
        return nullptr;
    }
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, surf);
    SDL_FreeSurface(surf);
    if (!tex) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                     "BitmapFont: SDL_CreateTextureFromSurface failed: %s",
                     SDL_GetError());
        return nullptr;
    }
    SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);

    // Populate font.
    auto font = std::unique_ptr<BitmapFont>(new BitmapFont());
    font->atlas_ = tex;

    const auto& m = j.at("metrics");
    font->ascender_   = m.at("ascender").get<int>();
    font->descender_  = m.at("descender").get<int>();
    font->line_height_ = m.at("line_height").get<int>();

    // Dense cmap.
    const auto& jcmap = j.at("cmap");
    font->cmap_.reserve(jcmap.size());
    for (const auto& v : jcmap)
        font->cmap_.push_back(v.get<uint16_t>());

    // Glyph table.
    const auto& jglyphs = j.at("glyphs");
    font->glyphs_.reserve(jglyphs.size());
    for (const auto& g : jglyphs) {
        GlyphInfo gi{};
        gi.atlas_x = g.at("atlas_x").get<int>();
        gi.atlas_y = g.at("atlas_y").get<int>();
        gi.slot_w  = g.at("slot_w").get<int>();
        gi.ink_h   = g.at("ink_h").get<int>();
        gi.advance = g.at("advance").get<int>();
        gi.y_off   = g.value("y_off", 0);
        font->glyphs_.push_back(gi);
    }

    return font;
}

void BitmapFont::draw_text(SDL_Renderer* renderer,
                           const char* text,
                           int x, int baseline_y,
                           SDL_Color color) const {
    if (!atlas_ || !text) return;

    SDL_SetTextureColorMod(atlas_, color.r, color.g, color.b);
    SDL_SetTextureAlphaMod(atlas_, color.a);

    const char* p = text;
    while (*p) {
        const uint32_t cp = next_codepoint(p);
        if (cp == 0) break;

        const int gi = codepoint_to_glyph(cp);
        const GlyphInfo& g = glyphs_[gi];
        if (g.ink_h <= 0 || g.slot_w <= 0) {
            x += g.advance;
            continue;
        }

        // Place the glyph's ink top at baseline_y + y_off so its baseline
        // falls at baseline_y (y_off is negative for above-baseline ink).
        const SDL_Rect src{g.atlas_x, g.atlas_y, g.slot_w, g.ink_h};
        const SDL_Rect dst{x, baseline_y + g.y_off, g.slot_w, g.ink_h};
        SDL_RenderCopy(renderer, atlas_, &src, &dst);
        x += g.advance;
    }
}

void BitmapFont::draw_text_shadowed(SDL_Renderer* renderer,
                                    const char* text,
                                    int x, int baseline_y,
                                    SDL_Color color,
                                    SDL_Color shadow,
                                    int dx, int dy) const {
    draw_text(renderer, text, x + dx, baseline_y + dy, shadow);
    draw_text(renderer, text, x, baseline_y, color);
}

int BitmapFont::measure_text(const char* text) const {
    int w = 0;
    const char* p = text;
    while (*p) {
        const uint32_t cp = next_codepoint(p);
        if (cp == 0) break;
        w += glyphs_[codepoint_to_glyph(cp)].advance;
    }
    return w;
}

}  // namespace opente::render
