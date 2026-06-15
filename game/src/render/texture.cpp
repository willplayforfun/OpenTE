#include "render/texture.h"

#include <SDL_image.h>

#include <utility>

namespace opente::render {

Texture::~Texture() {
    if (texture_ != nullptr) {
        SDL_DestroyTexture(texture_);
    }
}

Texture::Texture(Texture&& other) noexcept
    : texture_(std::exchange(other.texture_, nullptr)),
      width_(std::exchange(other.width_, 0)),
      height_(std::exchange(other.height_, 0)) {}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        if (texture_ != nullptr) {
            SDL_DestroyTexture(texture_);
        }
        texture_ = std::exchange(other.texture_, nullptr);
        width_ = std::exchange(other.width_, 0);
        height_ = std::exchange(other.height_, 0);
    }
    return *this;
}

Texture Texture::load(SDL_Renderer* renderer, const std::filesystem::path& path) {
    SDL_Texture* raw = IMG_LoadTexture(renderer, path.string().c_str());
    if (raw == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load texture '%s': %s",
                      path.string().c_str(), IMG_GetError());
        return Texture{};
    }

    Texture texture;
    texture.texture_ = raw;
    SDL_QueryTexture(raw, nullptr, nullptr, &texture.width_, &texture.height_);
    return texture;
}

Texture Texture::from_raw(SDL_Texture* raw) {
    if (raw == nullptr) return Texture{};
    Texture t;
    t.texture_ = raw;
    SDL_QueryTexture(raw, nullptr, nullptr, &t.width_, &t.height_);
    return t;
}

}  // namespace opente::render
