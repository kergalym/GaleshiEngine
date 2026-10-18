#pragma once

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

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

    // P0: create dynamic box body; returns raw body index (0xFFFFFFFF on failure).
    uint32_t CreateBox(const glm::vec3& halfExtents, const glm::vec3& position);

    // P0: read body positions/rotations from Jolt, write into TransformComponent.
    void SyncTransformsToECS(Scene& scene);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
