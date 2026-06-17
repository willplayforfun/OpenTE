#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <SDL.h>

namespace opente::render {

/// Bitmap font loaded from an extracted Trade Empires glyph atlas.
///
/// Each face/size pair (e.g. "clea" at 12pt) is one BitmapFont instance.
/// Load with BitmapFont::load(); draw with draw_text().
///
/// Atlas format (written by OpenTE extractor `sprites/font.py`):
///   - RGBA PNG, white ink (R=G=B=255), alpha = glyph coverage [0-255].
///   - Stored top-down (no Y-flip).
///   - Source rect for glyph i:
///       x = glyphs[i].atlas_x,  y = glyphs[i].atlas_y,
///       w = glyphs[i].slot_w,   h = glyphs[i].ink_h
///   - Destination: (cursor_x, baseline_y + glyphs[i].y_off)
///     y_off is the offset from the baseline to the top of the glyph's ink
///     (negative = above the baseline), so each glyph lands at its correct
///     height regardless of cap/x-height/descender.
///
/// Text colour is applied via SDL_SetTextureColorMod.
/// SDL_BLENDMODE_BLEND is set on the texture at load time.
class BitmapFont {
public:
    ~BitmapFont();

    BitmapFont(const BitmapFont&)            = delete;
    BitmapFont& operator=(const BitmapFont&) = delete;

    /// Loads the font from `json_path` (e.g. `.../game_data/fonts/clea_12pt.json`).
    /// The PNG atlas is expected at the same directory with the `_atlas.png` suffix.
    /// Returns nullptr on failure (missing file, parse error, SDL texture error).
    static std::unique_ptr<BitmapFont> load(SDL_Renderer* renderer,
                                            const std::filesystem::path& json_path);

    /// Draws UTF-8 text with its baseline at (x, baseline_y).
    /// `color` sets the ink colour; alpha is multiplied with glyph coverage.
    void draw_text(SDL_Renderer* renderer,
                   const char* text,
                   int x, int baseline_y,
                   SDL_Color color) const;

    /// Draws `text` with a drop shadow: a shadow pass in `shadow` at
    /// (x+dx, baseline_y+dy), then the main pass in `color`. The original
    /// Trade Empires UI renders all text with a 1px shadow, so this is the
    /// default path for UI text. (Inline <o> outline markup is a separate,
    /// symmetric effect — not this.)
    void draw_text_shadowed(SDL_Renderer* renderer,
                            const char* text,
                            int x, int baseline_y,
                            SDL_Color color,
                            SDL_Color shadow = SDL_Color{0, 0, 0, 255},
                            int dx = 1, int dy = 1) const;

    /// Returns the pixel width of `text` (sum of advance widths).
    int measure_text(const char* text) const;

    // NOTE: there is intentionally NO small-caps helper here. The original
    // game's small-caps UI text uses the `seri`/`sans` faces, whose lowercase
    // codepoints are already small-capital glyphs; small caps is a property of
    // the font, not a render-time transform. Just draw_text() the real face.

    /// Rich-text rendering: supports <b>...</b> (1px horizontal smear bold)
    /// and <o>...</o> (1px symmetric outline) inline markup.
    void draw_rich_text(SDL_Renderer* renderer, const char* text,
                        int x, int baseline_y, SDL_Color color) const;
    void draw_rich_text_shadowed(SDL_Renderer* renderer, const char* text,
                                 int x, int baseline_y, SDL_Color color,
                                 SDL_Color shadow = SDL_Color{0, 0, 0, 255},
                                 int dx = 1, int dy = 1) const;
    int measure_rich_text(const char* text) const;

    int line_height() const noexcept { return line_height_; }
    int ascender()    const noexcept { return ascender_; }
    int descender()   const noexcept { return descender_; }

private:
    struct GlyphInfo {
        int atlas_x = 0;
        int atlas_y = 0;   // top of ink in the atlas PNG
        int slot_w  = 0;
        int ink_h   = 0;
        int advance = 0;
        int y_off   = 0;   // baseline -> top-of-ink offset (negative = above)
    };

    BitmapFont() = default;

    // Map a Unicode codepoint to a glyph index via the dense cmap array.
    int codepoint_to_glyph(uint32_t cp) const noexcept;

    SDL_Texture* atlas_     = nullptr;
    int          line_height_ = 0;
    int          ascender_    = 0;
    int          descender_   = 0;   // positive = below baseline

    std::vector<uint16_t> cmap_;     // dense: cmap[cp] = glyph_index
    std::vector<GlyphInfo> glyphs_;
};

}  // namespace opente::render
