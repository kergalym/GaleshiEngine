#include "GaleshiEngine/AI/AISystem.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include <glm/glm.hpp>
#include <cmath>
#include <spdlog/spdlog.h>

namespace Galeshi {

void AISystem::Update(Scene& scene, float dt) {
    auto view = scene.Registry().view<Galeshi::NavAgentComponent, Galeshi::TransformComponent>();
    for (auto e : view) {
        auto& agent = view.get<Galeshi::NavAgentComponent>(e);
        auto& tc = view.get<Galeshi::TransformComponent>(e);

        if (agent.path.empty()) continue;
        if (agent.currentWaypoint >= agent.path.size()) {
            agent.path.clear();
            agent.currentWaypoint = 0;
            continue;
        }

        glm::vec3 target = agent.path[agent.currentWaypoint];
        glm::vec3 dir = target - tc.position;
        float dist = glm::length(dir);

        if (dist < 0.01f) {
            agent.currentWaypoint++;
            continue;
        }

        if (dist < agent.stoppingDistance) {
            agent.currentWaypoint++;
            if (agent.currentWaypoint >= agent.path.size()) {
                agent.path.clear();
                agent.currentWaypoint = 0;
            }
            continue;
        }

        glm::vec3 move = (dir / dist) * agent.speed * dt;
        tc.position += move;
    }
}

} // namespace Galeshi
