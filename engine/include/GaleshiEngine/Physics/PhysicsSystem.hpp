#pragma once

#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>
#include <memory>

namespace Galeshi {

class Scene;

// Оставляем имя EnginePhysicsSystem, так как именно оно было вставлено в main.cpp
class EnginePhysicsSystem {
public:
    EnginePhysicsSystem();
    ~EnginePhysicsSystem();

    bool Init();
    void Shutdown();
    void Reset();
    void Update(Scene& scene, float deltaTime);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
