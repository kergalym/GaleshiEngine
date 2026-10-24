#pragma once

#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <string>
#include <cstdint>
#include <functional>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

namespace Galeshi {

class Scene;

class ScriptEngine {
public:
    ScriptEngine();
    
    bool RunScript(const std::string& filepath);
    void BindFunctions();
    void BindAI();
    void BindECS(Scene& scene);
    void BindInput(std::function<bool(const std::string&)> isKeyDown);
    void BindVelocity(std::function<glm::vec3(uint32_t)> getVel, std::function<void(uint32_t, const glm::vec3&)> setVel);
    void BindAudio(std::function<void(const std::string&)> playSound);
    void BindPhysics(std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)> createBox);
    void BindImpulse(std::function<void(uint32_t, const glm::vec3&)> applyImpulse);
    
    void RunAIScripts(Scene& scene, float dt);
    void ClearLuaEntities();
    bool HotReload(const std::string& path = "assets/scripts/init.lua");
    void Update(float dt);
    
    sol::state& GetState() { return m_lua; }
    
    // Console logging
    void AddLog(const std::string& msg);
    std::vector<std::string> GetLogs() const { return m_logs; }
    void ClearLogs() { m_logs.clear(); }

private:
    sol::state m_lua;
    Scene* m_scene = nullptr;
    std::function<void(const std::string&)> m_playSound;
    std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)> m_createBox;
    
    struct AIControllerInstance {
        sol::environment env;
        sol::protected_function updateFunc;
        std::string loadedPath;
    };
    std::unordered_map<uint32_t, AIControllerInstance> m_aiControllers;
    std::vector<std::string> m_logs;
};

} // namespace Galeshi
