#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
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
#include <csignal>
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

ScriptEngine::ScriptEngine() {
    m_lua.open_libraries(sol::lib::base, sol::lib::math,
                         sol::lib::table, sol::lib::string);
    BindFunctions();
#if defined(__unix__) || defined(__APPLE__)
    // A dead worker must not terminate the editor with SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
#endif
}

ScriptEngine::~ScriptEngine() {
    StopCppWorker();
}

void ScriptEngine::AddLog(const std::string& msg) {
    m_logs.push_back(msg);
    if (m_logs.size() > 1000) m_logs.erase(m_logs.begin());
    spdlog::info("[Script] {}", msg);
}

bool ScriptEngine::RunScript(const std::string& filepath) {
    try {
        m_lua.script_file(filepath);
        // A fresh script (including a hot reload) gets its own RunOnce pass.
        m_runOnceInvoked = false;
        AddLog("Ran script: " + filepath);
        return true;
    } catch (const sol::error& e) {
        AddLog(std::string("ERROR: ") + e.what());
        return false;
    }
}


void ScriptEngine::BindFunctions() {
    BuildApi();
}

bool ScriptEngine::HasPhysicsBody(uint32_t entityId) const {
    if (!m_scene) return false;
    const entt::entity entity = static_cast<entt::entity>(entityId);
    auto& registry = m_scene->Registry();
    if (!registry.valid(entity) ||
        !registry.all_of<RigidBodyComponent>(entity)) {
        return false;
    }
    return registry.get<RigidBodyComponent>(entity).bodyId != 0xFFFFFFFFu;
}

void ScriptEngine::DestroyEntity(entt::entity entity) {
    if (!m_scene || !m_scene->Registry().valid(entity)) return;
    auto& registry = m_scene->Registry();

    if (registry.all_of<RigidBodyComponent>(entity)) {
        auto& body = registry.get<RigidBodyComponent>(entity);
        if (body.bodyId != 0xFFFFFFFFu && m_physics.destroyBody) {
            m_physics.destroyBody(body.bodyId);
        }
    }
    if (registry.all_of<SoftBodyComponent>(entity)) {
        auto& softBody = registry.get<SoftBodyComponent>(entity);
        if (softBody.softBodyId != 0xFFFFFFFFu && m_physics.destroyBody) {
            m_physics.destroyBody(softBody.softBodyId);
        }
    }
    if (registry.all_of<RagdollComponent>(entity) &&
        m_physics.destroyRagdoll) {
        m_physics.destroyRagdoll(entity);
    }

    registry.destroy(entity);
}

void ScriptEngine::BindECS(Scene& scene) {
    BuildApi();
    m_scene = &scene;
}

void ScriptEngine::BindAssets(AssetManager* assets) {
    BuildApi();
    m_assets = assets;
}

void ScriptEngine::BindInput(std::function<bool(const std::string&)> isKeyDown) {
    BuildApi();
    m_isKeyDown = std::move(isKeyDown);
}

void ScriptEngine::BindInputAxes(
    std::function<float(const std::string&)> getAxis,
    std::function<float(const std::string&)> getAxisRaw,
    std::function<glm::vec2()> getMousePosition,
    std::function<glm::vec2()> getMouseDelta,
    std::function<float()> getMouseWheel,
    std::function<bool(const std::string&)> isMouseButtonDown) {
    BuildApi();
    m_getAxis = std::move(getAxis);
    m_getAxisRaw = std::move(getAxisRaw);
    m_getMousePosition = std::move(getMousePosition);
    m_getMouseDelta = std::move(getMouseDelta);
    m_getMouseWheel = std::move(getMouseWheel);
    m_isMouseButtonDown = std::move(isMouseButtonDown);
}

void ScriptEngine::BindVelocity(
    std::function<glm::vec3(uint32_t)> getVel,
    std::function<void(uint32_t, const glm::vec3&)> setVel) {
    BuildApi();
    m_getVelocity = std::move(getVel);
    m_setVelocity = std::move(setVel);
}

void ScriptEngine::BindAudio(
    std::function<void(const std::string&, bool)> playSound,
    std::function<void(const std::string&)> stopSound,
    std::function<bool(const std::string&)> isSoundPlaying,
    std::function<void()> stopAllSounds,
    std::function<bool(const std::string&)> pauseSound,
    std::function<bool(const std::string&)> resumeSound,
    std::function<bool(const std::string&, float)> setSoundVolume,
    std::function<bool(const std::string&, float)> setSoundPitch,
    std::function<bool(const std::string&, bool)> setSoundLooping,
    std::function<bool(const std::string&, float)> seekSound,
    std::function<float(const std::string&)> soundDuration,
    std::function<float(const std::string&)> soundPosition) {
    BuildApi();
    m_playSound = std::move(playSound);
    m_stopSound = std::move(stopSound);
    m_isSoundPlaying = std::move(isSoundPlaying);
    m_stopAllSounds = std::move(stopAllSounds);
    m_pauseSound = std::move(pauseSound);
    m_resumeSound = std::move(resumeSound);
    m_setSoundVolume = std::move(setSoundVolume);
    m_setSoundPitch = std::move(setSoundPitch);
    m_setSoundLooping = std::move(setSoundLooping);
    m_seekSound = std::move(seekSound);
    m_soundDuration = std::move(soundDuration);
    m_soundPosition = std::move(soundPosition);
}

void ScriptEngine::BindAudio(std::function<void(const std::string&)> playSound) {
    BuildApi();
    m_playSound = [play = std::move(playSound)](const std::string& path, bool) {
        if (play) play(path);
    };
}

void ScriptEngine::BindAudio3D(
    std::function<bool(uint32_t)> playSource,
    std::function<void(uint32_t)> stopSource,
    std::function<bool(uint32_t)> isPlayingSource) {
    BuildApi();
    m_playAudioSource = std::move(playSource);
    m_stopAudioSource = std::move(stopSource);
    m_isAudioSourcePlaying = std::move(isPlayingSource);
}

void ScriptEngine::PushUIEvent(const UIEvent& event) {
    constexpr std::size_t kMaxQueuedUIEvents = 256;
    if (m_cppUIEvents.size() >= kMaxQueuedUIEvents) {
        m_cppUIEvents.pop_front();
    }
    m_cppUIEvents.push_back(event);
}

void ScriptEngine::BindUIState(std::function<bool()> pointerOverUI) {
    BuildApi();
    m_isPointerOverUI = std::move(pointerOverUI);
}

void ScriptEngine::BindUIText(
    std::function<std::string(uint32_t, const std::string&,
                              const std::string&)> textValue) {
    BuildApi();
    m_uiText = std::move(textValue);
}

void ScriptEngine::BindRendering(const RenderingBindings& bindings) {
    BuildApi();
    m_rendering = bindings;
}

void ScriptEngine::BindLandscapeBake(
    std::function<bool(uint32_t)> bake) {
    BuildApi();
    m_landscapeBake = std::move(bake);
}

void ScriptEngine::BindPhysics(const PhysicsBindings& bindings) {
    BuildApi();
    m_physics = bindings;
}

void ScriptEngine::BindPhysics(
    std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)>
        createBox) {
    BuildApi();
    PhysicsBindings bindings;
    if (createBox) {
        bindings.createBox = [createBox = std::move(createBox)](
                                 const glm::vec3& halfExtents,
                                 const glm::vec3& position,
                                 entt::entity entity,
                                 bool) {
            return createBox(halfExtents, position, entity);
        };
    }
    m_physics = std::move(bindings);
}

void ScriptEngine::BindImpulse(
    std::function<void(uint32_t, const glm::vec3&)> applyImpulse) {
    BuildApi();
    m_applyImpulse = std::move(applyImpulse);
}

void ScriptEngine::BindAI() {
    BuildApi();
}

void ScriptEngine::BindNavigation(
    std::function<bool(const glm::vec3&, const glm::vec3&,
                       std::vector<glm::vec3>&)> findPath,
    std::function<bool()> buildNavMesh) {
    BuildApi();
    m_findPath = std::move(findPath);
    m_buildNavMesh = std::move(buildNavMesh);
}

void ScriptEngine::BindEngine(std::function<void()> requestExit) {
    BuildApi();
    m_requestExit = std::move(requestExit);
}

void ScriptEngine::BindCamera(std::function<bool(int)> setMode,
                              std::function<int()> getMode,
                              std::function<glm::vec3()> getForward,
                              std::function<glm::vec3()> getRight) {
    BuildApi();
    m_setCameraMode = std::move(setMode);
    m_getCameraMode = std::move(getMode);
    m_getCameraForward = std::move(getForward);
    m_getCameraRight = std::move(getRight);
}

void ScriptEngine::BindPlayer(std::function<bool()> getStrafing,
                              std::function<void(bool)> setStrafing) {
    BuildApi();
    m_getPlayerStrafing = std::move(getStrafing);
    m_setPlayerStrafing = std::move(setStrafing);
}

void ScriptEngine::BindShader(
    std::function<bool(const std::string&, std::string&)> compileShader) {
    BuildApi();
    m_compileShader = std::move(compileShader);
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

void ScriptEngine::RunEntityScripts(Scene& scene, float dt) {
    auto& registry = scene.Registry();
    auto view = registry.view<ScriptComponent>();

    std::vector<uint32_t> active;
    active.reserve(view.size());
    for (auto entity : view) {
        active.push_back(static_cast<uint32_t>(entity));
    }

    for (uint32_t id : active) {
        const entt::entity entity = static_cast<entt::entity>(id);
        if (!registry.valid(entity) ||
            !registry.all_of<ScriptComponent>(entity)) {
            continue;
        }
        auto& component = registry.get<ScriptComponent>(entity);
        if (component.scriptPath.empty() || !component.enabled) {
            m_entityScripts.erase(id);
            continue;
        }

        std::filesystem::path resolved(component.scriptPath);
        if (resolved.is_relative()) {
            const std::string project = ProjectManager::GetProjectPath();
            if (!project.empty()) {
                const std::filesystem::path candidate =
                    std::filesystem::path(project) / resolved;
                std::error_code existsEc;
                if (std::filesystem::is_regular_file(candidate, existsEc)) {
                    resolved = candidate;
                }
            }
        }

        std::error_code timeEc;
        const auto writeTime =
            std::filesystem::last_write_time(resolved, timeEc);
        auto& instance = m_entityScripts[id];
        const bool pathChanged =
            instance.loadedPath != component.scriptPath;
        const bool fileChanged =
            !timeEc && !pathChanged && instance.writeTime != writeTime;

        if (pathChanged || fileChanged) {
            EntityScriptInstance next;
            next.loadedPath = component.scriptPath;
            next.writeTime = writeTime;
            if (timeEc) {
                next.loadFailed = true;
                AddLog("[SCRIPT] Cannot open script '" +
                       resolved.string() + "': " + timeEc.message());
            } else {
                next.env = sol::environment(m_lua, sol::create,
                                            m_lua.globals());
                try {
                    m_lua.script_file(resolved.string(), next.env);
                    next.startFunc = next.env["OnStart"];
                    next.updateFunc = next.env["OnUpdate"];
                    spdlog::info("[SCRIPT] Loaded '{}' for entity {}",
                                 resolved.string(), id);
                } catch (const sol::error& error) {
                    next.loadFailed = true;
                    AddLog(std::string("[SCRIPT ERROR] ") + error.what());
                }
            }
            instance = std::move(next);
            if (instance.startFunc.valid()) {
                sol::protected_function_result result = instance.startFunc(id);
                if (!result.valid()) {
                    sol::error error = result;
                    AddLog(std::string("[SCRIPT ERROR] OnStart: ") +
                           error.what());
                }
            }
        }

        if (!registry.valid(entity) ||
            !registry.all_of<ScriptComponent>(entity)) {
            m_entityScripts.erase(id);
            continue;
        }
        if (instance.updateFunc.valid()) {
            sol::protected_function_result result =
                instance.updateFunc(id, dt);
            if (!result.valid()) {
                sol::error error = result;
                AddLog(std::string("[SCRIPT ERROR] OnUpdate: ") +
                       error.what());
            }
        }
    }

    for (auto it = m_entityScripts.begin(); it != m_entityScripts.end();) {
        const entt::entity entity = static_cast<entt::entity>(it->first);
        if (!registry.valid(entity) ||
            !registry.all_of<ScriptComponent>(entity)) {
            it = m_entityScripts.erase(it);
        } else {
            ++it;
        }
    }
}

void ScriptEngine::ClearEntityScripts() {
    m_entityScripts.clear();
}

void ScriptEngine::ClearLuaEntities() {
    if (!m_scene) return;
    auto view = m_scene->Registry().view<LuaEntityComponent>();
    std::vector<entt::entity> to_destroy(view.begin(), view.end());
    for (auto entity : to_destroy) DestroyEntity(entity);
    if (!to_destroy.empty()) AddLog("Cleared " + std::to_string(to_destroy.size()) +
                                   " script-spawned entities");
}

bool ScriptEngine::CallUIHandler(const std::string& handler,
                                 const std::string& widgetId,
                                 float value,
                                 bool checked) {
    if (handler.empty()) return false;

    sol::protected_function function;
    const auto it = m_uiHandlers.find(handler);
    if (it != m_uiHandlers.end()) {
        function = it->second;
    } else {
        sol::object object = m_lua[handler];
        if (!object.valid() || object.get_type() != sol::type::function) {
            spdlog::warn("[UI] Lua handler '{}' is not defined", handler);
            return false;
        }
        function = object;
    }

    sol::protected_function_result result = function(widgetId, value, checked);
    if (!result.valid()) {
        sol::error error = result;
        AddLog(std::string("[UI ERROR] ") + handler + ": " + error.what());
        return false;
    }
    return true;
}

void ScriptEngine::DispatchTriggerBeginOverlap(uint32_t trigger,
                                               uint32_t other) {
    constexpr std::size_t kMaxQueuedTriggerEvents = 256;
    if (m_cppTriggerEvents.size() >= kMaxQueuedTriggerEvents) {
        m_cppTriggerEvents.pop_front();
    }
    m_cppTriggerEvents.push_back({true, trigger, other});

    sol::object object = m_lua["OnTriggerBeginOverlap"];
    if (!object.valid() || object.get_type() != sol::type::function) return;
    sol::protected_function function = object;
    sol::protected_function_result result = function(trigger, other);
    if (!result.valid()) {
        sol::error error = result;
        AddLog(std::string("[TRIGGER ERROR] OnTriggerBeginOverlap: ") +
               error.what());
    }
}

void ScriptEngine::DispatchTriggerEndOverlap(uint32_t trigger,
                                             uint32_t other) {
    constexpr std::size_t kMaxQueuedTriggerEvents = 256;
    if (m_cppTriggerEvents.size() >= kMaxQueuedTriggerEvents) {
        m_cppTriggerEvents.pop_front();
    }
    m_cppTriggerEvents.push_back({false, trigger, other});

    sol::object object = m_lua["OnTriggerEndOverlap"];
    if (!object.valid() || object.get_type() != sol::type::function) return;
    sol::protected_function function = object;
    sol::protected_function_result result = function(trigger, other);
    if (!result.valid()) {
        sol::error error = result;
        AddLog(std::string("[TRIGGER ERROR] OnTriggerEndOverlap: ") +
               error.what());
    }
}

bool ScriptEngine::HotReload(const std::string& path) {
    ClearLuaEntities();
    m_aiControllers.clear();
    m_uiHandlers.clear();
    return RunScript(path);
}

void ScriptEngine::Update(float dt) {
    m_deltaTime = dt;
    m_time += static_cast<double>(dt);
    ++m_frame;

    UpdateCppScripts(dt);

    if (!m_runOnceInvoked) {
        // Mark as invoked before the call so a failing RunOnce is not retried
        // every frame. It is reset by the next successful script load/reload.
        m_runOnceInvoked = true;

        auto runOnceFunc = m_lua["RunOnce"];
        if (runOnceFunc.valid() && runOnceFunc.get_type() != sol::type::nil) {
            if (runOnceFunc.get_type() == sol::type::function) {
                try {
                    runOnceFunc();
                } catch (const sol::error& e) {
                    AddLog(std::string("[RUNONCE ERROR] ") + e.what());
                }
            } else {
                AddLog("[RUNONCE ERROR] RunOnce is not a function");
            }
        }
    }

    // RunAlways is the new per-frame entry point. Keep update(dt) working for
    // scripts written before the lifecycle callbacks were introduced.
    auto runAlwaysFunc = m_lua["RunAlways"];
    if (runAlwaysFunc.valid() && runAlwaysFunc.get_type() == sol::type::function) {
        try {
            runAlwaysFunc(dt);
        } catch (const sol::error& e) {
            AddLog(std::string("[RUNALWAYS ERROR] ") + e.what());
        }
    } else {
        auto updateFunc = m_lua["update"];
        if (updateFunc.valid() &&
            updateFunc.get_type() == sol::type::function) {
            try {
                updateFunc(dt);
            } catch (const sol::error& e) {
                AddLog(std::string("[UPDATE ERROR] ") + e.what());
            }
        }
    }

    // Apply script velocities for character roots that have no live Jolt body.
    // Scripts must refresh the velocity during this Update; stale entries are
    // kept for get_velocity() but no longer move the entity.
    if (!m_scene || dt == 0.0f) return;
    auto& registry = m_scene->Registry();
    for (auto it = m_fallbackVelocities.begin();
         it != m_fallbackVelocities.end();) {
        const entt::entity entity = static_cast<entt::entity>(it->first);
        if (!registry.valid(entity) || HasPhysicsBody(it->first)) {
            it = m_fallbackVelocities.erase(it);
            continue;
        }
        if (it->second.frame == m_frame &&
            registry.all_of<TransformComponent>(entity)) {
            auto& tf = registry.get<TransformComponent>(entity);
            auto* rb = registry.try_get<RigidBodyComponent>(entity);
            if (rb && rb->useGravity) {
                if (!it->second.hasGroundY) {
                    it->second.groundY = tf.position.y;
                    it->second.hasGroundY = true;
                }
                // Body-less character roots have no Jolt gravity. Keep the
                // fallback controller grounded at its initial height so a
                // jump rises, slows down and returns instead of flying away.
                if (tf.position.y > it->second.groundY + 1e-4f ||
                    it->second.value.y > 0.0f) {
                    it->second.value.y -= 9.81f * dt;
                }
                tf.position += it->second.value * dt;
                if (tf.position.y <= it->second.groundY) {
                    tf.position.y = it->second.groundY;
                    if (it->second.value.y < 0.0f) {
                        it->second.value.y = 0.0f;
                    }
                }
            } else {
                tf.position += it->second.value * dt;
            }
        }
        ++it;
    }
}



} // namespace Galeshi
