#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
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
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include <spdlog/spdlog.h>

namespace Galeshi {

using namespace ScriptEngineInternal;

void ScriptEngine::BuildApiGameplay(ScriptApiBuildContext& context) {
    auto& readNumber = context.readNumber;
    auto& readBool = context.readBool;
    auto& readString = context.readString;
    auto& readVec2 = context.readVec2;
    auto& readVec3 = context.readVec3;
    auto& readVec4 = context.readVec4;
    auto& readQuat = context.readQuat;
    auto& readVec3Args = context.readVec3Args;
    auto& makeVec2 = context.makeVec2;
    auto& makeVec3 = context.makeVec3;
    auto& makeVec4 = context.makeVec4;
    auto& makeQuat = context.makeQuat;
    auto& validId = context.validId;
    auto& idFromValue = context.idFromValue;
    auto& resolve = context.resolve;
    auto& idFromTable = context.idFromTable;
    auto& resolveTable = context.resolveTable;
    auto& makeEntityTable = context.makeEntityTable;
    auto& makeEntity = context.makeEntity;
    auto& getTransform = context.getTransform;
    auto& world = context.world;
    auto& input = context.input;
    auto& physics = context.physics;
    auto& audio = context.audio;
    auto& animation = context.animation;
    auto& ai = context.ai;
    auto& navigation = context.navigation;
    auto& ui = context.ui;
    auto& particles = context.particles;
    auto& engine = context.engine;
    auto& camera = context.camera;
    auto& player = context.player;
    auto& shader = context.shader;

    // ------------------------------------------------------------------
    // Physics.  The script layer owns the ECS components; the host creates
    // the native Jolt body through the callbacks registered by BindPhysics().
    // ------------------------------------------------------------------
    auto spawnPhysicsEntity = [this, makeEntity](const std::string& name,
                                                  int shape,
                                                  const glm::vec3& position,
                                                  const glm::vec3& size,
                                                  float radius,
                                                  float halfHeight,
                                                  bool isStatic) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        if (name == "Player") entity.AddComponent<PlayerTagComponent>();

        auto& transform = entity.AddComponent<TransformComponent>();
        transform.position = position;

        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.motionType = isStatic ? RigidBodyComponent::MotionType::Static
                                   : RigidBodyComponent::MotionType::Dynamic;
        body.mass = isStatic ? 0.0f : 1.0f;
        body.useGravity = !isStatic;

        auto& collider = entity.AddComponent<ColliderComponent>();
        collider.friction = 0.5f;

        uint32_t bodyId = 0xFFFFFFFFu;
        if (shape == 0) {
            transform.scale = size;
            collider.shapeType = ColliderShapeType::Box;
            collider.halfExtents = size * 0.5f;
            if (m_physics.createBox) {
                bodyId = m_physics.createBox(size * 0.5f, position,
                                             static_cast<entt::entity>(entity),
                                             isStatic);
            }
        } else if (shape == 1) {
            transform.scale = glm::vec3(radius * 2.0f);
            collider.shapeType = ColliderShapeType::Sphere;
            collider.radius = radius;
            if (m_physics.createSphere) {
                bodyId = m_physics.createSphere(radius, position,
                                                static_cast<entt::entity>(entity),
                                                isStatic);
            }
        } else {
            transform.scale = glm::vec3(radius * 2.0f,
                                        (halfHeight + radius) * 2.0f,
                                        radius * 2.0f);
            collider.shapeType = ColliderShapeType::Capsule;
            collider.radius = radius;
            collider.halfHeight = halfHeight;
            if (m_physics.createCapsule) {
                bodyId = m_physics.createCapsule(halfHeight, radius, position,
                                                 static_cast<entt::entity>(entity),
                                                 isStatic);
            }
        }
        body.bodyId = bodyId;
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };

    physics["create_box"] = [this, spawnPhysicsEntity, readVec3](
                                 const std::string& name, float x, float y, float z,
                                 sol::object sizeValue,
                                 sol::optional<bool> isStatic) -> sol::object {
        glm::vec3 size{1.0f};
        if (sizeValue.get_type() == sol::type::table) {
            if (!readVec3(sizeValue, size)) {
                return sol::make_object(m_lua, sol::nil);
            }
        } else if (sizeValue.get_type() == sol::type::number) {
            size = glm::vec3(static_cast<float>(sizeValue.as<double>()));
        } else {
            return sol::make_object(m_lua, sol::nil);
        }
        return spawnPhysicsEntity(name, 0, {x, y, z}, size, 0.0f, 0.0f,
                                  isStatic.value_or(false));
    };
    physics["create_sphere"] = [spawnPhysicsEntity](
                                    const std::string& name, float x, float y,
                                    float z, float radius,
                                    sol::optional<bool> isStatic) {
        return spawnPhysicsEntity(name, 1, {x, y, z}, glm::vec3(radius * 2.0f),
                                  radius, 0.0f, isStatic.value_or(false));
    };
    physics["create_capsule"] = [spawnPhysicsEntity](
                                     const std::string& name, float x, float y,
                                     float z, float halfHeight, float radius,
                                     sol::optional<bool> isStatic) {
        return spawnPhysicsEntity(name, 2, {x, y, z}, glm::vec3(0.0f),
                                  radius, halfHeight, isStatic.value_or(false));
    };

    const auto applyImpulse = [this, resolve, readVec3Args](
                                  sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 impulse{0.0f};
        if (entity == entt::null || !readVec3Args(args, impulse)) return false;
        const uint32_t id = static_cast<uint32_t>(entity);
        if (m_applyImpulse && HasPhysicsBody(id)) {
            m_applyImpulse(id, impulse);
            return true;
        }

        // A body-less character root (for example a skeleton/visual root
        // without a ColliderComponent) moves through the script fallback
        // controller. Apply the impulse there as a velocity change so Jump
        // works for those characters as well.
        if (!m_scene) return false;
        auto& registry = m_scene->Registry();
        if (!registry.valid(entity) ||
            !registry.all_of<RigidBodyComponent, TransformComponent>(entity)) {
            return false;
        }
        const auto& rb = registry.get<RigidBodyComponent>(entity);
        auto& fallback = m_fallbackVelocities[id];
        if (!fallback.hasGroundY) {
            fallback.groundY =
                registry.get<TransformComponent>(entity).position.y;
            fallback.hasGroundY = true;
        }
        fallback.value += impulse / std::max(rb.mass, 0.01f);
        fallback.frame = m_frame;
        return true;
    };
    const auto getVelocity = [this, resolve, makeVec3](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return makeVec3(glm::vec3(0.0f));
        const uint32_t id = static_cast<uint32_t>(entity);
        if (m_getVelocity && HasPhysicsBody(id)) {
            return makeVec3(m_getVelocity(id));
        }
        const auto it = m_fallbackVelocities.find(id);
        return makeVec3(it != m_fallbackVelocities.end()
                            ? it->second.value
                            : glm::vec3(0.0f));
    };
    const auto setVelocity = [this, resolve, readVec3Args](
                                  sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 velocity{0.0f};
        if (entity == entt::null || !readVec3Args(args, velocity)) return false;
        const uint32_t id = static_cast<uint32_t>(entity);
        if (m_setVelocity && HasPhysicsBody(id)) {
            m_setVelocity(id, velocity);
            m_fallbackVelocities.erase(id);
        } else {
            auto& fallback = m_fallbackVelocities[id];
            if (!fallback.hasGroundY && m_scene) {
                auto& registry = m_scene->Registry();
                if (registry.valid(entity) &&
                    registry.all_of<TransformComponent>(entity)) {
                    fallback.groundY =
                        registry.get<TransformComponent>(entity).position.y;
                    fallback.hasGroundY = true;
                }
            }
            fallback.value = velocity;
            fallback.frame = m_frame;
        }
        return true;
    };
    const auto destroyBody = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<RigidBodyComponent>(entity)) return false;
        auto& body = registry.get<RigidBodyComponent>(entity);
        if (body.bodyId == 0xFFFFFFFFu) return false;
        if (m_physics.destroyBody) m_physics.destroyBody(body.bodyId);
        body.bodyId = 0xFFFFFFFFu;
        return true;
    };

    physics["add_impulse"] = applyImpulse;
    physics["get_velocity"] = getVelocity;
    physics["velocity"] = getVelocity;
    physics["set_velocity"] = setVelocity;
    physics["destroy_body"] = destroyBody;
    m_lua.set_function("apply_impulse", applyImpulse);
    m_lua.set_function("get_velocity", getVelocity);
    m_lua.set_function("set_velocity", setVelocity);
    m_lua.set_function("new_physics_cube", [spawnPhysicsEntity](
                                                const std::string& name,
                                                float x, float y, float z,
                                                float size) {
        return spawnPhysicsEntity(name, 0, {x, y, z}, glm::vec3(size),
                                  0.0f, 0.0f, false);
    });

    // ------------------------------------------------------------------
    // Audio (miniaudio-backed by the host application).  Mixer bus values
    // live in Scene and are applied by the audio system each frame.
    // ------------------------------------------------------------------
    const auto playSound = [this](const std::string& path,
                                  sol::optional<bool> loop) {
        if (!m_playSound) {
            AddLog("[Lua] audio.play: no audio backend is bound");
            return false;
        }
        m_playSound(path, loop.value_or(false));
        return true;
    };
    const auto stopSound = [this](const std::string& path) {
        if (m_stopSound) m_stopSound(path);
    };
    const auto isSoundPlaying = [this](const std::string& path) {
        return m_isSoundPlaying ? m_isSoundPlaying(path) : false;
    };
    const auto pauseSound = [this](const std::string& path) {
        return m_pauseSound ? m_pauseSound(path) : false;
    };
    const auto resumeSound = [this](const std::string& path) {
        return m_resumeSound ? m_resumeSound(path) : false;
    };
    const auto setSoundVolume = [this](const std::string& path, float value) {
        return m_setSoundVolume ? m_setSoundVolume(path, value) : false;
    };
    const auto setSoundPitch = [this](const std::string& path, float value) {
        return m_setSoundPitch ? m_setSoundPitch(path, value) : false;
    };
    const auto setSoundLooping = [this](const std::string& path, bool value) {
        return m_setSoundLooping ? m_setSoundLooping(path, value) : false;
    };
    const auto seekSound = [this](const std::string& path, float value) {
        return m_seekSound ? m_seekSound(path, value) : false;
    };
    const auto soundDuration = [this](const std::string& path) {
        return m_soundDuration ? m_soundDuration(path) : 0.0f;
    };
    const auto soundPosition = [this](const std::string& path) {
        return m_soundPosition ? m_soundPosition(path) : 0.0f;
    };
    const auto stopAllSounds = [this]() {
        if (m_stopAllSounds) m_stopAllSounds();
    };
    const auto setBus = [this](const std::string& name, float volume,
                               sol::optional<bool> muted) {
        if (!m_scene) return false;
        const int index = AudioBusIndexFromName(name);
        if (index < 0 || index >= kAudioBusCount) return false;
        auto& bus = m_scene->AudioBuses()[static_cast<size_t>(index)];
        bus.volume = std::clamp(volume, 0.0f, 1.0f);
        if (muted) bus.muted = *muted;
        return true;
    };
    const auto getBus = [this](const std::string& name) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        const int index = AudioBusIndexFromName(name);
        if (index < 0 || index >= kAudioBusCount) {
            return sol::make_object(m_lua, sol::nil);
        }
        const auto& bus = m_scene->AudioBuses()[static_cast<size_t>(index)];
        sol::table table = m_lua.create_table();
        table["name"] = AudioBusName(static_cast<AudioBus>(index));
        table["volume"] = bus.volume;
        table["muted"] = bus.muted;
        return sol::make_object(m_lua, table);
    };

    audio["play"] = playSound;
    audio["stop"] = stopSound;
    audio["is_playing"] = isSoundPlaying;
    audio["pause"] = pauseSound;
    audio["resume"] = resumeSound;
    audio["set_volume"] = setSoundVolume;
    audio["set_pitch"] = setSoundPitch;
    audio["set_looping"] = setSoundLooping;
    audio["seek"] = seekSound;
    audio["duration"] = soundDuration;
    audio["position"] = soundPosition;
    audio["stop_all"] = stopAllSounds;
    audio["set_bus"] = setBus;
    audio["get_bus"] = getBus;
    m_lua.set_function("play_sound", [this](const std::string& path) {
        if (!m_playSound) {
            AddLog("[Lua] play_sound: no audio backend is bound");
            return false;
        }
        m_playSound(path, false);
        return true;
    });
    m_lua.set_function("stop_sound", stopSound);
    m_lua.set_function("is_sound_playing", isSoundPlaying);

    // ------------------------------------------------------------------
    // Animation state machines.
    // ------------------------------------------------------------------
    auto findAnimationMachine = [this](entt::entity entity)
        -> AnimationStateMachineComponent* {
        if (!m_scene) return nullptr;
        auto& registry = m_scene->Registry();
        int depth = 0;
        while (registry.valid(entity) && depth++ < 128) {
            if (registry.all_of<AnimationStateMachineComponent>(entity)) {
                return &registry.get<AnimationStateMachineComponent>(entity);
            }
            if (!registry.all_of<ParentComponent>(entity)) break;
            entity = registry.get<ParentComponent>(entity).parent;
        }
        return nullptr;
    };

    const auto setAnimationState = [resolve, findAnimationMachine](
                                       sol::object value,
                                       const std::string& state) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::SetState(*machine, state);
            return true;
        }
        return false;
    };
    const auto setAnimationParameter = [resolve, findAnimationMachine](
                                           sol::object value,
                                           const std::string& name,
                                           float parameter) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::SetParameter(*machine, name, parameter);
            return true;
        }
        return false;
    };
    const auto getAnimationParameter = [resolve, findAnimationMachine](
                                           sol::object value,
                                           const std::string& name,
                                           sol::optional<float> fallback) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            return AnimationSystem::GetParameter(*machine, name,
                                                 fallback.value_or(0.0f));
        }
        return fallback.value_or(0.0f);
    };
    const auto triggerAnimation = [resolve, findAnimationMachine](
                                      sol::object value,
                                      const std::string& name) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::Trigger(*machine, name);
            return true;
        }
        return false;
    };

    animation["set_state"] = setAnimationState;
    animation["set_parameter"] = setAnimationParameter;
    animation["get_parameter"] = getAnimationParameter;
    animation["trigger"] = triggerAnimation;
    m_lua.set_function("set_animation_state", setAnimationState);
    m_lua.set_function("set_animation_parameter", setAnimationParameter);
    m_lua.set_function("get_animation_parameter", getAnimationParameter);
    m_lua.set_function("trigger_animation", triggerAnimation);

    // ------------------------------------------------------------------
    // Skeletal model assets and playback.
    //
    // Galeshi.animation.model(path) returns a lightweight model object that
    // remembers the source file and clip list.  Spawn/apply attach the full
    // imported mesh hierarchy and SkeletalComponent to the scene; play passes
    // the model and a clip directly to the animation system.
    // ------------------------------------------------------------------
    auto skeletalPath = [](const sol::object& value) -> std::string {
        if (value.get_type() == sol::type::string) {
            return value.as<std::string>();
        }
        if (value.get_type() == sol::type::table) {
            sol::table table = value.as<sol::table>();
            const sol::object path = table.get<sol::object>("path");
            if (path.get_type() == sol::type::string) {
                return path.as<std::string>();
            }
        }
        return {};
    };

    auto makeSkeletalModel = [this](const std::string& path) -> sol::object {
        if (!m_assets) {
            AddLog("[Lua] animation.model: no AssetManager is bound");
            return sol::make_object(m_lua, sol::nil);
        }
        if (path.empty()) return sol::make_object(m_lua, sol::nil);

        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty() && !loaded.hasAnimation) {
            AddLog("[Lua] animation.model: could not load '" + path + "'");
            return sol::make_object(m_lua, sol::nil);
        }

        const std::string relativePath = m_assets->MakeRelativePath(path);
        sol::table model = m_lua.create_table();
        model["path"] = relativePath;
        model["name"] = std::filesystem::path(path).stem().string();
        model["valid"] = true;
        model["has_animation"] = loaded.hasAnimation;
        model["mesh_count"] = static_cast<int>(loaded.meshes.size());
        model["bone_count"] = loaded.totalBones;
        model["num_clips"] = static_cast<int>(loaded.animationNames.size());

        sol::table clips = m_lua.create_table();
        sol::table durations = m_lua.create_table();
        for (size_t i = 0; i < loaded.animationNames.size(); ++i) {
            clips[i + 1] = loaded.animationNames[i];
        }
        for (size_t i = 0; i < loaded.animationDurations.size(); ++i) {
            durations[i + 1] = loaded.animationDurations[i];
        }
        model["clips"] = clips;
        model["durations"] = durations;
        model[sol::metatable_key] = m_skeletalMeta;
        return sol::make_object(m_lua, model);
    };

    auto spawnSkeletalModel = [this, makeEntity](
                                  const std::string& path,
                                  const std::string& requestedName,
                                  const glm::vec3& position) -> sol::object {
        if (!m_assets || !m_scene) {
            AddLog("[Lua] animation.spawn: no AssetManager or Scene is bound");
            return sol::make_object(m_lua, sol::nil);
        }
        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty()) {
            AddLog("[Lua] animation.spawn: model has no meshes: " + path);
            return sol::make_object(m_lua, sol::nil);
        }

        const std::string source = m_assets->MakeRelativePath(path);
        const std::string stem = std::filesystem::path(path).stem().string();
        const std::string rootName = requestedName.empty()
            ? (stem.empty() ? std::string("SkeletalModel") : stem)
            : requestedName;

        Entity root = m_scene->CreateEntity(rootName);
        root.AddComponent<LuaEntityComponent>();
        auto& transform = root.GetComponent<TransformComponent>();
        transform.position = position;

        if (loaded.hasAnimation || loaded.totalBones > 0) {
            auto& skeleton = root.AddComponent<SkeletalComponent>();
            skeleton.isAnimated = loaded.hasAnimation;
            skeleton.sourcePath = source;
            skeleton.boneMatrices.assign(256, glm::mat4(1.0f));
            if (loaded.hasAnimation) {
                auto& machine =
                    root.AddComponent<AnimationStateMachineComponent>();
                machine = AnimationSystem::CreateDefaultStateMachine(
                    loaded.animationNames);
            }
        }

        for (size_t i = 0; i < loaded.meshes.size(); ++i) {
            Entity meshEntity = m_scene->CreateEntity(
                rootName + "_" + std::to_string(i));
            meshEntity.AddComponent<LuaEntityComponent>();
            meshEntity.AddComponent<ParentComponent>(
                ParentComponent{static_cast<entt::entity>(root)});
            meshEntity.AddComponent<MeshComponent>(MeshComponent{
                loaded.meshes[i], source, "", static_cast<int>(i)});

            std::shared_ptr<Material> material;
            if (i < loaded.meshMaterialIndex.size()) {
                const int materialIndex = loaded.meshMaterialIndex[i];
                if (materialIndex >= 0 &&
                    materialIndex < static_cast<int>(loaded.materials.size())) {
                    material = loaded.materials[static_cast<size_t>(materialIndex)];
                }
            }
            if (!material && !loaded.materials.empty()) {
                material = loaded.materials.front();
            }
            if (!material) material = std::make_shared<Material>();
            meshEntity.AddComponent<MaterialComponent>(
                MaterialComponent{material});
        }

        spdlog::info("[Lua] Spawned skeletal model '{}' ({} meshes, {} clips)",
                     source, loaded.meshes.size(), loaded.animationNames.size());
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(root)));
    };

    auto applySkeletalModel = [this](entt::entity entity,
                                     const std::string& path) -> bool {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty() && !loaded.hasAnimation) return false;

        auto& registry = m_scene->Registry();
        SkeletalComponent* skeleton = nullptr;
        if (registry.all_of<SkeletalComponent>(entity)) {
            skeleton = &registry.get<SkeletalComponent>(entity);
        } else {
            skeleton = &registry.emplace<SkeletalComponent>(entity);
        }
        skeleton->isAnimated = loaded.hasAnimation;
        skeleton->sourcePath = m_assets->MakeRelativePath(path);
        skeleton->currentTime = 0.0f;
        skeleton->activeAnimation = 0;
        skeleton->boneMatrices.assign(256, glm::mat4(1.0f));

        if (loaded.hasAnimation) {
            if (!registry.all_of<AnimationStateMachineComponent>(entity)) {
                auto& machine =
                    registry.emplace<AnimationStateMachineComponent>(entity);
                machine = AnimationSystem::CreateDefaultStateMachine(
                    loaded.animationNames);
            } else {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                if (machine.states.empty()) {
                    machine = AnimationSystem::CreateDefaultStateMachine(
                        loaded.animationNames);
                }
            }
        }
        return true;
    };

    auto resolveSkeletalClip = [this](const std::string& path,
                                      const sol::object& value,
                                      int& outClip) -> bool {
        if (!m_assets) return false;
        auto loaded = m_assets->LoadModel(path);
        if (!loaded.hasAnimation || loaded.animationNames.empty()) return false;

        int index = 0; // Lua-facing clip indices are 1-based.
        if (value.get_type() == sol::type::number) {
            index = static_cast<int>(value.as<double>()) - 1;
        } else if (value.get_type() == sol::type::string) {
            const std::string name = value.as<std::string>();
            bool found = false;
            for (size_t i = 0; i < loaded.animationNames.size(); ++i) {
                if (loaded.animationNames[i] == name) {
                    index = static_cast<int>(i);
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        index = std::clamp(index, 0,
                           static_cast<int>(loaded.animationNames.size()) - 1);
        outClip = index;
        return true;
    };

    auto playSkeletalModel = [this, applySkeletalModel, resolveSkeletalClip](
                                 entt::entity entity,
                                 const std::string& path,
                                 const sol::object& clipValue) -> bool {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        if (!applySkeletalModel(entity, path)) return false;

        int clip = 0;
        if (!resolveSkeletalClip(path, clipValue, clip)) return false;

        auto& registry = m_scene->Registry();
        auto* skeleton = registry.try_get<SkeletalComponent>(entity);
        if (!skeleton) return false;
        skeleton->isAnimated = true;
        skeleton->activeAnimation = clip;
        skeleton->currentTime = 0.0f;

        if (auto* machine =
                registry.try_get<AnimationStateMachineComponent>(entity)) {
            int stateIndex = -1;
            for (size_t i = 0; i < machine->states.size(); ++i) {
                if (machine->states[i].clip == clip) {
                    stateIndex = static_cast<int>(i);
                    break;
                }
            }
            if (stateIndex >= 0) {
                machine->enabled = true;
                AnimationSystem::SetState(*machine,
                                          machine->states[stateIndex].name);
            } else {
                // Custom state machines may not have a state for this clip.
                // Fall back to direct clip playback.
                machine->enabled = false;
            }
        }
        return true;
    };

    auto playSkeletalClip = [this, playSkeletalModel](entt::entity entity,
                                                      const sol::object& clipValue) {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        if (!skeleton || skeleton->sourcePath.empty()) return false;
        return playSkeletalModel(entity, skeleton->sourcePath, clipValue);
    };

    // Shared metatable for model objects returned by animation.model().
    m_skeletalMeta = m_lua.create_table();
    sol::table modelMethods = m_lua.create_table();
    modelMethods["clip_count"] = [](sol::table self) -> int {
        const sol::object count = self.get<sol::object>("num_clips");
        return count.get_type() == sol::type::number
            ? static_cast<int>(count.as<double>())
            : 0;
    };
    modelMethods["clip_name"] = [](sol::table self, int index) -> std::string {
        const sol::object clipsValue = self.get<sol::object>("clips");
        if (clipsValue.get_type() != sol::type::table) return {};
        sol::table clips = clipsValue.as<sol::table>();
        const sol::object name = clips.get<sol::object>(index);
        return name.get_type() == sol::type::string
            ? name.as<std::string>()
            : std::string{};
    };
    modelMethods["duration"] = [](sol::table self, int index) -> float {
        const sol::object durationsValue = self.get<sol::object>("durations");
        if (durationsValue.get_type() != sol::type::table) return 0.0f;
        sol::table durations = durationsValue.as<sol::table>();
        const sol::object value = durations.get<sol::object>(index);
        return value.get_type() == sol::type::number
            ? static_cast<float>(value.as<double>())
            : 0.0f;
    };
    modelMethods["spawn"] = [this, skeletalPath, spawnSkeletalModel](
                                sol::table self,
                                sol::optional<std::string> name,
                                sol::optional<float> x,
                                sol::optional<float> y,
                                sol::optional<float> z) -> sol::object {
        return spawnSkeletalModel(skeletalPath(sol::object(self)),
                                  name.value_or(""),
                                  {x.value_or(0.0f), y.value_or(0.0f),
                                   z.value_or(0.0f)});
    };
    modelMethods["apply"] = [this, resolve, skeletalPath, applySkeletalModel](
                                sol::table self, sol::object entity) {
        return applySkeletalModel(resolve(entity),
                                  skeletalPath(sol::object(self)));
    };
    modelMethods["play"] = [this, resolve, skeletalPath, playSkeletalModel](
                               sol::table self, sol::object entity,
                               sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalModel(resolve(entity), skeletalPath(sol::object(self)),
                                 clip);
    };
    m_skeletalMeta[sol::meta_function::index] = modelMethods;
    m_skeletalMeta[sol::meta_function::to_string] =
        [](sol::table self) -> std::string {
            const sol::object path = self.get<sol::object>("path");
            const sol::object count = self.get<sol::object>("num_clips");
            return std::string("SkeletalModel(") +
                   (path.get_type() == sol::type::string
                        ? path.as<std::string>()
                        : std::string("?")) +
                   ", clips=" +
                   std::to_string(count.get_type() == sol::type::number
                                      ? static_cast<int>(count.as<double>())
                                      : 0) + ")";
        };

    animation["model"] = [makeSkeletalModel](const std::string& path) {
        return makeSkeletalModel(path);
    };
    animation["load"] = [makeSkeletalModel](const std::string& path) {
        return makeSkeletalModel(path);
    };
    animation["spawn"] = [this, skeletalPath, spawnSkeletalModel](
                             sol::object modelValue,
                             sol::optional<std::string> name,
                             sol::optional<float> x,
                             sol::optional<float> y,
                             sol::optional<float> z) -> sol::object {
        const std::string path = skeletalPath(modelValue);
        if (path.empty()) return sol::make_object(m_lua, sol::nil);
        return spawnSkeletalModel(path, name.value_or(""),
                                  {x.value_or(0.0f), y.value_or(0.0f),
                                   z.value_or(0.0f)});
    };
    animation["apply"] = [this, resolve, skeletalPath, applySkeletalModel](
                             sol::object entity, sol::object modelValue) {
        return applySkeletalModel(resolve(entity), skeletalPath(modelValue));
    };
    animation["play"] = [this, resolve, skeletalPath, playSkeletalModel](
                            sol::object entity, sol::object modelValue,
                            sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalModel(resolve(entity), skeletalPath(modelValue),
                                 clip);
    };
    animation["play_clip"] = [this, resolve, playSkeletalClip](
                                 sol::object entity,
                                 sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalClip(resolve(entity), clip);
    };
    animation["stop"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null || !m_scene) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        if (!skeleton) return false;
        skeleton->isAnimated = false;
        return true;
    };
    animation["is_playing"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null || !m_scene) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        return skeleton && skeleton->isAnimated;
    };

    // Legacy aliases for simple scripts.
    m_lua.set_function("load_skeletal_model", [makeSkeletalModel](
                                                   const std::string& path) {
        return makeSkeletalModel(path);
    });


}

} // namespace Galeshi
