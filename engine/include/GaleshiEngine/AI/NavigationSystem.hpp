#pragma once

#include <glm/glm.hpp>
#include <vector>
#include <memory>

namespace Galeshi {

class Scene;

class NavigationSystem {
public:
    NavigationSystem();
    ~NavigationSystem();

    void Init();
    void Shutdown();

    // Builds a NavMesh from landscape and mesh geometry.
    bool BuildFromScene(Scene& scene);

    // Finds a path; returns true when one exists.
    bool FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& outPath);

    const std::vector<glm::vec3>& GetDebugTriangles() const { return m_debugTriangles; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<glm::vec3> m_debugTriangles;
};

} // namespace Galeshi
