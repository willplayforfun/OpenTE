#include "core/scene_manager.h"

namespace opente::core {

void SceneManager::set_scene(std::unique_ptr<Scene> scene) {
    scene_ = std::move(scene);
}

void SceneManager::reset() {
    scene_.reset();
}

bool SceneManager::handle_event(const SDL_Event& event) {
    if (!scene_) return false;
    return scene_->handle_event(event);
}

void SceneManager::update(float dt) {
    if (scene_) scene_->update(dt);
}

void SceneManager::render() {
    if (scene_) scene_->render();
}

bool SceneManager::wants_quit() const {
    return scene_ && scene_->wants_quit();
}

}  // namespace opente::core
