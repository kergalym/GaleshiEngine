#include "GaleshiEngine/AI/AISystem.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/AI/NavigationSystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/glm.hpp>
#include <cmath>
#include <vector>
#include <spdlog/spdlog.h>

namespace Galeshi {

void AISystem::Update(Scene& scene, float dt, NavigationSystem* navSystem) {
    // Behavior trees and perception run before path following.  They set
    // NavAgent targets; the loop below turns those targets into movement.
    BehaviorTreeSystem::Update(scene, dt, navSystem);

    auto view = scene.Registry().view<NavAgentComponent, TransformComponent>();
    for (auto e : view) {
        auto& agent = view.get<NavAgentComponent>(e);
        auto& tc    = view.get<TransformComponent>(e);

        // 1. Rebuild path if a new target was set (e.g., by a behavior tree,
        //    Lua, or right-click).  Without a navmesh, fall back to a direct
        //    point so AI still moves in scenes that have not built one yet.
        if (agent.hasTarget) {
            std::vector<glm::vec3> newPath;
            if (navSystem && navSystem->FindPath(tc.position, agent.targetPosition, newPath)) {
                agent.path = std::move(newPath);
                agent.currentWaypoint = 0;
            } else {
                agent.path = { agent.targetPosition };
                agent.currentWaypoint = 0;
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

        // Keep the agent facing along the path while it follows navmesh
        // output.  Direct (non-navmesh) movement already does this in the
        // behavior tree, but a NavAgent previously slid sideways.
        const float yaw = std::atan2(delta.x, delta.z);
        tc.rotation = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

        // Move toward waypoint
        glm::vec3 move = (delta / dist) * agent.speed * dt;
        if (glm::length(move) > dist) move = delta; // don't overshoot
        tc.position += move;
    }
}

} // namespace Galeshi
