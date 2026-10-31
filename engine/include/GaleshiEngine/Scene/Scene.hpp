#pragma once

#include "GaleshiEngine/Scene/Entity.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <entt/entt.hpp>
#include <glm/gtc/quaternion.hpp>
#include <array>
#include <string>
#include <vector>

namespace Galeshi {

// Camera state stored at scene level.  The editor camera is orbit-based, so
// both the derived world-space position/rotation and the orbit controls are
// saved.  Keeping the latter makes a loaded scene restore the exact editor
// view instead of only a momentary position.
struct SceneCameraState {
    bool      valid = false;
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 target{0.0f};
    float     yaw = 0.0f;
    float     pitch = 0.35f;
    float     radius = 8.0f;
};

class Scene {
public:
    Scene() = default;
    ~Scene() = default;

    Scene(const Scene&)            = delete;
    Scene& operator=(const Scene&) = delete;

    Entity CreateEntity(const std::string& name = "Entity");
    void   DestroyEntity(Entity e);

    entt::registry&       Registry()       { return m_registry; }
    const entt::registry& Registry() const { return m_registry; }

    // Утилита: пробегает все сущности с NameComponent
    std::vector<Entity> AllEntities();
    Entity FindEntityByName(const std::string& name);

    // Scene-level mixer state (serialized with the scene).
    std::array<AudioBusSettings, kAudioBusCount>& AudioBuses() {
        return m_audioBuses;
    }
    const std::array<AudioBusSettings, kAudioBusCount>& AudioBuses() const {
        return m_audioBuses;
    }

    // Scene-level camera view state.  `valid` is false for scenes saved
    // before camera state existed, so loaders can keep their current view.
    SceneCameraState& CameraState() { return m_cameraState; }
    const SceneCameraState& CameraState() const { return m_cameraState; }

    // Global post-process settings used as the base for every scene. Enabled
    // PostProcessVolumeComponents override this scene-wide; the editor applies
    // the toggle values to PostProcessManager when a scene is loaded.
    PostProcessSettings& GlobalPostProcess() { return m_globalPostProcess; }
    const PostProcessSettings& GlobalPostProcess() const {
        return m_globalPostProcess;
    }
    bool HasGlobalPostProcess() const { return m_hasGlobalPostProcess; }
    void SetHasGlobalPostProcess(bool value) { m_hasGlobalPostProcess = value; }

private:
    entt::registry m_registry;
    std::array<AudioBusSettings, kAudioBusCount> m_audioBuses{};
    SceneCameraState m_cameraState;
    PostProcessSettings m_globalPostProcess;
    bool m_hasGlobalPostProcess = false;
};

} // namespace Galeshi
