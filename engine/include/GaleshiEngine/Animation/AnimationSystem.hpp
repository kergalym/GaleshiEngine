#pragma once

#include "GaleshiEngine/Scene/Components.hpp"
#include <string>
#include <vector>

namespace Galeshi {

class Scene;
class AssetManager;

class AnimationSystem {
public:
    static void Update(Scene& scene, AssetManager& assets, float dt);

    // Creates a state machine with one state per imported clip.
    static AnimationStateMachineComponent CreateDefaultStateMachine(
        const std::vector<std::string>& clipNames);

    static bool SetState(AnimationStateMachineComponent& machine,
                         const std::string& stateName);
    static void SetParameter(AnimationStateMachineComponent& machine,
                             const std::string& name, float value);
    static void Trigger(AnimationStateMachineComponent& machine,
                        const std::string& name);
    static float GetParameter(const AnimationStateMachineComponent& machine,
                              const std::string& name, float fallback = 0.0f);
};

} // namespace Galeshi
