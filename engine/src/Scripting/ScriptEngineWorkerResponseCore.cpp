#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Animation/BoneAttachment.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

namespace Galeshi {

using namespace ScriptEngineInternal;

bool ScriptEngine::MakeWorkerCoreResponse(
    const CppWorkerPacket& request, CppWorkerPacket& response) {
    const auto kind = static_cast<CppWorkerRequest>(request.kind);
    switch (kind) {
    case CppWorkerRequest::KeyDown:
        response.valid = m_isKeyDown && m_isKeyDown(request.text)
            ? 1u : 0u;
        break;
    case CppWorkerRequest::Axis:
        response.x = m_getAxis ? m_getAxis(request.text) : 0.0f;
        break;
    case CppWorkerRequest::FindEntity:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<NameComponent>();
            for (auto entity : view) {
                if (view.get<NameComponent>(entity).name == request.text) {
                    response.entity = static_cast<uint32_t>(entity);
                    response.valid = 1;
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::EntityValid:
        response.valid = m_scene &&
            m_scene->Registry().valid(
                static_cast<entt::entity>(request.entity)) ? 1u : 0u;
        break;
    case CppWorkerRequest::GetPosition:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const auto* attachment =
                    registry.try_get<BoneAttachmentComponent>(entity);
                const GaleshiScriptVec3 value = ToScriptVec3(
                    attachment ? attachment->localPosition
                               : registry.get<TransformComponent>(entity).position);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPosition:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const glm::vec3 position =
                    FromScriptVec3({request.x, request.y, request.z});
                if (auto* attachment =
                        registry.try_get<BoneAttachmentComponent>(entity)) {
                    attachment->localPosition = position;
                } else {
                    registry.get<TransformComponent>(entity).position =
                        position;
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetVelocity:
        if (m_getVelocity) {
            const GaleshiScriptVec3 value =
                ToScriptVec3(m_getVelocity(request.entity));
            response.x = value.x;
            response.y = value.y;
            response.z = value.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::SetVelocity:
        if (m_setVelocity) {
            m_setVelocity(
                request.entity,
                FromScriptVec3({request.x, request.y, request.z}));
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::SetCameraMode:
        if (m_setCameraMode) {
            response.valid = m_setCameraMode(
                static_cast<int>(request.entity)) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::GetCameraMode:
        if (m_getCameraMode) {
            response.entity =
                static_cast<uint32_t>(m_getCameraMode());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetCameraForward:
        if (m_getCameraForward) {
            const GaleshiScriptVec3 value =
                ToScriptVec3(m_getCameraForward());
            response.x = value.x;
            response.y = value.y;
            response.z = value.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetCameraRight:
        if (m_getCameraRight) {
            const GaleshiScriptVec3 value =
                ToScriptVec3(m_getCameraRight());
            response.x = value.x;
            response.y = value.y;
            response.z = value.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetPlayerStrafing:
        if (m_getPlayerStrafing) {
            response.valid = m_getPlayerStrafing() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetPlayerStrafing:
        if (m_setPlayerStrafing) {
            m_setPlayerStrafing(request.entity != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::CompileShader:
        if (m_compileShader) {
            std::string error;
            response.valid =
                m_compileShader(request.text, error) ? 1u : 0u;
            std::snprintf(response.text, sizeof(response.text), "%s",
                          error.c_str());
        }
        break;
    case CppWorkerRequest::CreateEntity:
        if (m_scene) {
            const std::string name =
                request.text[0] ? request.text : "Entity";
            const Entity entity = m_scene->CreateEntity(name);
            response.entity =
                static_cast<uint32_t>(entity.Handle());
            response.valid = entity.Handle() != entt::null ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::DestroyEntity:
        if (m_scene) {
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (m_scene->Registry().valid(entity)) {
                DestroyEntity(entity);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetName:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<NameComponent>(entity)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    registry.get<NameComponent>(entity).name.c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetName:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                if (registry.all_of<NameComponent>(entity)) {
                    registry.get<NameComponent>(entity).name =
                        request.text;
                } else {
                    registry.emplace<NameComponent>(
                        entity, NameComponent{request.text});
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetRotation:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const auto* attachment =
                    registry.try_get<BoneAttachmentComponent>(entity);
                const glm::quat rotation =
                    attachment ? attachment->localRotation
                               : registry.get<TransformComponent>(entity).rotation;
                response.x = rotation.x;
                response.y = rotation.y;
                response.z = rotation.z;
                response.w = rotation.w;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetRotation:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                glm::quat rotation(
                    request.w, request.x, request.y, request.z);
                if (glm::length(rotation) > 1e-6f) {
                    rotation = glm::normalize(rotation);
                } else {
                    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
                if (auto* attachment =
                        registry.try_get<BoneAttachmentComponent>(entity)) {
                    attachment->localRotation = rotation;
                } else {
                    registry.get<TransformComponent>(entity).rotation =
                        rotation;
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetScale:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const auto* attachment =
                    registry.try_get<BoneAttachmentComponent>(entity);
                const glm::vec3 scale =
                    attachment ? attachment->localScale
                               : registry.get<TransformComponent>(entity).scale;
                response.x = scale.x;
                response.y = scale.y;
                response.z = scale.z;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetScale:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const glm::vec3 scale(request.x, request.y, request.z);
                if (auto* attachment =
                        registry.try_get<BoneAttachmentComponent>(entity)) {
                    attachment->localScale = scale;
                } else {
                    registry.get<TransformComponent>(entity).scale = scale;
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetParent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                response.valid = 1;
                response.entity = UINT32_MAX;
                if (registry.all_of<ParentComponent>(entity)) {
                    const entt::entity parent =
                        registry.get<ParentComponent>(entity).parent;
                    if (parent != entt::null) {
                        response.entity =
                            static_cast<uint32_t>(parent);
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::SetParent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity)) break;
            if (request.other == UINT32_MAX) {
                registry.remove<ParentComponent>(entity);
                response.valid = 1;
                break;
            }
            const entt::entity parent =
                static_cast<entt::entity>(request.other);
            if (!registry.valid(parent) || parent == entity) break;
            // Reject cycles: the requested parent cannot already be a
            // descendant of this entity.
            entt::entity cursor = parent;
            bool cycle = false;
            for (int depth = 0; depth < 256 && cursor != entt::null;
                 ++depth) {
                if (cursor == entity) {
                    cycle = true;
                    break;
                }
                if (!registry.all_of<ParentComponent>(cursor)) break;
                cursor = registry.get<ParentComponent>(cursor).parent;
            }
            if (cycle) break;
            registry.remove<BoneAttachmentComponent>(entity);
            registry.emplace_or_replace<ParentComponent>(
                entity, ParentComponent{parent});
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AttachToBone:
        if (m_scene && m_assets && request.text[0]) {
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            const entt::entity character =
                static_cast<entt::entity>(request.other);
            response.valid = BoneAttachmentSystem::Attach(
                *m_scene, *m_assets, entity, character, request.text)
                ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::DetachFromBone:
        if (m_scene && m_assets) {
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            response.valid =
                BoneAttachmentSystem::Detach(*m_scene, *m_assets, entity)
                    ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::HasComponent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity)) break;
            const std::string key = NormalizeKey(request.text);
            response.valid =
                (key == "name" &&
                 registry.all_of<NameComponent>(entity)) ||
                (key == "transform" &&
                 registry.all_of<TransformComponent>(entity)) ||
                (key == "parent" &&
                 registry.all_of<ParentComponent>(entity)) ||
                ((key == "bone_attachment" ||
                  key == "boneattachment") &&
                 registry.all_of<BoneAttachmentComponent>(entity)) ||
                (key == "mesh" &&
                 registry.all_of<MeshComponent>(entity)) ||
                (key == "material" &&
                 registry.all_of<MaterialComponent>(entity)) ||
                (key == "model" &&
                 registry.all_of<ModelComponent>(entity)) ||
                (key == "skeletal" &&
                 registry.all_of<SkeletalComponent>(entity)) ||
                (key == "camera" &&
                 registry.all_of<CameraComponent>(entity)) ||
                (key == "directional_light" &&
                 registry.all_of<DirectionalLightComponent>(entity)) ||
                (key == "point_light" &&
                 registry.all_of<PointLightComponent>(entity)) ||
                (key == "spot_light" &&
                 registry.all_of<SpotLightComponent>(entity)) ||
                (key == "rigid_body" &&
                 registry.all_of<RigidBodyComponent>(entity)) ||
                (key == "rigidbody" &&
                 registry.all_of<RigidBodyComponent>(entity)) ||
                (key == "collider" &&
                 registry.all_of<ColliderComponent>(entity)) ||
                (key == "landscape" &&
                 registry.all_of<LandscapeComponent>(entity)) ||
                (key == "vegetation" &&
                 registry.all_of<VegetationComponent>(entity)) ||
                (key == "nav_agent" &&
                 registry.all_of<NavAgentComponent>(entity)) ||
                (key == "soft_body" &&
                 registry.all_of<SoftBodyComponent>(entity)) ||
                (key == "ai_controller" &&
                 registry.all_of<AIControllerComponent>(entity)) ||
                (key == "player_tag" &&
                 registry.all_of<PlayerTagComponent>(entity)) ||
                (key == "perception" &&
                 registry.all_of<PerceptionComponent>(entity)) ||
                (key == "behavior_tree" &&
                 registry.all_of<BehaviorTreeComponent>(entity)) ||
                (key == "audio_source" &&
                 registry.all_of<AudioSourceComponent>(entity)) ||
                (key == "audio_listener" &&
                 registry.all_of<AudioListenerComponent>(entity)) ||
                (key == "post_process_volume" &&
                 registry.all_of<PostProcessVolumeComponent>(entity)) ||
                (key == "particle_emitter" &&
                 registry.all_of<ParticleEmitterComponent>(entity)) ||
                (key == "water" &&
                 registry.all_of<WaterComponent>(entity)) ||
                (key == "groom" &&
                 registry.all_of<GroomComponent>(entity)) ||
                (key == "groom_hair" &&
                 registry.all_of<GroomComponent>(entity)) ||
                (key == "ui_canvas" &&
                 registry.all_of<UICanvasComponent>(entity)) ||
                (key == "animation_state_machine" &&
                 registry.all_of<AnimationStateMachineComponent>(entity)) ||
                (key == "script" &&
                 registry.all_of<ScriptComponent>(entity)) ||
                (key == "lua_script" &&
                 registry.all_of<ScriptComponent>(entity)) ||
                ((key == "script_variables" ||
                  key == "variables") &&
                 registry.all_of<ScriptVariablesComponent>(entity))
                ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::AddComponent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity)) break;
            const std::string key = NormalizeKey(request.text);
            if (key == "water") {
                if (!registry.all_of<WaterComponent>(entity)) {
                    registry.emplace<WaterComponent>(entity);
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetVariable:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity) || !request.text[0]) break;
            auto& variables =
                registry.all_of<ScriptVariablesComponent>(entity)
                    ? registry.get<ScriptVariablesComponent>(entity)
                    : registry.emplace<ScriptVariablesComponent>(entity);
            variables.values[request.text] = request.w;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetVariable:
        response.x = request.w;
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity) || !request.text[0] ||
                !registry.all_of<ScriptVariablesComponent>(entity)) {
                break;
            }
            const auto& variables =
                registry.get<ScriptVariablesComponent>(entity).values;
            const auto it = variables.find(request.text);
            if (it != variables.end()) response.x = it->second;
        }
        break;
    case CppWorkerRequest::FindAnimationEntity:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity root =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(root)) break;

            std::vector<entt::entity> pending{root};
            for (size_t head = 0; head < pending.size() && head < 4096;
                 ++head) {
                const entt::entity current = pending[head];
                if (!registry.valid(current)) continue;
                if (registry.all_of<AnimationStateMachineComponent>(current)) {
                    response.entity = static_cast<uint32_t>(current);
                    response.valid = 1;
                    break;
                }
                auto childView = registry.view<ParentComponent>();
                for (entt::entity child : childView) {
                    if (childView.get<ParentComponent>(child).parent ==
                        current) {
                        pending.push_back(child);
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::GetAxisRaw:
        response.x = m_getAxisRaw ? m_getAxisRaw(request.text) : 0.0f;
        break;
    case CppWorkerRequest::MouseButtonDown:
        response.valid = m_isMouseButtonDown &&
            m_isMouseButtonDown(request.text) ? 1u : 0u;
        break;
    case CppWorkerRequest::GetMousePosition:
        if (m_getMousePosition) {
            const glm::vec2 value = m_getMousePosition();
            response.x = value.x;
            response.y = value.y;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetMouseDelta:
        if (m_getMouseDelta) {
            const glm::vec2 value = m_getMouseDelta();
            response.x = value.x;
            response.y = value.y;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetMouseWheel:
        if (m_getMouseWheel) {
            response.x = m_getMouseWheel();
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AttachBox:
    case CppWorkerRequest::AttachSphere:
    case CppWorkerRequest::AttachCapsule:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                auto& transform =
                    registry.get<TransformComponent>(entity);
                if (registry.all_of<RigidBodyComponent>(entity)) {
                    auto& oldBody =
                        registry.get<RigidBodyComponent>(entity);
                    if (oldBody.bodyId != 0xFFFFFFFFu &&
                        m_physics.destroyBody) {
                        m_physics.destroyBody(oldBody.bodyId);
                    }
                }
                auto& body = registry.emplace_or_replace<
                    RigidBodyComponent>(entity);
                body.motionType = request.valid
                    ? RigidBodyComponent::MotionType::Static
                    : RigidBodyComponent::MotionType::Dynamic;
                body.mass = request.valid ? 0.0f : 1.0f;
                body.useGravity = request.valid == 0;
                auto& collider = registry.emplace_or_replace<
                    ColliderComponent>(entity);
                collider.friction = 0.5f;

                if (kind == CppWorkerRequest::AttachBox) {
                    const glm::vec3 halfExtents(
                        request.x, request.y, request.z);
                    transform.scale = halfExtents * 2.0f;
                    collider.shapeType = ColliderShapeType::Box;
                    collider.halfExtents = halfExtents;
                    if (m_physics.createBox) {
                        body.bodyId = m_physics.createBox(
                            halfExtents, transform.position, entity,
                            request.valid != 0);
                    }
                } else if (kind == CppWorkerRequest::AttachSphere) {
                    const float radius = request.x;
                    transform.scale =
                        glm::vec3(radius * 2.0f);
                    collider.shapeType =
                        ColliderShapeType::Sphere;
                    collider.radius = radius;
                    if (m_physics.createSphere) {
                        body.bodyId = m_physics.createSphere(
                            radius, transform.position, entity,
                            request.valid != 0);
                    }
                } else {
                    const float halfHeight = request.x;
                    const float radius = request.y;
                    transform.scale = glm::vec3(
                        radius * 2.0f,
                        (halfHeight + radius) * 2.0f,
                        radius * 2.0f);
                    collider.shapeType =
                        ColliderShapeType::Capsule;
                    collider.radius = radius;
                    collider.halfHeight = halfHeight;
                    if (m_physics.createCapsule) {
                        body.bodyId = m_physics.createCapsule(
                            halfHeight, radius, transform.position,
                            entity, request.valid != 0);
                    }
                }
                response.valid =
                    body.bodyId != 0xFFFFFFFFu ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::DestroyBody:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<RigidBodyComponent>(entity)) {
                auto& body = registry.get<RigidBodyComponent>(entity);
                if (body.bodyId != 0xFFFFFFFFu) {
                    if (m_physics.destroyBody) {
                        m_physics.destroyBody(body.bodyId);
                    }
                    body.bodyId = 0xFFFFFFFFu;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::ApplyImpulse:
        if (m_applyImpulse && HasPhysicsBody(request.entity)) {
            m_applyImpulse(
                request.entity,
                FromScriptVec3({request.x, request.y, request.z}));
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::CreateRagdoll:
        if (m_physics.createRagdoll) {
            response.valid = m_physics.createRagdoll(
                static_cast<entt::entity>(request.entity)) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::DestroyRagdoll:
        if (m_physics.destroyRagdoll) {
            m_physics.destroyRagdoll(
                static_cast<entt::entity>(request.entity));
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::RagdollActive:
        if (m_physics.ragdollActive) {
            response.valid = m_physics.ragdollActive(
                static_cast<entt::entity>(request.entity)) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::ApplyRagdollImpulse:
        if (m_physics.applyRagdollImpulse) {
            m_physics.applyRagdollImpulse(
                static_cast<entt::entity>(request.entity),
                FromScriptVec3({request.x, request.y, request.z}));
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::BuildNavMesh:
        if (m_buildNavMesh) {
            response.valid = m_buildNavMesh() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::FindPath:
        if (m_findPath) {
            m_cppNavigationPath.clear();
            const glm::vec3 start(request.x, request.y, request.z);
            const glm::vec3 end(request.x2, request.y2, request.z2);
            response.valid = m_findPath(
                start, end, m_cppNavigationPath) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::PathPointCount:
        response.entity = static_cast<uint32_t>(
            m_cppNavigationPath.size());
        response.valid = 1;
        break;
    case CppWorkerRequest::PathPoint:
        if (request.entity < m_cppNavigationPath.size()) {
            const glm::vec3& point =
                m_cppNavigationPath[request.entity];
            response.x = point.x;
            response.y = point.y;
            response.z = point.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::PlaySound:
        if (m_playSound) {
            m_playSound(request.text, request.valid != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::StopSound:
        if (m_stopSound) {
            m_stopSound(request.text);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::IsSoundPlaying:
        if (m_isSoundPlaying) {
            response.valid =
                m_isSoundPlaying(request.text) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::StopAllSounds:
        if (m_stopAllSounds) {
            m_stopAllSounds();
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::PauseSound:
        if (m_pauseSound) {
            response.valid = m_pauseSound(request.text) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::ResumeSound:
        if (m_resumeSound) {
            response.valid = m_resumeSound(request.text) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetSoundVolume:
        if (m_setSoundVolume) {
            response.valid =
                m_setSoundVolume(request.text, request.x) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetSoundPitch:
        if (m_setSoundPitch) {
            response.valid =
                m_setSoundPitch(request.text, request.x) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetSoundLooping:
        if (m_setSoundLooping) {
            response.valid =
                m_setSoundLooping(request.text, request.valid != 0) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SeekSound:
        if (m_seekSound) {
            response.valid =
                m_seekSound(request.text, request.x) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::GetSoundDuration:
        if (m_soundDuration) {
            response.x = m_soundDuration(request.text);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetSoundPosition:
        if (m_soundPosition) {
            response.x = m_soundPosition(request.text);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AttachAudioSource:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                if (m_stopAudioSource) {
                    m_stopAudioSource(request.entity);
                }
                auto& source = registry.emplace_or_replace<
                    AudioSourceComponent>(entity);
                source.soundPath = request.text;
                source.loop = (request.valid & 1u) != 0;
                source.spatial = (request.valid & 2u) != 0;
                source.autoplay = false;
                source.playing = false;
                const int bus = static_cast<int>(request.other);
                source.bus = static_cast<AudioBus>(
                    std::clamp(bus, 0, kAudioBusCount - 1));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::PlaySource:
        if (m_playAudioSource) {
            response.valid =
                m_playAudioSource(request.entity) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::StopSource:
        if (m_stopAudioSource) {
            m_stopAudioSource(request.entity);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::IsSourcePlaying:
        if (m_isAudioSourcePlaying) {
            response.valid =
                m_isAudioSourcePlaying(request.entity) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetAudioBus:
        if (m_scene) {
            const int index = AudioBusIndexFromName(request.text);
            if (index >= 0 && index < kAudioBusCount) {
                auto& bus =
                    m_scene->AudioBuses()[static_cast<size_t>(index)];
                bus.volume = std::clamp(request.x, 0.0f, 1.0f);
                bus.muted = request.valid != 0;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetAudioBusVolume:
        if (m_scene) {
            const int index = AudioBusIndexFromName(request.text);
            if (index >= 0 && index < kAudioBusCount) {
                response.x = m_scene->AudioBuses()[
                    static_cast<size_t>(index)].volume;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetAudioBusMuted:
        if (m_scene) {
            const int index = AudioBusIndexFromName(request.text);
            if (index >= 0 && index < kAudioBusCount) {
                response.valid = m_scene->AudioBuses()[
                    static_cast<size_t>(index)].muted ? 1u : 0u;
            }
        }
        break;
    default:
        return false;
    }
    return true;
}

} // namespace Galeshi
