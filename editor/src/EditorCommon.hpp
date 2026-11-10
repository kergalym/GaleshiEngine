#pragma once

#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Core/JobSystem.hpp"
#include "GaleshiEngine/Input/InputAxisSystem.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Renderer/Framebuffer.hpp"
#include "GaleshiEngine/Renderer/RayPick.hpp"
#include "GaleshiEngine/Renderer/DebugRenderer.hpp"
#include "GaleshiEngine/Renderer/ParticleSystem.hpp"
#include "GaleshiEngine/Fluid/FluidSimulation.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Renderer/GroomSystem.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Editor/EditorState.hpp"
#include "GaleshiEngine/Editor/ContentBrowser.hpp"
#include "GaleshiEngine/Editor/FilePickerDialog.hpp"
#include "GaleshiEngine/Physics/PhysicsSystem.hpp"
#include "GaleshiEngine/AI/NavigationSystem.hpp"
#include "GaleshiEngine/AI/AISystem.hpp"
#include "GaleshiEngine/Audio/AudioSystem.hpp"
#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Scripting/CppScriptApi.hpp"
#include "GaleshiEngine/UI/UIRuntime.hpp"
#include "GaleshiEngine/Renderer/PostProcessManager.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"
#include "GaleshiEngine/Renderer/ImGuiBgfx.hpp"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_sdl3.h>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/base_sink.h>
#include <spdlog/pattern_formatter.h>
#include <ImGuizmo.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <filesystem>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <nlohmann/json.hpp>

struct LoadingState {
    enum class Stage : int {
        Idle = 0,
        Preparing,
        Parsing,
        Loading,
        Finalizing,
        Cancelling,
    };

    std::atomic<bool> isLoading{false};
    std::atomic<bool> cancelRequested{false};
    std::atomic<bool> workerRunning{false};
    std::atomic<bool> sceneLoaded{false};
    std::atomic<float> progress{0.0f};
    std::atomic<Stage> stage{Stage::Idle};
    std::string scenePath;   // Written before the worker starts, read-only after.
    bool cancelReturnsToProjectManager = false;
    std::thread loaderThread;

    ~LoadingState() {
        if (loaderThread.joinable()) {
            cancelRequested = true;
            loaderThread.join();
        }
    }
};

const char* LoadingStageText(LoadingState::Stage stage);

using ProfileClock = std::chrono::steady_clock;

std::string AssetExtensionLower(const std::string& path);
bool IsModelAssetPath(const std::string& path);
bool IsGltfModelAssetPath(const std::string& path);
bool IsDragDropModelAssetPath(const std::string& path);
bool IsImageAssetPath(const std::string& path);
bool IsAudioAssetPath(const std::string& path);
bool IsFontAssetPath(const std::string& path);
bool IsCppCodeAssetPath(const std::string& path);
bool IsShaderAssetPath(const std::string& path);

bool IsMaterialScriptAssetPath(const std::string& path);
bool IsVertexShaderSource(const std::string& source);
bool IsCodeAssetPath(const std::string& path);

// The project's script folder (scripts/ or assets/scripts/), used by the
// material presets and the PP.settings export/import.
std::filesystem::path ScriptsDirectory();

inline float ProfileElapsedMs(ProfileClock::time_point begin,
                              ProfileClock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

inline constexpr std::size_t kProfileHistorySize = 180;

struct CpuProfileSample {
    float frameMs = 0.0f;
    float updateMs = 0.0f;
    float physicsMs = 0.0f;
    float audioMs = 0.0f;
    float scriptsMs = 0.0f;
    float aiMs = 0.0f;
    float animationMs = 0.0f;
    float groomMs = 0.0f;
    float particlesMs = 0.0f;
    float cameraMs = 0.0f;
    float gameLogicMs = 0.0f;
    float renderCpuMs = 0.0f;
    float pssmMs = 0.0f;
    float pointShadowsMs = 0.0f;
    float spotShadowsMs = 0.0f;
    float occlusionMs = 0.0f;
    float gbufferMs = 0.0f;
    float planarReflectionMs = 0.0f;
    float postFxMs = 0.0f;
    float waterMs = 0.0f;
    float hairMs = 0.0f;
    float imguiMs = 0.0f;
};

struct GpuPassSample {
    std::string name;
    uint16_t view = 0;
    float gpuMs = 0.0f;
    float cpuMs = 0.0f;
};

template <std::size_t N>
void PushProfileHistory(std::array<float, N>& values, float value) {
    std::move(values.begin() + 1, values.end(), values.begin());
    values.back() = value;
}

struct EditorPreferences {
    float cameraOrbitSensitivity = 0.01f;
    // Fraction of the current orbit distance travelled per mouse-wheel notch.
    // The wheel dollies the editor camera forward/back along its view.
    float cameraWheelSpeed = 0.1f;
    float cameraFovDegrees = 60.0f;
};

struct EnvironmentPalettePreset {
    const char* name;
    glm::vec3 shadow;
    glm::vec3 mid;
    glm::vec3 highlight;
    float amount;
};

inline constexpr EnvironmentPalettePreset kEnvironmentPalettePresets[] = {
    {"Neutral",       {0.07f, 0.09f, 0.14f}, {0.48f, 0.45f, 0.40f},
                      {1.00f, 0.94f, 0.82f}, 1.0f},
    {"Warm Sunset",   {0.12f, 0.05f, 0.10f}, {0.55f, 0.33f, 0.22f},
                      {1.00f, 0.72f, 0.42f}, 0.85f},
    {"Cold Moonlight",{0.02f, 0.05f, 0.12f}, {0.25f, 0.38f, 0.55f},
                      {0.75f, 0.88f, 1.00f}, 0.80f},
    {"Teal & Orange", {0.02f, 0.12f, 0.14f}, {0.55f, 0.42f, 0.28f},
                      {1.00f, 0.72f, 0.35f}, 0.80f},
    {"Sepia",         {0.08f, 0.05f, 0.02f}, {0.55f, 0.40f, 0.22f},
                      {1.00f, 0.92f, 0.72f}, 0.90f},
    {"Noir",          {0.00f, 0.00f, 0.00f}, {0.45f, 0.45f, 0.45f},
                      {1.00f, 1.00f, 1.00f}, 1.00f},
    {"Verdant",       {0.02f, 0.08f, 0.04f}, {0.25f, 0.50f, 0.25f},
                      {0.80f, 1.00f, 0.65f}, 0.85f},
    {"Cyberpunk",     {0.03f, 0.01f, 0.10f}, {0.45f, 0.08f, 0.45f},
                      {0.10f, 0.90f, 0.95f}, 0.85f},
};

// Captures the editor's spdlog stream so startup diagnostics remain visible
// in the Output Log dock tab after the console has scrolled past them.
class EditorLogSink final : public spdlog::sinks::base_sink<std::mutex> {
public:
    struct Entry {
        std::string text;
        spdlog::level::level_enum level = spdlog::level::info;
    };

    EditorLogSink() {
        set_formatter(std::make_unique<spdlog::pattern_formatter>(
            "%H:%M:%S [%l] %v"));
    }

    std::vector<Entry> Snapshot() const {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        return {m_entries.begin(), m_entries.end()};
    }

    void Clear() {
        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_entries.clear();
    }

protected:
    void sink_it_(const spdlog::details::log_msg& msg) override {
        spdlog::memory_buf_t formatted;
        formatter_->format(msg, formatted);
        Entry entry;
        entry.text.assign(formatted.data(), formatted.size());
        entry.level = msg.level;

        std::lock_guard<std::mutex> lock(m_snapshotMutex);
        m_entries.push_back(std::move(entry));
        if (m_entries.size() > kMaxEntries) {
            m_entries.pop_front();
        }
    }

    void flush_() override {}

private:
    static constexpr size_t kMaxEntries = 5000;
    mutable std::mutex m_snapshotMutex;
    std::deque<Entry> m_entries;
};

extern std::shared_ptr<EditorLogSink> g_editorLogSink;
