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

    // Генерирует NavMesh из геометрии сцены (ландшафт + меши)
    bool BuildFromScene(Scene& scene);

    // Ищет путь между двумя точками. Возвращает true, если путь найден.
    bool FindPath(const glm::vec3& start, const glm::vec3& end, std::vector<glm::vec3>& outPath);

    // Для дебаг-отрисовки
    const std::vector<glm::vec3>& GetDebugTriangles() const { return m_debugTriangles; }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
    std::vector<glm::vec3> m_debugTriangles;
};

} // namespace Galeshi
