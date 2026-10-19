#include "GaleshiEngine/AI/AISystem.hpp"
#include "GaleshiEngine/AI/NavigationSystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <spdlog/spdlog.h>

namespace Galeshi {

void AISystem::Update(Scene& scene, float dt, NavigationSystem* navSystem) {
    auto view = scene.Registry().view<NavAgentComponent, TransformComponent>();
    for (auto e : view) {
        auto& agent = view.get<NavAgentComponent>(e);
        auto& tc    = view.get<TransformComponent>(e);

        // 1. Rebuild path if a new target was set (e.g., via right-click)
        if (agent.hasTarget && navSystem) {
            std::vector<glm::vec3> newPath;
            if (navSystem->FindPath(tc.position, agent.targetPosition, newPath)) {
                agent.path = std::move(newPath);
                agent.currentWaypoint = 0;
            } else {
                spdlog::warn("[AI] FindPath failed for agent {}", (uint32_t)e);
            }
            agent.hasTarget = false;
        }

        // 2. Follow the path waypoint-by-waypoint
        if (agent.path.empty()) continue;

        if (agent.currentWaypoint >= agent.path.size()) {
            agent.path.clear();
            agent.currentWaypoint = 0;
            continue;
        }

        glm::vec3 target = agent.path[agent.currentWaypoint];
        glm::vec3 delta  = target - tc.position;
        float dist       = glm::length(delta);

        // Reached current waypoint -> advance
        if (dist < 0.05f) {
            agent.currentWaypoint++;
            if (agent.currentWaypoint >= agent.path.size()) {
                agent.path.clear();
                agent.currentWaypoint = 0;
            }
            continue;
        }

        // Move toward waypoint
        glm::vec3 move = (delta / dist) * agent.speed * dt;
        if (glm::length(move) > dist) move = delta; // don't overshoot
        tc.position += move;
    }
}

} // namespace Galeshi
