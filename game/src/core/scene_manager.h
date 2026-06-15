#pragma once

#include <memory>

#include <SDL.h>

#include "core/scene.h"

namespace opente::core {

class SceneManager {
public:
    void set_scene(std::unique_ptr<Scene> scene);

    /// Destroys the current scene immediately. Call before SDL teardown.
    void reset();

    bool has_scene() const { return scene_ != nullptr; }

    bool handle_event(const SDL_Event& event);
    void update(float dt);
    void render();
    bool wants_quit() const;

private:
    std::unique_ptr<Scene> scene_;
};

}  // namespace opente::core
