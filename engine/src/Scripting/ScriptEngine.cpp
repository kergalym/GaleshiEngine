#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include <spdlog/spdlog.h>

namespace Galeshi {

ScriptEngine::ScriptEngine() {
    m_lua.open_libraries(sol::lib::base, sol::lib::math,
                         sol::lib::table, sol::lib::string);
    BindFunctions();
}

void ScriptEngine::AddLog(const std::string& msg) {
    m_logs.push_back(msg);
    if (m_logs.size() > 1000) m_logs.erase(m_logs.begin());
    spdlog::info("[Lua] {}", msg);
}

bool ScriptEngine::RunScript(const std::string& filepath) {
    try {
        m_lua.script_file(filepath);
        AddLog("Ran script: " + filepath);
        return true;
    } catch (const sol::error& e) {
        AddLog(std::string("ERROR: ") + e.what());
        return false;
    }
}

void ScriptEngine::BindFunctions() {
    m_lua.set_function("print", [this](sol::variadic_args va) {
        std::string out;
        for (auto v : va) {
            if (v.is<std::string>()) out += v.as<std::string>();
            else if (v.is<double>()) out += std::to_string(v.as<double>());
            else if (v.is<int>()) out += std::to_string(v.as<int>());
            else if (v.is<bool>()) out += v.as<bool>() ? "true" : "false";
            else out += "?";
            out += " ";
        }
        AddLog(out);
    });
    m_lua.set_function("log", [this](const std::string& msg) {
        AddLog(msg);
    });
}

void ScriptEngine::BindECS(Scene& scene) {
    m_scene = &scene;

    m_lua.set_function("new_entity", [this](const std::string& name) -> uint32_t {
        if (!m_scene) return static_cast<uint32_t>(entt::null);
        auto e = m_scene->CreateEntity(name);
        e.AddComponent<LuaEntityComponent>();
        return static_cast<uint32_t>(static_cast<entt::entity>(e));
    });

    m_lua.set_function("set_position", [this](uint32_t eid, float x, float y, float z) {
        if (!m_scene) return;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (!reg.valid(e)) return;
        if (reg.all_of<TransformComponent>(e)) reg.get<TransformComponent>(e).position = glm::vec3(x, y, z);
        else reg.emplace<TransformComponent>(e).position = glm::vec3(x, y, z);
    });

    m_lua.set_function("get_position", [this](uint32_t eid) -> sol::table {
        sol::table t = m_lua.create_table();
        t["x"] = 0.0f; t["y"] = 0.0f; t["z"] = 0.0f;
        if (!m_scene) return t;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (!reg.valid(e) || !reg.all_of<TransformComponent>(e)) return t;
        auto& tf = reg.get<TransformComponent>(e);
        t["x"] = tf.position.x; t["y"] = tf.position.y; t["z"] = tf.position.z;
        return t;
    });

    m_lua.set_function("translate", [this](uint32_t eid, float dx, float dy, float dz) {
        if (!m_scene) return;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (!reg.valid(e) || !reg.all_of<TransformComponent>(e)) return;
        reg.get<TransformComponent>(e).position += glm::vec3(dx, dy, dz);
    });

    m_lua.set_function("destroy_entity", [this](uint32_t eid) {
        if (!m_scene) return;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (reg.valid(e)) reg.destroy(e);
    });
}

void ScriptEngine::BindInput(std::function<bool(const std::string&)> isKeyDown) {
    m_lua.set_function("is_key_down", isKeyDown);
}

void ScriptEngine::BindVelocity(std::function<glm::vec3(uint32_t)> getVel, std::function<void(uint32_t, const glm::vec3&)> setVel) {
    m_lua.set_function("get_velocity", [this, getVel](uint32_t eid) -> sol::table {
        sol::table t = m_lua.create_table();
        glm::vec3 v = getVel(eid);
        t["x"] = v.x; t["y"] = v.y; t["z"] = v.z;
        return t;
    });
    m_lua.set_function("set_velocity", [setVel](uint32_t eid, float x, float y, float z) {
        setVel(eid, glm::vec3(x, y, z));
    });
}

void ScriptEngine::BindAudio(std::function<void(const std::string&)> playSound) {
    m_playSound = std::move(playSound);
    m_lua.set_function("play_sound", [this](const std::string& path) {
        if (m_playSound) m_playSound(path);
    });
}

void ScriptEngine::BindPhysics(std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)> createBox) {
    m_createBox = std::move(createBox);
    m_lua.set_function("new_physics_cube",
        [this](const std::string& name, float x, float y, float z, float size) -> uint32_t {
            if (!m_scene) return static_cast<uint32_t>(entt::null);
            auto e = m_scene->CreateEntity(name);
            e.AddComponent<LuaEntityComponent>();
            if (name == "Player") e.AddComponent<PlayerTagComponent>();
            auto& tf = e.AddComponent<TransformComponent>();
            tf.position = glm::vec3(x, y, z);
            tf.scale    = glm::vec3(size);
            auto& rb = e.AddComponent<RigidBodyComponent>();
            rb.motionType = RigidBodyComponent::MotionType::Dynamic;
            rb.mass = 1.0f;
            if (m_createBox) rb.bodyId = m_createBox(glm::vec3(size * 0.5f), glm::vec3(x, y, z), static_cast<entt::entity>(e));
            return static_cast<uint32_t>(static_cast<entt::entity>(e));
        });
}

void ScriptEngine::BindImpulse(std::function<void(uint32_t, const glm::vec3&)> applyImpulse) {
    m_lua.set_function("apply_impulse", [applyImpulse](uint32_t eid, float x, float y, float z) {
        applyImpulse(eid, glm::vec3(x, y, z));
    });
}

void ScriptEngine::BindAI() {
    m_lua.set_function("set_ai_target", [this](uint32_t eid, float x, float y, float z) {
        if (!m_scene) return;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (reg.valid(e) && reg.all_of<NavAgentComponent>(e)) {
            auto& agent = reg.get<NavAgentComponent>(e);
            agent.targetPosition = glm::vec3(x, y, z);
            agent.hasTarget = true;
        }
    });
    m_lua.set_function("is_path_done", [this](uint32_t eid) -> bool {
        if (!m_scene) return true;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (reg.valid(e) && reg.all_of<NavAgentComponent>(e)) return reg.get<NavAgentComponent>(e).path.empty();
        return true;
    });
    m_lua.set_function("set_ai_speed", [this](uint32_t eid, float speed) {
        if (!m_scene) return;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        if (reg.valid(e) && reg.all_of<NavAgentComponent>(e)) reg.get<NavAgentComponent>(e).speed = speed;
    });

    auto findAnimationMachine = [this](uint32_t eid) -> AnimationStateMachineComponent* {
        if (!m_scene) return nullptr;
        auto& reg = m_scene->Registry();
        entt::entity e = static_cast<entt::entity>(eid);
        int depth = 0;
        while (reg.valid(e) && depth++ < 128) {
            if (reg.all_of<AnimationStateMachineComponent>(e)) return &reg.get<AnimationStateMachineComponent>(e);
            if (!reg.all_of<ParentComponent>(e)) break;
            e = reg.get<ParentComponent>(e).parent;
        }
        return nullptr;
    };
    m_lua.set_function("set_animation_state", [findAnimationMachine](uint32_t eid, const std::string& state) {
        if (auto* machine = findAnimationMachine(eid)) AnimationSystem::SetState(*machine, state);
    });
    m_lua.set_function("set_animation_parameter", [findAnimationMachine](uint32_t eid, const std::string& name, float value) {
        if (auto* machine = findAnimationMachine(eid)) AnimationSystem::SetParameter(*machine, name, value);
    });
    m_lua.set_function("trigger_animation", [findAnimationMachine](uint32_t eid, const std::string& name) {
        if (auto* machine = findAnimationMachine(eid)) AnimationSystem::Trigger(*machine, name);
    });
}

void ScriptEngine::RunAIScripts(Scene& scene, float dt) {
    auto& reg = scene.Registry();
    auto view = reg.view<AIControllerComponent>();
    for (auto e : view) {
        auto& aic = view.get<AIControllerComponent>(e);
        if (aic.scriptPath.empty()) continue;
        uint32_t eid = (uint32_t)e;
        auto& instance = m_aiControllers[eid];
        if (instance.loadedPath != aic.scriptPath) {
            try {
                instance.env = sol::environment(m_lua, sol::create, m_lua.globals());
                m_lua.script_file(aic.scriptPath, instance.env);
                instance.updateFunc = instance.env["update_ai"];
                instance.loadedPath = aic.scriptPath;
                spdlog::info("[AI] Loaded script for entity {}: {}", eid, aic.scriptPath);
            } catch (const sol::error& err) {
                AddLog(std::string("[AI ERROR] ") + err.what());
                instance.loadedPath = "ERROR_" + aic.scriptPath;
            }
        }
        if (instance.updateFunc.valid()) {
            sol::protected_function_result result = instance.updateFunc(eid, dt);
            if (!result.valid()) {
                sol::error err = result;
                AddLog(std::string("[AI ERROR] ") + err.what());
            }
        }
    }
}

void ScriptEngine::ClearLuaEntities() {
    if (!m_scene) return;
    auto& reg = m_scene->Registry();
    auto view = reg.view<LuaEntityComponent>();
    std::vector<entt::entity> to_destroy(view.begin(), view.end());
    for (auto e : to_destroy) reg.destroy(e);
    if (!to_destroy.empty()) AddLog("Cleared " + std::to_string(to_destroy.size()) + " script-spawned entities");
}

bool ScriptEngine::HotReload(const std::string& path) {
    ClearLuaEntities();
    m_aiControllers.clear();
    return RunScript(path);
}

void ScriptEngine::Update(float dt) {
    auto updateFunc = m_lua["update"];
    if (updateFunc.valid()) {
        try {
            updateFunc(dt);
        } catch (const sol::error& e) {
            AddLog(std::string("[UPDATE ERROR] ") + e.what());
        }
    }
}

} // namespace Galeshi
