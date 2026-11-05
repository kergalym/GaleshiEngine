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

void ScriptEngine::BuildApiCore(ScriptApiBuildContext& context) {
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

    // Build the shared entity metatable before any handle can be returned.
    sol::table methods = m_lua.create_table();
    m_entityMeta = m_lua.create_table();

    methods["valid"] = [resolveTable](sol::table self) {
        return resolveTable(self) != entt::entity{entt::null};
    };
    methods["get_id"] = [idFromTable](sol::table self) {
        return idFromTable(self);
    };
    methods["name"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return std::string{};
        auto& registry = m_scene->Registry();
        if (!registry.all_of<NameComponent>(entity)) return std::string{};
        return registry.get<NameComponent>(entity).name;
    };
    methods["set_name"] = [this, resolveTable](sol::table self,
                                                const std::string& name) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return self;
        auto& registry = m_scene->Registry();
        if (registry.all_of<NameComponent>(entity)) {
            registry.get<NameComponent>(entity).name = name;
        } else {
            registry.emplace<NameComponent>(entity, NameComponent{name});
        }
        return self;
    };
    methods["position"] = [this, resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->position : glm::vec3(0.0f));
    };
    methods["set_position"] = [resolveTable, getTransform, readVec3Args](
                                  sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 position{0.0f};
        if (entity == entt::null || !readVec3Args(args, position)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position = position;
        }
        return self;
    };
    methods["translate"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 delta{0.0f};
        if (entity == entt::null || !readVec3Args(args, delta)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position += delta;
        }
        return self;
    };
    methods["rotation"] = [this, resolveTable, getTransform, makeQuat](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeQuat(transform ? transform->rotation
                                  : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    };
    methods["set_rotation"] = [resolveTable, getTransform, readQuat, readNumber](
                                  sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return self;
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        bool ok = false;
        if (args.size() == 1) {
            ok = readQuat(args[0], rotation);
        } else if (args.size() >= 4) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
            ok = readNumber(args[0], x) && readNumber(args[1], y) &&
                 readNumber(args[2], z) && readNumber(args[3], w);
            if (ok) rotation = glm::quat(w, x, y, z);
        } else if (args.size() >= 3) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            ok = readNumber(args[0], x) && readNumber(args[1], y) &&
                 readNumber(args[2], z);
            if (ok) rotation = glm::quat(glm::radians(glm::vec3{x, y, z}));
        }
        if (!ok) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->rotation = glm::normalize(rotation);
        }
        return self;
    };
    methods["euler"] = [this, resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        if (!transform) return makeVec3(glm::vec3(0.0f));
        return makeVec3(glm::degrees(glm::eulerAngles(transform->rotation)));
    };
    methods["set_euler"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 euler{0.0f};
        if (entity == entt::null || !readVec3Args(args, euler)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->rotation = glm::normalize(
                glm::quat(glm::radians(euler)));
        }
        return self;
    };
    methods["scale"] = [resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->scale : glm::vec3(1.0f));
    };
    methods["set_scale"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 scale{1.0f};
        if (entity == entt::null || !readVec3Args(args, scale)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->scale = scale;
        }
        return self;
    };
    methods["scale_by"] = [resolveTable, getTransform, readVec3Args](
                              sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 scale{1.0f};
        if (entity == entt::null || !readVec3Args(args, scale)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->scale *= scale;
        }
        return self;
    };
    methods["parent"] = [this, resolveTable, makeEntity](sol::table self) -> sol::object {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return sol::make_object(m_lua, sol::nil);
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParentComponent>(entity)) {
            return sol::make_object(m_lua, sol::nil);
        }
        const entt::entity parent = registry.get<ParentComponent>(entity).parent;
        if (parent == entt::null || !registry.valid(parent)) {
            return sol::make_object(m_lua, sol::nil);
        }
        return makeEntity(static_cast<uint32_t>(parent));
    };
    methods["set_parent"] = [this, resolveTable, resolve](sol::table self,
                                                          sol::object parentValue) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        if (parentValue.get_type() == sol::type::nil ||
            parentValue.get_type() == sol::type::none) {
            auto& registry = m_scene->Registry();
            if (registry.all_of<ParentComponent>(entity)) {
                registry.remove<ParentComponent>(entity);
            }
            return true;
        }
        const entt::entity parent = resolve(parentValue);
        if (parent == entt::null || parent == entity) return false;
        auto& registry = m_scene->Registry();
        entt::entity cursor = parent;
        for (int depth = 0; depth < 256 && cursor != entt::null; ++depth) {
            if (cursor == entity) return false;
            if (!registry.all_of<ParentComponent>(cursor)) break;
            cursor = registry.get<ParentComponent>(cursor).parent;
        }
        registry.emplace_or_replace<ParentComponent>(entity,
                                                     ParentComponent{parent});
        return true;
    };
    methods["detach"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParentComponent>(entity)) return false;
        registry.remove<ParentComponent>(entity);
        return true;
    };
    methods["children"] = [this, resolveTable, makeEntityTable](sol::table self) {
        sol::table result = m_lua.create_table();
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return result;
        int index = 1;
        auto view = m_scene->Registry().view<ParentComponent>();
        for (entt::entity child : view) {
            if (view.get<ParentComponent>(child).parent == entity) {
                result[index++] = makeEntityTable(static_cast<uint32_t>(child));
            }
        }
        return result;
    };
    methods["destroy"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    };
    methods["has_component"] = [this, resolveTable](sol::table self,
                                                     const std::string& name) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null || !m_scene) return false;
        auto& registry = m_scene->Registry();
        const std::string key = NormalizeKey(name);
        if (key == "name") return registry.all_of<NameComponent>(entity);
        if (key == "transform") return registry.all_of<TransformComponent>(entity);
        if (key == "parent") return registry.all_of<ParentComponent>(entity);
        if (key == "mesh") return registry.all_of<MeshComponent>(entity);
        if (key == "material") return registry.all_of<MaterialComponent>(entity);
        if (key == "skeletal") return registry.all_of<SkeletalComponent>(entity);
        if (key == "camera") return registry.all_of<CameraComponent>(entity);
        if (key == "directional_light") return registry.all_of<DirectionalLightComponent>(entity);
        if (key == "point_light") return registry.all_of<PointLightComponent>(entity);
        if (key == "spot_light") return registry.all_of<SpotLightComponent>(entity);
        if (key == "rigid_body" || key == "rigidbody") return registry.all_of<RigidBodyComponent>(entity);
        if (key == "collider") return registry.all_of<ColliderComponent>(entity);
        if (key == "landscape") return registry.all_of<LandscapeComponent>(entity);
        if (key == "vegetation") return registry.all_of<VegetationComponent>(entity);
        if (key == "nav_agent") return registry.all_of<NavAgentComponent>(entity);
        if (key == "soft_body") return registry.all_of<SoftBodyComponent>(entity);
        if (key == "ai_controller") return registry.all_of<AIControllerComponent>(entity);
        if (key == "player_tag") return registry.all_of<PlayerTagComponent>(entity);
        if (key == "perception") return registry.all_of<PerceptionComponent>(entity);
        if (key == "behavior_tree") return registry.all_of<BehaviorTreeComponent>(entity);
        if (key == "audio_source") return registry.all_of<AudioSourceComponent>(entity);
        if (key == "audio_listener") return registry.all_of<AudioListenerComponent>(entity);
        if (key == "post_process_volume") return registry.all_of<PostProcessVolumeComponent>(entity);
        if (key == "particle_emitter") return registry.all_of<ParticleEmitterComponent>(entity);
        if (key == "water") return registry.all_of<WaterComponent>(entity);
        if (key == "groom" || key == "groom_hair") return registry.all_of<GroomComponent>(entity);
        if (key == "ui_canvas") return registry.all_of<UICanvasComponent>(entity);
        if (key == "animation_state_machine") return registry.all_of<AnimationStateMachineComponent>(entity);
        if (key == "lua_entity") return registry.all_of<LuaEntityComponent>(entity);
        return false;
    };

    m_entityMeta[sol::meta_function::index] = methods;
    m_entityMeta[sol::meta_function::to_string] = [this, idFromTable](sol::table self) {
        const uint32_t id = idFromTable(self);
        if (!m_scene || !m_scene->Registry().valid(static_cast<entt::entity>(id))) {
            return std::string("Entity(invalid)");
        }
        auto& registry = m_scene->Registry();
        if (registry.all_of<NameComponent>(static_cast<entt::entity>(id))) {
            return std::string("Entity(") +
                   registry.get<NameComponent>(static_cast<entt::entity>(id)).name +
                   ", id=" + std::to_string(id) + ")";
        }
        return std::string("Entity(id=") + std::to_string(id) + ")";
    };
    m_entityMeta[sol::meta_function::equal_to] = [idFromTable](sol::table lhs,
                                                                sol::table rhs) {
        return idFromTable(lhs) == idFromTable(rhs);
    };

    auto createEntityId = [this](const std::string& name) -> uint32_t {
        if (!m_scene) return static_cast<uint32_t>(entt::null);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        return static_cast<uint32_t>(static_cast<entt::entity>(entity));
    };

    // ------------------------------------------------------------------
    // World / ECS
    // ------------------------------------------------------------------
    world["create"] = [createEntityId, makeEntity](const std::string& name) {
        return makeEntity(createEntityId(name));
    };
    world["entity"] = [makeEntity, idFromValue](sol::object value) {
        return makeEntity(idFromValue(value));
    };
    world["find"] = [this, makeEntity](const std::string& name) {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->FindEntityByName(name);
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };
    world["find_all"] = [this, makeEntityTable](sol::optional<std::string> name) {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<NameComponent>();
        for (entt::entity entity : view) {
            if (name && view.get<NameComponent>(entity).name != *name) continue;
            result[index++] = makeEntityTable(static_cast<uint32_t>(entity));
        }
        return result;
    };
    world["all"] = [this, makeEntityTable](sol::optional<std::string> name) {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<NameComponent>();
        for (entt::entity entity : view) {
            if (name && view.get<NameComponent>(entity).name != *name) continue;
            result[index++] = makeEntityTable(static_cast<uint32_t>(entity));
        }
        return result;
    };
    world["exists"] = [resolve](sol::object value) {
        return resolve(value) != entt::entity{entt::null};
    };
    world["destroy"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    };
    world["count"] = [this]() -> size_t {
        if (!m_scene) return 0;
        return m_scene->Registry().view<NameComponent>().size();
    };
    world["clear_script_entities"] = [this]() {
        ClearLuaEntities();
    };

    // Legacy ECS aliases.
    m_lua.set_function("new_entity", [createEntityId](const std::string& name) {
        return createEntityId(name);
    });
    m_lua.set_function("set_position", [resolve, getTransform, readVec3Args](
                                          sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 position{0.0f};
        if (entity == entt::null || !readVec3Args(args, position)) return;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position = position;
        }
    });
    m_lua.set_function("get_position", [resolve, getTransform, makeVec3](
                                          sol::object value) {
        const entt::entity entity = resolve(value);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->position : glm::vec3(0.0f));
    });
    m_lua.set_function("translate", [resolve, getTransform, readVec3Args](
                                        sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 delta{0.0f};
        if (entity == entt::null || !readVec3Args(args, delta)) return;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position += delta;
        }
    });
    m_lua.set_function("destroy_entity", [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    });

    // ------------------------------------------------------------------
    // Input (SDL3-backed by the host application).
    // ------------------------------------------------------------------
    const auto keyDown = [this](const std::string& key) {
        return m_isKeyDown ? m_isKeyDown(key) : false;
    };
    const auto axis = [this](const std::string& name) {
        return m_getAxis ? m_getAxis(name) : 0.0f;
    };
    const auto axisRaw = [this](const std::string& name) {
        return m_getAxisRaw ? m_getAxisRaw(name) : 0.0f;
    };
    const auto mouseDown = [this](const std::string& button) {
        return m_isMouseButtonDown ? m_isMouseButtonDown(button) : false;
    };
    const auto mousePosition = [this, makeVec2]() {
        return makeVec2(m_getMousePosition ? m_getMousePosition() : glm::vec2(0.0f));
    };
    const auto mouseDelta = [this, makeVec2]() {
        return makeVec2(m_getMouseDelta ? m_getMouseDelta() : glm::vec2(0.0f));
    };
    const auto mouseWheel = [this]() {
        return m_getMouseWheel ? m_getMouseWheel() : 0.0f;
    };

    input["key_down"] = keyDown;
    input["axis"] = axis;
    input["axis_raw"] = axisRaw;
    input["mouse_down"] = mouseDown;
    input["mouse_position"] = mousePosition;
    input["mouse_delta"] = mouseDelta;
    input["mouse_wheel"] = mouseWheel;
    m_lua.set_function("is_key_down", keyDown);
    m_lua.set_function("get_axis", axis);
    m_lua.set_function("get_axis_raw", axisRaw);
    m_lua.set_function("is_mouse_button_down", mouseDown);
    m_lua.set_function("get_mouse_position", mousePosition);
    m_lua.set_function("get_mouse_delta", mouseDelta);
    m_lua.set_function("get_mouse_wheel", mouseWheel);


}

} // namespace Galeshi
