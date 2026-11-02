#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

#include <glm/glm.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <spdlog/spdlog.h>
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

void BlendBonePalettes(const std::vector<glm::mat4>& a,
                       const std::vector<glm::mat4>& b,
                       float blend,
                       std::vector<glm::mat4>& out) {
    if (out.size() != 256) out.assign(256, glm::mat4(1.0f));
    for (int i = 0; i < 255; ++i) {
        glm::vec3 scaleA, translationA, skewA;
        glm::vec4 perspectiveA;
        glm::quat rotationA;
        glm::vec3 scaleB, translationB, skewB;
        glm::vec4 perspectiveB;
        glm::quat rotationB;

        const bool okA = glm::decompose(a[i], scaleA, rotationA,
                                        translationA, skewA, perspectiveA);
        const bool okB = glm::decompose(b[i], scaleB, rotationB,
                                        translationB, skewB, perspectiveB);
        if (okA && okB) {
            const glm::vec3 translation =
                glm::mix(translationA, translationB, blend);
            const glm::quat rotation =
                glm::normalize(glm::slerp(rotationA, rotationB, blend));
            const glm::vec3 scale = glm::mix(scaleA, scaleB, blend);
            out[i] = glm::translate(glm::mat4(1.0f), translation) *
                     glm::mat4_cast(rotation) *
                     glm::scale(glm::mat4(1.0f), scale);
        } else {
            for (int c = 0; c < 4; ++c) {
                out[i][c] = glm::mix(a[i][c], b[i][c], blend);
            }
        }
    }
    out[255] = glm::mat4(1.0f);
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
        if (skel.sourcePath.empty()) continue;

        auto targetModel = assets.LoadModel(skel.sourcePath);
        auto* machine =
            scene.Registry().try_get<AnimationStateMachineComponent>(handle);

        bool hasSeparateClip = false;
        bool hasEmbeddedClip = false;
        if (machine) {
            for (const auto& state : machine->states) {
                if (state.animationPath.empty()) {
                    hasEmbeddedClip = true;
                } else {
                    hasSeparateClip = true;
                }
            }
        }
        if (!skel.isAnimated && !hasSeparateClip && !hasEmbeddedClip) continue;
        if (hasSeparateClip ||
            (hasEmbeddedClip && machine && machine->enabled)) {
            // A state machine can drive clips even when the skeleton's
            // standalone playback checkbox was turned off. This is essential
            // for embedded clips authored with the character FBX.
            if (hasEmbeddedClip && machine && machine->enabled &&
                !skel.isAnimated) {
                spdlog::info(
                    "[Animation] Enabling embedded clips for '{}'",
                    skel.sourcePath);
            }
            skel.isAnimated = true;
        }

        if (skel.boneMatrices.size() != 256) {
            skel.boneMatrices.assign(256, glm::mat4(1.0f));
        }

        if (!machine || !machine->enabled) {
            if (!targetModel.hasAnimation || targetModel.animationNames.empty()) {
                continue;
            }
            const int clip = std::clamp(
                skel.activeAnimation, 0,
                static_cast<int>(targetModel.animationNames.size()) - 1);
            const float duration = ClipDuration(targetModel, clip);
            skel.activeAnimation = clip;
            if (duration > 0.0f) {
                skel.currentTime += dt * skel.animationSpeed;
                if (skel.currentTime < 0.0f) skel.currentTime = 0.0f;
                skel.currentTime = std::fmod(skel.currentTime, duration);
            } else {
                skel.currentTime += dt * skel.animationSpeed;
                if (skel.currentTime > 10.0f) skel.currentTime = 0.0f;
            }
            assets.UpdateAnimationClip(targetModel, clip, skel.currentTime,
                                       skel.boneMatrices);
            continue;
        }

        if (machine->states.empty()) {
            if (!targetModel.hasAnimation || targetModel.animationNames.empty()) {
                continue;
            }
            *machine = CreateDefaultStateMachine(targetModel.animationNames);
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

        auto animationPathForState = [&](const AnimationState& state) {
            return state.animationPath.empty()
                ? skel.sourcePath
                : state.animationPath;
        };
        auto sampleState = [&](const AnimationState& state, float time,
                               std::vector<glm::mat4>& out) {
            const std::string path = animationPathForState(state);
            auto animationModel = assets.LoadModel(path);
            if (!animationModel.hasAnimation ||
                animationModel.animationNames.empty()) {
                out.assign(256, glm::mat4(1.0f));
                return;
            }
            const int clip = std::clamp(
                state.clip, 0,
                static_cast<int>(animationModel.animationNames.size()) - 1);
            if (state.animationPath.empty()) {
                assets.UpdateAnimationClip(animationModel, clip, time, out);
            } else {
                assets.UpdateAnimationClipRetargeted(
                    animationModel, clip, time, targetModel, out,
                    machine->animationScale);
            }
        };

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
        const std::string currentPath = animationPathForState(state);
        auto currentModel = assets.LoadModel(currentPath);
        const int clip = std::clamp(
            state.clip, 0,
            std::max(0, static_cast<int>(currentModel.animationNames.size()) - 1));
        const float duration = ClipDuration(currentModel, clip);
        machine->currentClipDuration = duration;
        const float speed = state.speed * machine->playbackSpeed *
                            std::max(0.0f, skel.animationSpeed);
        AdvanceTime(machine->stateTime, duration, dt * speed, state.loop);

        if (machine->blendElapsed < machine->blendDuration &&
            machine->previousState >= 0 &&
            machine->previousState < static_cast<int>(machine->states.size())) {
            machine->blendElapsed = std::min(
                machine->blendElapsed + std::max(dt, 0.0f),
                machine->blendDuration);
            AnimationState& previous =
                machine->states[machine->previousState];
            const std::string previousPath = animationPathForState(previous);
            auto previousModel = assets.LoadModel(previousPath);
            const int previousClip = std::clamp(
                previous.clip, 0,
                std::max(0, static_cast<int>(previousModel.animationNames.size()) - 1));
            const float previousDuration = ClipDuration(previousModel,
                                                        previousClip);
            AdvanceTime(machine->previousTime, previousDuration,
                        dt * previous.speed * machine->playbackSpeed *
                            std::max(0.0f, skel.animationSpeed),
                        previous.loop);

            float blend = machine->blendDuration > 1e-5f
                ? std::clamp(machine->blendElapsed / machine->blendDuration,
                             0.0f, 1.0f)
                : 1.0f;
            blend = blend * blend * (3.0f - 2.0f * blend);

            std::vector<glm::mat4> previousPalette;
            std::vector<glm::mat4> currentPalette;
            sampleState(previous, machine->previousTime, previousPalette);
            sampleState(state, machine->stateTime, currentPalette);
            BlendBonePalettes(previousPalette, currentPalette, blend,
                              skel.boneMatrices);
            if (machine->blendElapsed >= machine->blendDuration) {
                machine->previousState = -1;
            }
        } else {
            machine->previousState = -1;
            sampleState(state, machine->stateTime, skel.boneMatrices);
        }

        skel.activeAnimation = clip;
        skel.currentTime = machine->stateTime;
    }
}

} // namespace Galeshi
