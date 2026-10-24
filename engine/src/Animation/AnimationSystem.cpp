#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

#include <glm/glm.hpp>
#include <algorithm>
#include <cmath>

namespace Galeshi {
namespace {

const AnimationParameter* FindParameter(const AnimationStateMachineComponent& machine,
                                        const std::string& name) {
    for (const auto& parameter : machine.parameters) {
        if (parameter.name == name) return &parameter;
    }
    return nullptr;
}

bool TransitionMatches(const AnimationStateMachineComponent& machine,
                       const AnimationTransition& transition,
                       bool& consumeTrigger) {
    consumeTrigger = false;
    if (!transition.enabled) return false;
    if (transition.fromState >= 0 && transition.fromState != machine.currentState) return false;

    switch (transition.condition) {
        case AnimationCondition::Always:
            return true;
        case AnimationCondition::BoolTrue: {
            const AnimationParameter* p = FindParameter(machine, transition.parameter);
            return p && p->value > 0.5f;
        }
        case AnimationCondition::BoolFalse: {
            const AnimationParameter* p = FindParameter(machine, transition.parameter);
            return p && p->value <= 0.5f;
        }
        case AnimationCondition::Greater: {
            const AnimationParameter* p = FindParameter(machine, transition.parameter);
            return p && p->value > transition.threshold;
        }
        case AnimationCondition::Less: {
            const AnimationParameter* p = FindParameter(machine, transition.parameter);
            return p && p->value < transition.threshold;
        }
        case AnimationCondition::Trigger: {
            const AnimationParameter* p = FindParameter(machine, transition.parameter);
            if (p && p->value > 0.5f) {
                consumeTrigger = true;
                return true;
            }
            return false;
        }
    }
    return false;
}

float ClipDuration(const AssetManager::LoadedModel& model, int clip) {
    if (clip >= 0 && clip < static_cast<int>(model.animationDurations.size())) {
        return std::max(0.0f, model.animationDurations[static_cast<size_t>(clip)]);
    }
    return 0.0f;
}

void AdvanceTime(float& time, float duration, float step, bool loop) {
    time += step;
    if (duration <= 0.0f) return;
    if (loop) {
        time = std::fmod(time, duration);
        if (time < 0.0f) time += duration;
    } else {
        time = std::clamp(time, 0.0f, duration);
    }
}

} // namespace

AnimationStateMachineComponent AnimationSystem::CreateDefaultStateMachine(
    const std::vector<std::string>& clipNames) {
    AnimationStateMachineComponent machine;
    if (clipNames.empty()) {
        AnimationState state;
        state.name = "Idle";
        state.clip = 0;
        machine.states.push_back(state);
        return machine;
    }

    machine.states.reserve(clipNames.size());
    for (size_t i = 0; i < clipNames.size(); ++i) {
        AnimationState state;
        state.name = clipNames[i].empty() ? ("Clip " + std::to_string(i)) : clipNames[i];
        state.clip = static_cast<int>(i);
        state.loop = true;
        state.speed = 1.0f;
        machine.states.push_back(state);
    }
    machine.currentState = 0;
    machine.previousState = -1;
    machine.blendElapsed = machine.blendDuration;
    return machine;
}

bool AnimationSystem::SetState(AnimationStateMachineComponent& machine,
                               const std::string& stateName) {
    for (size_t i = 0; i < machine.states.size(); ++i) {
        if (machine.states[i].name != stateName) continue;
        const int next = static_cast<int>(i);
        if (next == machine.currentState) return true;
        machine.previousState = machine.currentState;
        machine.previousTime = machine.stateTime;
        machine.currentState = next;
        machine.stateTime = 0.0f;
        machine.blendElapsed = 0.0f;
        machine.initialized = true;
        return true;
    }
    return false;
}

void AnimationSystem::SetParameter(AnimationStateMachineComponent& machine,
                                   const std::string& name, float value) {
    for (auto& parameter : machine.parameters) {
        if (parameter.name == name) {
            parameter.value = value;
            return;
        }
    }
    if (!name.empty()) machine.parameters.push_back({name, value});
}

void AnimationSystem::Trigger(AnimationStateMachineComponent& machine,
                              const std::string& name) {
    SetParameter(machine, name, 1.0f);
}

float AnimationSystem::GetParameter(const AnimationStateMachineComponent& machine,
                                    const std::string& name, float fallback) {
    const AnimationParameter* parameter = FindParameter(machine, name);
    return parameter ? parameter->value : fallback;
}

void AnimationSystem::Update(Scene& scene, AssetManager& assets, float dt) {
    auto view = scene.Registry().view<SkeletalComponent>();
    for (auto handle : view) {
        auto& skel = view.get<SkeletalComponent>(handle);
        if (!skel.isAnimated || skel.sourcePath.empty()) continue;

        auto model = assets.LoadModel(skel.sourcePath);
        if (!model.hasAnimation || model.animationNames.empty()) continue;

        if (skel.boneMatrices.size() != 256) {
            skel.boneMatrices.assign(256, glm::mat4(1.0f));
        }

        auto* machine = scene.Registry().try_get<AnimationStateMachineComponent>(handle);
        if (!machine || !machine->enabled) {
            const int clip = std::clamp(skel.activeAnimation, 0,
                                        static_cast<int>(model.animationNames.size()) - 1);
            const float duration = ClipDuration(model, clip);
            skel.activeAnimation = clip;
            if (duration > 0.0f) {
                skel.currentTime += dt * skel.animationSpeed;
                if (skel.currentTime < 0.0f) skel.currentTime = 0.0f;
                skel.currentTime = std::fmod(skel.currentTime, duration);
            } else {
                skel.currentTime += dt * skel.animationSpeed;
                if (skel.currentTime > 10.0f) skel.currentTime = 0.0f;
            }
            assets.UpdateAnimationClip(model, clip, skel.currentTime, skel.boneMatrices);
            continue;
        }

        if (machine->states.empty()) {
            *machine = CreateDefaultStateMachine(model.animationNames);
        }
        if (machine->currentState < 0 ||
            machine->currentState >= static_cast<int>(machine->states.size())) {
            machine->currentState = 0;
            machine->stateTime = 0.0f;
        }
        if (!machine->initialized) {
            machine->previousState = -1;
            machine->blendElapsed = machine->blendDuration;
            machine->initialized = true;
        }

        // Pick the first matching transition. Triggers are consumed so they
        // fire exactly once.
        for (size_t i = 0; i < machine->transitions.size(); ++i) {
            auto& transition = machine->transitions[i];
            bool consumeTrigger = false;
            if (!TransitionMatches(*machine, transition, consumeTrigger)) continue;
            if (transition.toState < 0 ||
                transition.toState >= static_cast<int>(machine->states.size())) continue;

            if (consumeTrigger) {
                for (auto& parameter : machine->parameters) {
                    if (parameter.name == transition.parameter) {
                        parameter.value = 0.0f;
                        break;
                    }
                }
            }

            if (transition.toState != machine->currentState) {
                machine->previousState = machine->currentState;
                machine->previousTime = machine->stateTime;
                machine->currentState = transition.toState;
                machine->stateTime = 0.0f;
                machine->blendElapsed = 0.0f;
                machine->blendDuration = std::max(0.0f, transition.duration);
            }
            break;
        }

        AnimationState& state = machine->states[machine->currentState];
        const int clip = std::clamp(state.clip, 0,
                                    static_cast<int>(model.animationNames.size()) - 1);
        const float duration = ClipDuration(model, clip);
        machine->currentClipDuration = duration;
        const float speed = state.speed * machine->playbackSpeed * std::max(0.0f, skel.animationSpeed);
        AdvanceTime(machine->stateTime, duration, dt * speed, state.loop);

        if (machine->blendElapsed < machine->blendDuration &&
            machine->previousState >= 0 &&
            machine->previousState < static_cast<int>(machine->states.size())) {
            machine->blendElapsed = std::min(machine->blendElapsed + std::max(dt, 0.0f),
                                             machine->blendDuration);
            AnimationState& previous = machine->states[machine->previousState];
            const int previousClip = std::clamp(previous.clip, 0,
                                                static_cast<int>(model.animationNames.size()) - 1);
            const float previousDuration = ClipDuration(model, previousClip);
            AdvanceTime(machine->previousTime, previousDuration,
                        dt * previous.speed * machine->playbackSpeed *
                            std::max(0.0f, skel.animationSpeed),
                        previous.loop);

            float blend = machine->blendDuration > 1e-5f
                ? std::clamp(machine->blendElapsed / machine->blendDuration, 0.0f, 1.0f)
                : 1.0f;
            blend = blend * blend * (3.0f - 2.0f * blend);
            assets.UpdateAnimationBlended(model,
                                          previousClip, machine->previousTime,
                                          clip, machine->stateTime,
                                          blend, skel.boneMatrices);
            if (machine->blendElapsed >= machine->blendDuration) {
                machine->previousState = -1;
            }
        } else {
            machine->previousState = -1;
            assets.UpdateAnimationClip(model, clip, machine->stateTime, skel.boneMatrices);
        }

        skel.activeAnimation = clip;
        skel.currentTime = machine->stateTime;
    }
}

} // namespace Galeshi
