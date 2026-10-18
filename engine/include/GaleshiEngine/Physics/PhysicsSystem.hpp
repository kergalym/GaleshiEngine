#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <functional>
#include <entt/entt.hpp>

namespace Galeshi {

class Scene;

class EnginePhysicsSystem {
public:
    EnginePhysicsSystem();
    ~EnginePhysicsSystem();

    bool Init();
    void Shutdown();
    void Reset();
    void Update(Scene& scene, float deltaTime);

    uint32_t CreateBox(const glm::vec3& halfExtents, const glm::vec3& position, entt::entity entity = entt::null, bool isStatic = false);
    uint32_t CreateSphere(float radius, const glm::vec3& position, entt::entity entity = entt::null, bool isStatic = false);
    uint32_t CreateCapsule(float halfHeight, float radius, const glm::vec3& position, entt::entity entity = entt::null, bool isStatic = false);

    void SyncTransformsToECS(Scene& scene);
    uint32_t CreateCloth(int gridX, int gridY, float spacing, const glm::vec3& origin, entt::entity entity);
    void UpdateSoftBodies(Scene& scene);

    // Callback receives (entityA, entityB) for each ContactAdded event.
    void SetOnContactCallback(std::function<void(entt::entity, entt::entity)> cb);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
