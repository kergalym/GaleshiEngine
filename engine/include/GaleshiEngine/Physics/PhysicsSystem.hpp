#pragma once

#include <memory>

namespace Galeshi {

class Scene;

class PhysicsSystem {
public:
    PhysicsSystem();
    ~PhysicsSystem();

    void Init();
    void Shutdown();
    void Update(Scene& scene, float dt);
    void Reset();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
