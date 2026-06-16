#pragma once

#include <SDL.h>

namespace opente::core {

class Scene {
public:
    virtual ~Scene() = default;
    virtual bool handle_event(const SDL_Event& event) = 0;
    virtual void update(float dt) = 0;
    virtual void render() = 0;
    virtual bool wants_quit()       const { return false; }
    virtual bool wants_main_menu()  const { return false; }
};

}  // namespace opente::core
