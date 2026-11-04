#pragma once

#include <sol/sol.hpp>
#include <entt/entt.hpp>
#include <string>
#include <cstdint>
#include <chrono>
#include <filesystem>
#include <deque>
#include <functional>
#include <vector>
#include <unordered_map>
#include <glm/glm.hpp>

#include "GaleshiEngine/Scripting/CppScriptApi.hpp"
#include "GaleshiEngine/Scripting/CppScriptWorkerProtocol.hpp"
#include "GaleshiEngine/UI/UIRuntime.hpp"

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
    ~ScriptEngine();

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
    // Entity-based 3D audio. The host resolves the AudioSourceComponent and
    // world position; the scripting API only carries entity ids.
    void BindAudio3D(
        std::function<bool(uint32_t)> playSource,
        std::function<void(uint32_t)> stopSource,
        std::function<bool(uint32_t)> isPlayingSource);
    // Queues one UIRuntime event for the C++ script worker. Poll it from the
    // script with GaleshiScriptApi::poll_ui_event.
    void PushUIEvent(const UIEvent& event);
    void ClearUIEvents() { m_cppUIEvents.clear(); }
    // Optional host query used by the C++ is_pointer_over_ui call.
    void BindUIState(std::function<bool()> pointerOverUI);
    void BindUIText(
        std::function<std::string(uint32_t, const std::string&,
                                  const std::string&)> textValue);
    struct RenderingBindings {
        std::function<uint32_t()> width;
        std::function<uint32_t()> height;
        std::function<bool()> getDeferred;
        std::function<void(bool)> setDeferred;
        std::function<bool()> getLod;
        std::function<void(bool)> setLod;
        std::function<bool()> getOcclusion;
        std::function<void(bool)> setOcclusion;
    };
    void BindRendering(const RenderingBindings& bindings);
    // Optional editor-side landscape bake callback. The runtime host may
    // leave this unbound; script baking then returns false.
    void BindLandscapeBake(std::function<bool(uint32_t)> bake);
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
    void BindCamera(std::function<bool(int)> setMode,
                    std::function<int()> getMode,
                    std::function<glm::vec3()> getForward = {},
                    std::function<glm::vec3()> getRight = {});
    void BindPlayer(std::function<bool()> getStrafing,
                    std::function<void(bool)> setStrafing);
    void BindShader(
        std::function<bool(const std::string&, std::string&)> compileShader);

    void RunAIScripts(Scene& scene, float dt);
    void ClearLuaEntities();
    bool HotReload(const std::string& path = "assets/scripts/init.lua");
    // Scans project scripts/ and assets/scripts/ for .cpp modules, rebuilds
    // changed sources, and swaps loaded libraries in place.
    bool ReloadCppScripts(bool force = false);
    // Diagnostics for the Code Editor tabs. Errors contain compiler
    // stderr (syntax, missing includes, link failures); output contains
    // build/load status.
    const std::string& GetCppBuildOutput() const { return m_cppBuildOutput; }
    const std::string& GetCppBuildErrors() const { return m_cppBuildErrors; }
    bool HasCppBuildErrors() const { return !m_cppBuildErrors.empty(); }
    // Live checks used by the Code Editor while the buffer is being edited.
    // C++ checks only compile one translation unit (no module is loaded).
    bool CheckCppSyntax(const std::string& source, const std::string& sourcePath,
                        std::string& diagnostics);
    bool CheckLuaSyntax(const std::string& source, const std::string& sourcePath,
                        std::string& error);
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
    void UpdateCppScripts(float dt);
    bool StartCppWorker();
    void StopCppWorker(const std::string& reason = {});
    bool HandleWorkerPacket(CppWorkerPacket& packet);
    CppWorkerPacket MakeWorkerResponse(const CppWorkerPacket& request);
    void DestroyEntity(entt::entity entity);
    bool HasPhysicsBody(uint32_t entityId) const;

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
    std::function<bool(uint32_t)> m_playAudioSource;
    std::function<void(uint32_t)> m_stopAudioSource;
    std::function<bool(uint32_t)> m_isAudioSourcePlaying;
    std::function<bool()> m_isPointerOverUI;
    std::function<std::string(uint32_t, const std::string&,
                              const std::string&)> m_uiText;
    std::deque<UIEvent> m_cppUIEvents;
    RenderingBindings m_rendering;
    std::function<bool(uint32_t)> m_landscapeBake;
    PhysicsBindings m_physics;
    std::function<bool(const glm::vec3&, const glm::vec3&,
                       std::vector<glm::vec3>&)> m_findPath;
    std::function<bool()> m_buildNavMesh;
    std::function<void()> m_requestExit;
    std::function<bool(int)> m_setCameraMode;
    std::function<int()> m_getCameraMode;
    std::function<glm::vec3()> m_getCameraForward;
    std::function<glm::vec3()> m_getCameraRight;
    std::function<bool()> m_getPlayerStrafing;
    std::function<void(bool)> m_setPlayerStrafing;
    std::function<bool(const std::string&, std::string&)> m_compileShader;
    std::string m_shaderError;

    std::unordered_map<std::string, sol::protected_function> m_uiHandlers;

    // Script-driven transform movement for entities that have no live Jolt
    // body (for example a parented character root without a collider).
    struct FallbackVelocity {
        glm::vec3 value{0.0f};
        uint64_t frame = 0;
        float groundY = 0.0f;
        bool hasGroundY = false;
    };
    std::unordered_map<uint32_t, FallbackVelocity> m_fallbackVelocities;

    struct AIControllerInstance {
        sol::environment env;
        sol::protected_function updateFunc;
        std::string loadedPath;
    };
    std::unordered_map<uint32_t, AIControllerInstance> m_aiControllers;

    struct CppScriptModule {
        std::string path;
        std::filesystem::file_time_type writeTime{};
        std::string libraryPath;
    };
    std::unordered_map<std::string, CppScriptModule> m_cppModules;
    std::unordered_map<std::string, std::filesystem::file_time_type>
        m_cppFailures;
    std::vector<glm::vec3> m_cppNavigationPath;
    std::chrono::steady_clock::time_point m_nextCppScan{};
    std::string m_cppBuildOutput;
    std::string m_cppBuildErrors;

    int m_cppWorkerPid = -1;
    int m_cppWorkerWrite = -1;
    int m_cppWorkerRead = -1;
    bool m_cppWorkerReady = false;

    std::vector<std::string> m_logs;
    bool m_runOnceInvoked = false;
    double m_time = 0.0;
    float m_deltaTime = 0.0f;
    uint64_t m_frame = 0;
};

} // namespace Galeshi
