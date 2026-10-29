#pragma once

#include "GaleshiEngine/Scene/Entity.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <entt/entt.hpp>
#include <array>
#include <string>
#include <vector>

namespace Galeshi {

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

private:
    entt::registry m_registry;
    std::array<AudioBusSettings, kAudioBusCount> m_audioBuses{};
};

} // namespace Galeshi
