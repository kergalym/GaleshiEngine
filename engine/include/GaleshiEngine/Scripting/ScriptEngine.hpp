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
class AssetManager;

class ScriptEngine {
public:
    // Low-level binding surface for the physics backend.  The scripting API
    // only stores entity ids; these callbacks create/destroy the native bodies.
    struct PhysicsBindings {
        std::function<uint32_t(const glm::vec3& halfExtents,
                               const glm::vec3& position,
                               entt::entity entity,
                               bool isStatic)> createBox;
        std::function<uint32_t(float radius,
                               const glm::vec3& position,
                               entt::entity entity,
                               bool isStatic)> createSphere;
        std::function<uint32_t(float halfHeight,
                               float radius,
                               const glm::vec3& position,
                               entt::entity entity,
                               bool isStatic)> createCapsule;
        std::function<void(uint32_t bodyId)> destroyBody;
    };

    ScriptEngine();

    bool RunScript(const std::string& filepath);

    // Builds the unified `Galeshi` Lua API (and the legacy global aliases).
    // Calling it more than once is safe.
    void BindFunctions();

    // The methods below only connect engine callbacks/subsystems to the API;
    // the Lua entry points themselves are created once by BindFunctions().
    void BindECS(Scene& scene);
    // Enables the Galeshi.animation model/spawn/playback helpers. The host
    // keeps ownership of the AssetManager.
    void BindAssets(AssetManager* assets);
    void BindInput(std::function<bool(const std::string&)> isKeyDown);
    void BindInputAxes(
        std::function<float(const std::string&)> getAxis,
        std::function<float(const std::string&)> getAxisRaw,
        std::function<glm::vec2()> getMousePosition,
        std::function<glm::vec2()> getMouseDelta,
        std::function<float()> getMouseWheel,
        std::function<bool(const std::string&)> isMouseButtonDown);
    void BindVelocity(std::function<glm::vec3(uint32_t)> getVel,
                      std::function<void(uint32_t, const glm::vec3&)> setVel);
    void BindAudio(
        std::function<void(const std::string&, bool)> playSound,
        std::function<void(const std::string&)> stopSound = {},
        std::function<bool(const std::string&)> isSoundPlaying = {},
        std::function<void()> stopAllSounds = {});
    // Backwards-compatible non-looping playback binding.
    void BindAudio(std::function<void(const std::string&)> playSound);
    void BindPhysics(const PhysicsBindings& bindings);
    // Backwards-compatible box-only binding.
    void BindPhysics(
        std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)>
            createBox);
    void BindImpulse(std::function<void(uint32_t, const glm::vec3&)> applyImpulse);
    void BindAI();
    void BindNavigation(
        std::function<bool(const glm::vec3&, const glm::vec3&,
                           std::vector<glm::vec3>&)> findPath,
        std::function<bool()> buildNavMesh = {});
    void BindEngine(std::function<void()> requestExit);

    void RunAIScripts(Scene& scene, float dt);
    void ClearLuaEntities();
    bool HotReload(const std::string& path = "assets/scripts/init.lua");
    void Update(float dt);

    sol::state& GetState() { return m_lua; }

    // Runs a UI event handler registered through Galeshi.ui.on() or a global
    // Lua function with the same name.  Returns false when no handler exists.
    bool CallUIHandler(const std::string& handler,
                       const std::string& widgetId,
                       float value,
                       bool checked);

    // Console logging
    void AddLog(const std::string& msg);
    std::vector<std::string> GetLogs() const { return m_logs; }
    void ClearLogs() { m_logs.clear(); }

private:
    void BuildApi();
    void DestroyEntity(entt::entity entity);

    sol::state m_lua;
    sol::table m_api;
    sol::table m_entityMeta;
    sol::table m_skeletalMeta;
    bool m_apiBuilt = false;
    Scene* m_scene = nullptr;
    AssetManager* m_assets = nullptr;

    std::function<bool(const std::string&)> m_isKeyDown;
    std::function<float(const std::string&)> m_getAxis;
    std::function<float(const std::string&)> m_getAxisRaw;
    std::function<glm::vec2()> m_getMousePosition;
    std::function<glm::vec2()> m_getMouseDelta;
    std::function<float()> m_getMouseWheel;
    std::function<bool(const std::string&)> m_isMouseButtonDown;
    std::function<glm::vec3(uint32_t)> m_getVelocity;
    std::function<void(uint32_t, const glm::vec3&)> m_setVelocity;
    std::function<void(uint32_t, const glm::vec3&)> m_applyImpulse;

    std::function<void(const std::string&, bool)> m_playSound;
    std::function<void(const std::string&)> m_stopSound;
    std::function<bool(const std::string&)> m_isSoundPlaying;
    std::function<void()> m_stopAllSounds;
    PhysicsBindings m_physics;
    std::function<bool(const glm::vec3&, const glm::vec3&,
                       std::vector<glm::vec3>&)> m_findPath;
    std::function<bool()> m_buildNavMesh;
    std::function<void()> m_requestExit;

    std::unordered_map<std::string, sol::protected_function> m_uiHandlers;

    struct AIControllerInstance {
        sol::environment env;
        sol::protected_function updateFunc;
        std::string loadedPath;
    };
    std::unordered_map<uint32_t, AIControllerInstance> m_aiControllers;
    std::vector<std::string> m_logs;
    bool m_runOnceInvoked = false;
    double m_time = 0.0;
    float m_deltaTime = 0.0f;
    uint64_t m_frame = 0;
};

} // namespace Galeshi
