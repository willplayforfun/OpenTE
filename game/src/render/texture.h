#pragma once

#include <SDL.h>

#include <filesystem>
#include <string>

namespace opente::render {

/// Owns an SDL_Texture loaded from an image file (PNG, etc. via SDL_image).
///
/// Move-only RAII wrapper: the texture is destroyed when the Texture object
/// is destroyed. A default-constructed Texture is "empty" (width/height are
/// zero, `handle()` returns nullptr) and can be checked with `valid()`.
class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;

    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    /// Loads an image file into a GPU texture for `renderer`.
    /// Returns an empty (invalid) Texture and logs an error on failure.
    static Texture load(SDL_Renderer* renderer, const std::filesystem::path& path);

    bool valid() const { return texture_ != nullptr; }
    SDL_Texture* handle() const { return texture_; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    SDL_Texture* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

}  // namespace opente::render
