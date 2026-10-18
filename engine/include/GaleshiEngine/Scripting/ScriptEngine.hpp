#pragma once

#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <string>
#include <cstdint>
#include <functional>
#include <vector>
#include <spdlog/spdlog.h>
#include <glm/glm.hpp>

#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

namespace Galeshi {

class ScriptEngine {
public:
    ScriptEngine() {
        m_lua.open_libraries(sol::lib::base, sol::lib::math,
                             sol::lib::table, sol::lib::string);
        BindFunctions();
    }

    bool RunScript(const std::string& filepath) {
        try {
            m_lua.script_file(filepath);
            spdlog::info("[Lua] Ran script: {}", filepath);
            return true;
        } catch (const sol::error& e) {
            spdlog::error("[Lua] {}", e.what());
            return false;
        }
    }

    void BindFunctions() {
        m_lua.set_function("log", [](const std::string& msg) {
            spdlog::info("[Lua] {}", msg);
        });
    }

    void BindECS(Scene& scene) {
        m_scene = &scene;

        m_lua.set_function("new_entity", [this](const std::string& name) -> uint32_t {
            if (!m_scene) return static_cast<uint32_t>(entt::null);
            auto e = m_scene->CreateEntity(name);
            e.AddComponent<LuaEntityComponent>();
            return static_cast<uint32_t>(static_cast<entt::entity>(e));
        });

        m_lua.set_function("set_position",
            [this](uint32_t eid, float x, float y, float z) {
                if (!m_scene) return;
                auto& reg = m_scene->Registry();
                entt::entity e = static_cast<entt::entity>(eid);
                if (!reg.valid(e)) return;
                if (reg.all_of<TransformComponent>(e))
                    reg.get<TransformComponent>(e).position = glm::vec3(x, y, z);
                else
                    reg.emplace<TransformComponent>(e).position = glm::vec3(x, y, z);
            });

        m_lua.set_function("get_position",
            [this](uint32_t eid) -> sol::table {
                sol::table t = m_lua.create_table();
                t["x"] = 0.0f; t["y"] = 0.0f; t["z"] = 0.0f;
                if (!m_scene) return t;
                auto& reg = m_scene->Registry();
                entt::entity e = static_cast<entt::entity>(eid);
                if (!reg.valid(e) || !reg.all_of<TransformComponent>(e)) return t;
                auto& tf = reg.get<TransformComponent>(e);
                t["x"] = tf.position.x;
                t["y"] = tf.position.y;
                t["z"] = tf.position.z;
                return t;
            });

        m_lua.set_function("translate",
            [this](uint32_t eid, float dx, float dy, float dz) {
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

    // Dependency-injected so we don't drag miniaudio.h into this header.
    void BindAudio(std::function<void(const std::string&)> playSound) {
        m_playSound = std::move(playSound);
        m_lua.set_function("play_sound", [this](const std::string& path) {
            if (m_playSound) m_playSound(path);
        });
    }

    // Dependency-injected physics backend: CreateBox(halfExtents, position, entity).
    void BindPhysics(std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)> createBox) {
        m_createBox = std::move(createBox);
        m_lua.set_function("new_physics_cube",
            [this](const std::string& name, float x, float y, float z, float size) -> uint32_t {
                if (!m_scene) return static_cast<uint32_t>(entt::null);
                auto e = m_scene->CreateEntity(name);
                e.AddComponent<LuaEntityComponent>();
                auto& tf = e.AddComponent<TransformComponent>();
                tf.position = glm::vec3(x, y, z);
                tf.scale    = glm::vec3(size);
                auto& rb = e.AddComponent<RigidBodyComponent>();
                rb.motionType = RigidBodyComponent::MotionType::Dynamic;
                rb.mass = 1.0f;
                if (m_createBox) {
                    rb.bodyId = m_createBox(glm::vec3(size * 0.5f),
                                            glm::vec3(x, y, z),
                                            static_cast<entt::entity>(e));
                }
                return static_cast<uint32_t>(static_cast<entt::entity>(e));
            });
    }

    // Destroy all entities created by previous script runs.
    void ClearLuaEntities() {
        if (!m_scene) return;
        auto& reg = m_scene->Registry();
        auto view = reg.view<LuaEntityComponent>();
        std::vector<entt::entity> to_destroy(view.begin(), view.end());
        for (auto e : to_destroy) reg.destroy(e);
        if (!to_destroy.empty())
            spdlog::info("[Lua] Cleared {} script-spawned entities", to_destroy.size());
    }

    bool HotReload(const std::string& path = "assets/scripts/init.lua") {
        ClearLuaEntities();
        return RunScript(path);
    }

    sol::state& GetState() { return m_lua; }

private:
    sol::state m_lua;
    Scene* m_scene = nullptr;
    std::function<void(const std::string&)> m_playSound;
    std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)> m_createBox;
};

} // namespace Galeshi
