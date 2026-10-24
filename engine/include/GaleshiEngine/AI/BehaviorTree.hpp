#pragma once

#include "GaleshiEngine/Scene/Components.hpp"

namespace Galeshi {

class Scene;
class NavigationSystem;

class BehaviorTreeSystem {
public:
    static void Update(Scene& scene, float dt, NavigationSystem* navSystem = nullptr);

    // Preset used by the editor's "AI Agent (Behavior Tree)" menu entry.
    static BehaviorTreeComponent MakeDefaultBehaviorTree();

    // Clears per-node runtime data (running timers, running children).
    static void ResetRuntime(BehaviorTreeComponent& tree);
    static void ResetPerception(PerceptionComponent& perception);
};

} // namespace Galeshi
