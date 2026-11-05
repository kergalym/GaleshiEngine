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

void ScriptEngine::BuildApiRuntime(ScriptApiBuildContext& context) {
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
    auto& logFn = context.logFn;
    auto& warnFn = context.warnFn;
    auto& errorFn = context.errorFn;
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
    // Particles.
    // ------------------------------------------------------------------
    auto applyParticleProps = [readNumber, readBool, readVec3, readVec4](
                                  ParticleEmitterComponent& emitter,
                                  const sol::table& props) {
        bool boolean = false;
        float number = 0.0f;
        glm::vec3 vector3{0.0f};
        glm::vec4 color{1.0f};

        if (readBool(props.get<sol::object>("enabled"), boolean)) emitter.enabled = boolean;
        if (readNumber(props.get<sol::object>("emit_rate"), number)) emitter.emitRate = number;
        if (readNumber(props.get<sol::object>("lifetime"), number)) emitter.lifetime = number;
        if (readNumber(props.get<sol::object>("lifetime_variance"), number)) {
            emitter.lifetimeVariance = number;
        }
        if (readNumber(props.get<sol::object>("start_size"), number)) emitter.startSize = number;
        if (readNumber(props.get<sol::object>("end_size"), number)) emitter.endSize = number;
        if (readNumber(props.get<sol::object>("velocity_variance"), number)) {
            emitter.velocityVariance = number;
        }
        if (readNumber(props.get<sol::object>("spawn_radius"), number)) emitter.spawnRadius = number;
        if (readNumber(props.get<sol::object>("drag"), number)) emitter.drag = number;
        if (readNumber(props.get<sol::object>("max_particles"), number)) {
            emitter.maxParticles = static_cast<uint32_t>(
                std::max(0.0f, number));
        }
        if (readVec3(props.get<sol::object>("velocity"), vector3)) emitter.velocity = vector3;
        if (readVec3(props.get<sol::object>("spawn_box"), vector3)) emitter.spawnBoxSize = vector3;
        if (readVec3(props.get<sol::object>("gravity"), vector3)) emitter.gravity = vector3;
        if (readVec4(props.get<sol::object>("start_color"), color)) emitter.startColor = color;
        if (readVec4(props.get<sol::object>("end_color"), color)) emitter.endColor = color;
    };

    auto particleToTable = [this, makeVec3, makeVec4](const ParticleEmitterComponent& emitter) {
        sol::table table = m_lua.create_table();
        table["enabled"] = emitter.enabled;
        table["emit_rate"] = emitter.emitRate;
        table["lifetime"] = emitter.lifetime;
        table["lifetime_variance"] = emitter.lifetimeVariance;
        table["start_size"] = emitter.startSize;
        table["end_size"] = emitter.endSize;
        table["velocity"] = makeVec3(emitter.velocity);
        table["velocity_variance"] = emitter.velocityVariance;
        table["spawn_radius"] = emitter.spawnRadius;
        table["spawn_box"] = makeVec3(emitter.spawnBoxSize);
        table["gravity"] = makeVec3(emitter.gravity);
        table["drag"] = emitter.drag;
        table["max_particles"] = static_cast<double>(emitter.maxParticles);
        table["start_color"] = makeVec4(emitter.startColor);
        table["end_color"] = makeVec4(emitter.endColor);
        return table;
    };

    particles["create"] = [this, makeEntity, applyParticleProps](
                              const std::string& name, float x, float y, float z,
                              sol::optional<sol::table> props) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        auto& transform = entity.AddComponent<TransformComponent>();
        transform.position = {x, y, z};
        auto& emitter = entity.AddComponent<ParticleEmitterComponent>();
        if (props) applyParticleProps(emitter, *props);
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };
    particles["set"] = [this, resolve, applyParticleProps](sol::object value,
                                                           sol::table props) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParticleEmitterComponent>(entity)) return false;
        applyParticleProps(registry.get<ParticleEmitterComponent>(entity), props);
        return true;
    };
    particles["get"] = [this, resolve, particleToTable](sol::object value) -> sol::object {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return sol::make_object(m_lua, sol::nil);
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParticleEmitterComponent>(entity)) {
            return sol::make_object(m_lua, sol::nil);
        }
        return sol::make_object(m_lua,
                                particleToTable(registry.get<ParticleEmitterComponent>(entity)));
    };

    // ------------------------------------------------------------------
    // Engine / lifecycle.
    // ------------------------------------------------------------------
    engine["version"] = "0.3.0";
    engine["time"] = [this]() { return m_time; };
    engine["delta_time"] = [this]() { return static_cast<double>(m_deltaTime); };
    engine["frame"] = [this]() { return static_cast<double>(m_frame); };
    engine["quit"] = [this]() {
        if (m_requestExit) {
            m_requestExit();
            return true;
        }
        return false;
    };
    engine["log"] = logFn;
    engine["warn"] = warnFn;
    engine["error"] = errorFn;

    // ------------------------------------------------------------------
    // Gameplay camera mode. The host owns the actual camera; these calls
    // only switch between the player's third- and first-person views.
    // ------------------------------------------------------------------
    auto setCameraMode = [this](sol::object value) -> bool {
        int mode = -1;
        if (value.get_type() == sol::type::number) {
            const int numeric = value.as<int>();
            if (numeric == GALESHI_CAMERA_THIRD_PERSON ||
                numeric == GALESHI_CAMERA_FIRST_PERSON) {
                mode = numeric;
            }
        } else if (value.get_type() == sol::type::string) {
            const std::string name = NormalizeKey(value.as<std::string>());
            if (name == "third" || name == "third_person" ||
                name == "thirdperson") {
                mode = GALESHI_CAMERA_THIRD_PERSON;
            } else if (name == "first" || name == "first_person" ||
                       name == "firstperson") {
                mode = GALESHI_CAMERA_FIRST_PERSON;
            }
        }
        if (!m_setCameraMode || mode < 0) return false;
        return m_setCameraMode(mode);
    };
    const auto getCameraMode = [this]() -> int {
        return m_getCameraMode
            ? m_getCameraMode()
            : static_cast<int>(GALESHI_CAMERA_THIRD_PERSON);
    };
    const auto cameraForward = [this, makeVec3]() {
        return makeVec3(m_getCameraForward
            ? m_getCameraForward()
            : glm::vec3(0.0f, 0.0f, -1.0f));
    };
    const auto cameraRight = [this, makeVec3]() {
        return makeVec3(m_getCameraRight
            ? m_getCameraRight()
            : glm::vec3(1.0f, 0.0f, 0.0f));
    };
    const auto cameraMovement =
        [this, makeVec3](float horizontal, float vertical) {
            const glm::vec3 forward = m_getCameraForward
                ? m_getCameraForward()
                : glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 right = m_getCameraRight
                ? m_getCameraRight()
                : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 movement = forward * vertical + right * horizontal;
            const float length = glm::length(movement);
            if (length > 1.0f) movement /= length;
            return makeVec3(movement);
        };
    camera["THIRD_PERSON"] =
        static_cast<int>(GALESHI_CAMERA_THIRD_PERSON);
    camera["FIRST_PERSON"] =
        static_cast<int>(GALESHI_CAMERA_FIRST_PERSON);
    camera["set_mode"] = setCameraMode;
    camera["mode"] = getCameraMode;
    camera["forward"] = cameraForward;
    camera["right"] = cameraRight;
    camera["movement"] = cameraMovement;
    m_lua.set_function("set_camera_mode", setCameraMode);
    m_lua.set_function("get_camera_mode", getCameraMode);
    m_lua.set_function("get_camera_forward", cameraForward);
    m_lua.set_function("get_camera_right", cameraRight);
    m_lua.set_function("get_camera_movement", cameraMovement);

    const auto playerStrafing = [this]() {
        return m_getPlayerStrafing ? m_getPlayerStrafing() : false;
    };
    const auto setPlayerStrafing = [this](bool enabled) {
        if (!m_setPlayerStrafing) return false;
        m_setPlayerStrafing(enabled);
        return true;
    };
    player["strafing"] = playerStrafing;
    player["set_strafing"] = setPlayerStrafing;
    m_lua.set_function("is_player_strafing", playerStrafing);
    m_lua.set_function("set_player_strafing", setPlayerStrafing);

    const auto compileShader = [this](const std::string& path) {
        if (!m_compileShader) {
            m_shaderError = "No shader compiler is bound by the host.";
            return false;
        }
        std::string error;
        const bool ok = m_compileShader(path, error);
        m_shaderError = ok ? std::string{} : error;
        return ok;
    };
    const auto shaderError = [this]() { return m_shaderError; };
    shader["compile"] = compileShader;
    shader["error"] = shaderError;
    m_lua.set_function("compile_shader", compileShader);
    m_lua.set_function("get_shader_error", shaderError);


}

} // namespace Galeshi
