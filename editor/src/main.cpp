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
#include "GaleshiEngine/UI/UIRuntime.hpp"
#include "TextEditor/TextEditor.h"
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
#include <png.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <bx/math.h>

#include <algorithm>
#include <array>
#include <functional>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <future>
#include <atomic>
#include <chrono>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>

// The editor bakes landscape layers on the CPU. Compile its own static stb
// copy so it can decode the same PNG/JPEG/BMP/TGA files the engine loads.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>

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

static const char* LoadingStageText(LoadingState::Stage stage) {
    switch (stage) {
        case LoadingState::Stage::Preparing:  return "Preparing...";
        case LoadingState::Stage::Parsing:    return "Parsing scene...";
        case LoadingState::Stage::Loading:    return "Loading meshes & textures...";
        case LoadingState::Stage::Finalizing: return "Finalizing...";
        case LoadingState::Stage::Cancelling: return "Cancelling...";
        case LoadingState::Stage::Idle:       break;
    }
    return "";
}

using ProfileClock = std::chrono::steady_clock;

// Asset-extension helpers shared by drag & drop, import dialogs and the
// Outliner. Keeping the list in one place prevents a format from being
// accepted in one place and silently rejected in another.
static std::string AssetExtensionLower(const std::string& path) {
    std::string extension =
        std::filesystem::path(path).extension().string();
    for (char& c : extension)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return extension;
}

static bool IsModelAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".fbx" || extension == ".gltf" ||
           extension == ".glb" || extension == ".obj" ||
           extension == ".dae";
}

static bool IsGltfModelAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".gltf" || extension == ".glb";
}

// FBX remains loadable for existing scenes, but asset drag & drop fields now
// accept the runtime-oriented glTF/GLB path (plus OBJ/DAE for static props).
// Keeping this separate from IsModelAssetPath lets legacy file pickers and
// scene loading continue to work while Drop Model fields stay FBX-free.
static bool IsDragDropModelAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".gltf" || extension == ".glb" ||
           extension == ".obj" || extension == ".dae";
}

static bool IsImageAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".png" || extension == ".jpg" ||
           extension == ".jpeg" || extension == ".bmp" ||
           extension == ".tga" || extension == ".dds" ||
           extension == ".ktx";
}

static bool IsAudioAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".wav" || extension == ".mp3";
}

static bool IsFontAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".ttf" || extension == ".otf";
}

static bool IsCppCodeAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".cpp" || extension == ".cc" ||
           extension == ".cxx" || extension == ".hpp" ||
           extension == ".h";
}

static bool IsShaderAssetPath(const std::string& path) {
    const std::string extension = AssetExtensionLower(path);
    return extension == ".glsl" || extension == ".hlsl";
}

static bool IsVertexShaderSource(const std::string& source) {
    return source.find("gl_Position") != std::string::npos &&
           source.find("gl_FragColor") == std::string::npos;
}

static bool IsCodeAssetPath(const std::string& path) {
    return AssetExtensionLower(path) == ".lua" || IsCppCodeAssetPath(path) ||
           IsShaderAssetPath(path);
}

inline float ProfileElapsedMs(ProfileClock::time_point begin,
                              ProfileClock::time_point end) {
    return std::chrono::duration<float, std::milli>(end - begin).count();
}

constexpr std::size_t kProfileHistorySize = 180;

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

static const EnvironmentPalettePreset kEnvironmentPalettePresets[] = {
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

static std::shared_ptr<EditorLogSink> g_editorLogSink;

class GaleshiEditorApp final : public Galeshi::Application {
    Galeshi::AudioSystem m_audio;
    Galeshi::ScriptEngine m_scripts;
    Galeshi::PostProcessManager m_postProcess;
    // AO method used when no post-process volume overrides the settings.
    Galeshi::AmbientOcclusionMode m_globalAoMode =
        Galeshi::AmbientOcclusionMode::SSAO;
    // PSSM tuning. Normal offset and depth bias are expressed in shadow-map
    // texels and converted to per-cascade world/NDC values each frame.
    float m_pssmDepthBiasTexels = 0.75f;
    float m_pssmSlopeBias = 0.00025f;
    float m_pssmNormalOffsetTexels = 1.75f;
    float m_pssmNormalOffsetSlope = 0.5f;
    // 0 = softest PCF, 1 = single-tap hard shadows. The effective PCF
    // radius is derived in PssmPcfRadius().
    float m_pssmShadowSharpness = 0.625f;
    // Viewport render scaling. Dynamic resolution adjusts m_dynamicScale in
    // 5% steps while the manual scale is used when dynamic mode is disabled.
    float m_renderScale = 1.0f;
    bool m_dynamicResolution = false;
    float m_dynamicScale = 1.0f;
    float m_dynamicMinScale = 0.5f;
    float m_dynamicTargetFps = 60.0f;
    float m_smoothedFrameTime = 1.0f / 60.0f;
    int m_dynamicFrameCounter = 0;
    LoadingState m_loadingState;
    float m_displayProgress = 0.0f; // UI thread only, smoothed copy of progress
    std::unique_ptr<Galeshi::Texture> m_logoTexture;
    bool m_iblGenerated = false;
    bool m_projectLoaded = false;
    bool m_resetLayout = false;
    std::string m_gameProjectPath;
    EditorPreferences m_preferences;
    enum class PreferencesSection {
        General,
        Viewport,
        InputAxes,
        About,
    };
    PreferencesSection m_preferencesSection = PreferencesSection::General;
    bool m_showPreferences = false;
    bool m_showHistoryModal = false;
    bool m_showAudioMixer = false;
    bool m_showEnvironmentPalette = false;
    bool m_showStateMachineEditor = false;
    bool m_showOutputLog = true;
    bool m_logAutoScroll = true;
    int m_logLevelFilter = 0;
    std::string m_outputLogBuffer;
    std::string m_outputLogSelection;
    entt::entity m_stateMachineEditorEntity = entt::null;
    int m_selectedAxis = 0;
    int m_rebindAxis = -1;
    int m_rebindBinding = -1;
    std::vector<std::string> m_recentProjects;
    std::vector<std::string> m_recentScenes;
    bool m_pickingScene = false;
    char m_newProjectName[128] = "MyGame";
    char m_newProjectPath[512] = "";
    Galeshi::FilePickerDialog m_projectPicker;
    glm::vec3 m_lastIblSun{999.0f, 999.0f, 999.0f};

    // --- UI Designer state ---
    Galeshi::UIRuntime m_uiRuntime;
    Galeshi::UIRuntime m_uiPreviewRuntime;
    Galeshi::UICanvas m_uiPreviewCanvas;
    entt::entity m_uiCanvasEntity = entt::null;
    int m_uiWidgetIndex = -1;
    int m_uiTool = 0;               // 0 Select, 1 Move, 2 Rect
    bool m_uiShowDesigner = true;
    bool m_uiPreview = false;
    bool m_uiSafeArea = true;
    bool m_uiZTags = true;
    bool m_uiSnap = true;
    float m_uiSnapPx = 8.0f;
    int m_uiZoomPercent = 68;
    char m_uiSearch[64] = "";
    bool m_uiCanvasOverlay = true;
    bool m_uiDragActive = false;
    bool m_uiResizeMode = false;
    bool m_uiDragDirty = false;
    int m_uiResizeHandle = -1; // 0 TL, 1 TC, 2 TR, 3 MR, 4 BR, 5 BC, 6 BL, 7 ML
    int m_uiDragWidget = -1;
    ImVec2 m_uiDragStartMouse{0.0f, 0.0f};
    glm::vec2 m_uiDragStartPosition{0.0f};
    glm::vec2 m_uiDragStartSize{0.0f};

public:
    bool m_isGameMode = false;

    explicit GaleshiEditorApp(bool debugMode, bool isGameMode = false,
                              std::string gameProjectPath = {})
        : Galeshi::Application({
              .name = isGameMode ? "GaleshiGame" : "GaleshiEditor", 
              .width = 0, .height = 0,
              .editor = true,
              .debug = debugMode,
              .splashImage = isGameMode ? "" : "assets/icons/galeshi_engine_logo.png"}) {
        m_isGameMode = isGameMode;
        m_gameProjectPath = std::move(gameProjectPath);
    }

protected:
    // -------------------------------------------------------------------
    // Scene path helpers
    // -------------------------------------------------------------------
    static std::filesystem::path FindProjectRoot() {
        namespace fs = std::filesystem;
        fs::path dir = fs::current_path();
        for (int depth = 0; depth < 8; ++depth) {
            std::error_code ec;
            if (fs::exists(dir / "Game.gproj", ec) ||
                fs::exists(dir / "CMakePresets.json", ec)) {
                return dir;
            }
            const fs::path parent = dir.parent_path();
            if (parent.empty() || parent == dir) break;
            dir = parent;
        }
        return fs::current_path();
    }

    std::string SceneStartDirectory() const {
        namespace fs = std::filesystem;
        if (!m_currentScenePath.empty()) {
            const fs::path parent = fs::path(m_currentScenePath).parent_path();
            std::error_code ec;
            if (!parent.empty() && fs::exists(parent, ec)) return parent.string();
        }
        const fs::path projectRoot = FindProjectRoot();
        std::error_code ec;
        if (fs::exists(projectRoot, ec)) return projectRoot.string();
        const char* home = std::getenv("HOME");
        return home ? home : "/";
    }

    // -------------------------------------------------------------------
    // LIFECYCLE
    // -------------------------------------------------------------------
    void OnStart() override {
        m_logoTexture.reset(
            Galeshi::Texture::LoadFromFile(
                "assets/icons/galeshi_logo_round.png", true));
        if (!m_logoTexture) {
            spdlog::warn("[Editor] Missing logo asset: assets/icons/galeshi_logo_round.png");
        }

        LoadRecentProjects();
        LoadPreferences();

        // Packaged games pass their .gproj on the command line. The editor
        // keeps the historical project.gproj-in-CWD startup behaviour.
        std::string startupProject;
        if (m_isGameMode) {
            startupProject = m_gameProjectPath;
            if (startupProject.empty() &&
                std::filesystem::exists("project.gproj")) {
                startupProject = "project.gproj";
            }
            if (startupProject.empty()) {
                std::error_code ec;
                for (const auto& entry : std::filesystem::directory_iterator(
                         std::filesystem::current_path(), ec)) {
                    if (!entry.is_regular_file(ec)) continue;
                    if (entry.path().extension() == ".gproj") {
                        startupProject = entry.path().string();
                        break;
                    }
                }
            }
        } else if (std::filesystem::exists("project.gproj")) {
            startupProject = "project.gproj";
        }

        if (!startupProject.empty() &&
            Galeshi::ProjectManager::OpenProject(startupProject)) {
            m_projectLoaded = true;
        }
        if (m_projectLoaded) {
            InitializeEditor();
            if (m_isGameMode) LoadGameStartScene(startupProject);
        }
    }

    void LoadGameStartScene(const std::string& projectFile) {
        namespace fs = std::filesystem;
        std::string startScene = "scene.json";
        try {
            std::ifstream in(projectFile);
            if (in.is_open()) {
                nlohmann::json j;
                in >> j;
                startScene = j.value("startScene", startScene);
            }
        } catch (const std::exception& e) {
            spdlog::warn("[Game] Could not read start scene from {}: {}",
                         projectFile, e.what());
        }

        const fs::path projectDir = fs::path(projectFile).parent_path();
        std::error_code ec;
        fs::path scenePath = projectDir / startScene;
        if (!fs::exists(scenePath, ec)) {
            const fs::path assetsPath = projectDir / "assets" / startScene;
            if (fs::exists(assetsPath, ec)) scenePath = assetsPath;
        }
        if (!fs::exists(scenePath, ec)) {
            const fs::path cwdPath = fs::current_path() / startScene;
            if (fs::exists(cwdPath, ec)) scenePath = cwdPath;
        }
        if (!m_serializer) return;
        if (!fs::exists(scenePath, ec)) {
            spdlog::error("[Game] Start scene '{}' not found for {}",
                          startScene, projectFile);
            return;
        }
        if (!m_serializer->Load(scenePath.string())) {
            spdlog::error("[Game] Failed to load start scene {}",
                          scenePath.string());
            return;
        }

        m_currentScenePath = fs::absolute(scenePath).lexically_normal().string();
        m_editor.isPlaying = true;
        ApplySceneViewState();
        EnsurePhysicsBodies();
        ActivateGameplayCamera();
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        spdlog::info("[Game] Loaded start scene {}", m_currentScenePath);
    }

    void OnFileDrop(const std::string& filepath, float x, float y) override {
        if (!m_projectLoaded || !m_assets) return;
        namespace fs = std::filesystem;
        const std::string ext = AssetExtensionLower(filepath);
        if (ext == ".fbx") {
            spdlog::warn(
                "[Editor] FBX drag & drop is no longer supported. "
                "Export the model as glTF/GLB first: {}",
                filepath);
            return;
        }
        if (!IsDragDropModelAssetPath(filepath) && !IsImageAssetPath(filepath) &&
            !IsAudioAssetPath(filepath) && !IsFontAssetPath(filepath)) {
            return;
        }

        const std::string projectPath = Galeshi::ProjectManager::GetProjectPath();
        if (projectPath.empty()) {
            spdlog::error("[Editor] Cannot import '{}': no project is open", filepath);
            return;
        }

        const fs::path assetsRoot = fs::path(projectPath) / "assets";
        // A file dropped on the Content Browser is imported into the folder
        // currently open there. Drops elsewhere keep the previous behavior
        // and go to the project assets root.
        const bool overContentBrowser =
            m_browser && m_browser->ContainsPoint(x, y);
        const fs::path destinationDir =
            overContentBrowser ? m_browser->CurrentDirectory() : assetsRoot;

        if (m_browser) {
            if (m_browser->ImportFiles({filepath}, destinationDir)) {
                spdlog::info("[Editor] Imported file-manager drop '{}' into '{}'",
                             filepath, destinationDir.string());
            }
            return;
        }

        std::error_code ec;
        fs::create_directories(destinationDir, ec);
        const fs::path destination = destinationDir / fs::path(filepath).filename();
        fs::copy_file(filepath, destination,
                      fs::copy_options::overwrite_existing, ec);
        if (ec) {
            spdlog::error("[Editor] Failed to import '{}' to '{}': {}",
                          filepath, destination.string(), ec.message());
        }
    }

    void LoadRecentProjects() {
        const char* home = std::getenv("HOME");
        if (!home) return;
        std::filesystem::path p = std::filesystem::path(home) / ".galeshi_recent.json";
        std::ifstream f(p);
        if (f.is_open()) {
            try {
                nlohmann::json j; f >> j;
                if (j.is_array()) {
                    const auto raw = j.get<std::vector<std::string>>();
                    m_recentProjects.clear();
                    for (const auto& entry : raw) {
                        if (entry.empty()) continue;
                        std::error_code ec;
                        const auto absolute = std::filesystem::absolute(entry, ec);
                        const std::string normalized = ec
                            ? entry : absolute.lexically_normal().string();
                        if (std::find(m_recentProjects.begin(), m_recentProjects.end(),
                                      normalized) == m_recentProjects.end()) {
                            m_recentProjects.push_back(normalized);
                        }
                    }
                }
            } catch (...) {}
        }
    }

    void SaveRecentProjects() {
        const char* home = std::getenv("HOME");
        if (!home) return;
        std::filesystem::path p = std::filesystem::path(home) / ".galeshi_recent.json";
        std::ofstream f(p);
        if (f.is_open()) {
            nlohmann::json j = m_recentProjects;
            f << j.dump(4);
        }
    }

    // Take the path by value: callers used to pass a reference to an element
    // of m_recentProjects, and erasing that element below invalidated the
    // reference before the path could be captured by the load lambda.
    void AddRecentProject(std::string path) {
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(path, ec);
        if (!ec) path = absolute.lexically_normal().string();
        auto it = std::find(m_recentProjects.begin(), m_recentProjects.end(), path);
        if (it != m_recentProjects.end()) m_recentProjects.erase(it);
        m_recentProjects.insert(m_recentProjects.begin(), std::move(path));
        if (m_recentProjects.size() > 10) m_recentProjects.resize(10);
        SaveRecentProjects();
    }

    // -------------------------------------------------------------------
    // Recent scenes
    // -------------------------------------------------------------------
    static std::filesystem::path RecentScenesPath() {
        const char* home = std::getenv("HOME");
        if (!home) return {};
        return std::filesystem::path(home) / ".galeshi_recent_scenes.json";
    }

    static std::string NormalizeRecentScenePath(const std::string& path) {
        if (path.empty()) return {};
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(path, ec);
        return ec ? path : absolute.lexically_normal().string();
    }

    std::string RecentScenesProjectKey() const {
        return NormalizeRecentScenePath(
            Galeshi::ProjectManager::GetProjectPath());
    }

    void LoadRecentScenes() {
        m_recentScenes.clear();
        const std::filesystem::path recentPath = RecentScenesPath();
        const std::string projectKey = RecentScenesProjectKey();
        if (recentPath.empty() || projectKey.empty()) return;

        std::ifstream f(recentPath);
        if (!f.is_open()) return;
        try {
            nlohmann::json root;
            f >> root;
            if (!root.is_object() || !root.contains(projectKey) ||
                !root[projectKey].is_array()) {
                return;
            }
            for (const auto& entry : root[projectKey]) {
                if (!entry.is_string()) continue;
                const std::string path =
                    NormalizeRecentScenePath(entry.get<std::string>());
                if (path.empty()) continue;
                if (std::filesystem::path(path).extension() != ".json") continue;
                if (std::find(m_recentScenes.begin(), m_recentScenes.end(),
                              path) == m_recentScenes.end()) {
                    m_recentScenes.push_back(path);
                }
            }
            if (m_recentScenes.size() > 10) m_recentScenes.resize(10);
        } catch (const std::exception& e) {
            spdlog::warn("[RecentScenes] Could not read {}: {}",
                         recentPath.string(), e.what());
        }
    }

    void SaveRecentScenes() {
        const std::filesystem::path recentPath = RecentScenesPath();
        const std::string projectKey = RecentScenesProjectKey();
        if (recentPath.empty() || projectKey.empty()) return;

        // Keep lists that belong to other projects intact.
        nlohmann::json root = nlohmann::json::object();
        std::ifstream in(recentPath);
        if (in.is_open()) {
            try {
                nlohmann::json existing;
                in >> existing;
                if (existing.is_object()) root = std::move(existing);
            } catch (...) {
                // A corrupt cache is replaced with a fresh object.
            }
        }

        root[projectKey] = m_recentScenes;
        const std::filesystem::path tempPath =
            std::filesystem::path(recentPath.string() + ".tmp");
        std::ofstream out(tempPath, std::ios::trunc);
        if (!out.is_open()) {
            spdlog::warn("[RecentScenes] Could not write {}",
                         tempPath.string());
            return;
        }
        out << root.dump(4);
        out.close();

        std::error_code ec;
        std::filesystem::rename(tempPath, recentPath, ec);
        if (ec) {
            std::filesystem::remove(tempPath, ec);
            spdlog::warn("[RecentScenes] Could not replace {}: {}",
                         recentPath.string(), ec.message());
        }
    }

    void AddRecentScene(std::string path) {
        path = NormalizeRecentScenePath(path);
        if (path.empty() || RecentScenesProjectKey().empty()) return;
        if (std::filesystem::path(path).extension() != ".json") return;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) return;

        auto it = std::find(m_recentScenes.begin(), m_recentScenes.end(), path);
        if (it != m_recentScenes.end()) m_recentScenes.erase(it);
        m_recentScenes.insert(m_recentScenes.begin(), std::move(path));
        if (m_recentScenes.size() > 10) m_recentScenes.resize(10);
        SaveRecentScenes();
    }

    void RemoveRecentScene(const std::string& path) {
        const std::string normalized = NormalizeRecentScenePath(path);
        auto it = std::find(m_recentScenes.begin(), m_recentScenes.end(),
                            normalized);
        if (it == m_recentScenes.end()) return;
        m_recentScenes.erase(it);
        SaveRecentScenes();
    }

    void ClearRecentScenes() {
        if (m_recentScenes.empty()) return;
        m_recentScenes.clear();
        SaveRecentScenes();
        spdlog::info("[RecentScenes] Cleared");
    }

    std::string RecentSceneLabel(const std::string& path) const {
        const std::filesystem::path scenePath(path);
        const std::string projectPath =
            Galeshi::ProjectManager::GetProjectPath();
        if (!projectPath.empty()) {
            const std::filesystem::path base(projectPath);
            const std::filesystem::path relative =
                scenePath.lexically_relative(base);
            if (!relative.empty()) {
                const auto first = relative.begin();
                if (first != relative.end() && *first != "..") {
                    return relative.generic_string();
                }
            }
        }
        return scenePath.filename().string();
    }

    // -------------------------------------------------------------------
    // Input bindings
    // -------------------------------------------------------------------
    std::filesystem::path InputBindingsPath() const {
        const std::string projectPath =
            Galeshi::ProjectManager::GetProjectPath();
        if (!projectPath.empty()) {
            return std::filesystem::path(projectPath) / "input.json";
        }
        return FindProjectRoot() / "input.json";
    }

    void LoadInputBindings() {
        const std::filesystem::path path = InputBindingsPath();
        if (path.empty()) return;
        std::ifstream in(path);
        if (!in.is_open()) return;

        try {
            nlohmann::json root;
            in >> root;
            const nlohmann::json* axesJson = nullptr;
            if (root.is_array()) {
                axesJson = &root;
            } else if (root.contains("axes") && root["axes"].is_array()) {
                axesJson = &root["axes"];
            }
            if (!axesJson) return;

            int restoredAxes = 0;
            for (const auto& ja : *axesJson) {
                if (!ja.is_object()) continue;
                const std::string name = ja.value("name", std::string{});
                auto* axis = GetInput().FindAxis(name);
                if (!axis || !ja.contains("bindings") ||
                    !ja["bindings"].is_array()) {
                    continue;
                }

                std::vector<Galeshi::InputAxisBinding> bindings;
                for (const auto& jb : ja["bindings"]) {
                    if (!jb.is_object()) continue;
                    const std::string code = jb.value("code", std::string{});
                    if (code.empty()) continue;
                    Galeshi::InputAxisBinding binding;
                    binding.type = jb.value("type", std::string("key")) == "mouse"
                        ? Galeshi::InputBindingType::MouseButton
                        : Galeshi::InputBindingType::Key;
                    binding.code = code;
                    binding.direction = jb.value("direction", 1.0f);
                    bindings.push_back(std::move(binding));
                }
                axis->bindings = std::move(bindings);
                ++restoredAxes;
            }
            spdlog::info("[Input] Loaded {} axis binding(s) from {}",
                         restoredAxes, path.string());
        } catch (const std::exception& e) {
            spdlog::warn("[Input] Could not read {}: {}", path.string(),
                         e.what());
        }
    }

    void SaveInputBindings() {
        const std::filesystem::path path = InputBindingsPath();
        if (path.empty()) return;

        nlohmann::json root;
        root["version"] = 1;
        root["axes"] = nlohmann::json::array();
        for (const Galeshi::InputAxis& axis : GetInput().Axes()) {
            nlohmann::json bindings = nlohmann::json::array();
            for (const Galeshi::InputAxisBinding& binding : axis.bindings) {
                bindings.push_back({
                    {"type", binding.type == Galeshi::InputBindingType::MouseButton
                                 ? "mouse" : "key"},
                    {"code", binding.code},
                    {"direction", binding.direction}
                });
            }
            root["axes"].push_back({
                {"name", axis.name},
                {"bindings", std::move(bindings)}
            });
        }

        std::ofstream out(path, std::ios::trunc);
        if (!out.is_open()) {
            spdlog::warn("[Input] Could not write {}", path.string());
            return;
        }
        out << root.dump(4);
        spdlog::info("[Input] Saved axis bindings to {}", path.string());
    }

    void UpdateInputRebind() {
        if (m_rebindAxis < 0) return;
        auto& axes = GetInput().Axes();
        if (m_rebindAxis >= static_cast<int>(axes.size()) ||
            m_rebindBinding < 0 ||
            m_rebindBinding >= static_cast<int>(
                axes[static_cast<size_t>(m_rebindAxis)].bindings.size())) {
            m_rebindAxis = -1;
            m_rebindBinding = -1;
            return;
        }

        const std::string key = GetInput().PollCapturedKey();
        if (key.empty()) return;

        auto& binding = axes[static_cast<size_t>(m_rebindAxis)]
                             .bindings[static_cast<size_t>(m_rebindBinding)];
        binding.type = Galeshi::InputBindingType::Key;
        binding.code = key;
        m_rebindAxis = -1;
        m_rebindBinding = -1;
        SaveInputBindings();
    }

    static std::filesystem::path PreferencesPath() {
        const char* home = std::getenv("HOME");
        if (!home) return {};
        return std::filesystem::path(home) / ".galeshi_preferences.json";
    }

    void LoadPreferences() {
        const std::filesystem::path path = PreferencesPath();
        if (path.empty()) return;

        std::ifstream f(path);
        if (!f.is_open()) return;

        try {
            nlohmann::json j;
            f >> j;

            if (j.contains("viewport") && j["viewport"].is_object()) {
                const auto& viewport = j["viewport"];
                m_preferences.cameraOrbitSensitivity =
                    viewport.value("orbit_sensitivity", m_preferences.cameraOrbitSensitivity);
                m_preferences.cameraWheelSpeed =
                    viewport.value("wheel_speed", m_preferences.cameraWheelSpeed);
                m_preferences.cameraFovDegrees =
                    viewport.value("camera_fov", m_preferences.cameraFovDegrees);
            }

            if (j.contains("input_axes") && j["input_axes"].is_object()) {
                const auto& axesJson = j["input_axes"];
                for (Galeshi::InputAxis& axis : GetInput().Axes()) {
                    if (!axesJson.contains(axis.name) || !axesJson[axis.name].is_object()) {
                        continue;
                    }
                    const auto& axisJson = axesJson[axis.name];
                    axis.sensitivity = axisJson.value("sensitivity", axis.sensitivity);
                    axis.deadZone = axisJson.value("dead_zone", axis.deadZone);
                    axis.invert = axisJson.value("invert", axis.invert);
                }
            }
        } catch (const std::exception& e) {
            spdlog::warn("[Preferences] Could not load {}: {}", path.string(), e.what());
        }

        m_preferences.cameraOrbitSensitivity =
            std::clamp(m_preferences.cameraOrbitSensitivity, 0.001f, 0.1f);
        m_preferences.cameraWheelSpeed =
            std::clamp(m_preferences.cameraWheelSpeed, 0.02f, 0.5f);
        m_preferences.cameraFovDegrees =
            std::clamp(m_preferences.cameraFovDegrees, 30.0f, 110.0f);

        for (Galeshi::InputAxis& axis : GetInput().Axes()) {
            axis.sensitivity = std::clamp(axis.sensitivity, 0.001f, 10.0f);
            axis.deadZone = std::clamp(axis.deadZone, 0.0f, 0.99f);
        }
    }

    void SavePreferences() {
        const std::filesystem::path path = PreferencesPath();
        if (path.empty()) return;

        nlohmann::json j;
        j["viewport"] = {
            {"orbit_sensitivity", m_preferences.cameraOrbitSensitivity},
            {"wheel_speed", m_preferences.cameraWheelSpeed},
            {"camera_fov", m_preferences.cameraFovDegrees},
        };

        nlohmann::json axes = nlohmann::json::object();
        for (const Galeshi::InputAxis& axis : GetInput().Axes()) {
            axes[axis.name] = {
                {"sensitivity", axis.sensitivity},
                {"dead_zone", axis.deadZone},
                {"invert", axis.invert},
            };
        }
        j["input_axes"] = axes;

        std::ofstream f(path);
        if (!f.is_open()) {
            spdlog::warn("[Preferences] Could not write {}", path.string());
            return;
        }
        f << j.dump(4);
    }

    void DrawProjectManager() {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f), ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(600, 400), ImGuiCond_Always);
        ImGui::Begin("Project Manager", nullptr, ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking);

        ImGui::TextColored(ImVec4(0.8f, 0.8f, 0.8f, 1.0f), "Galeshi Engine Project Manager");
        ImGui::Separator();
        ImGui::Spacing();

        if (ImGui::BeginTabBar("ProjectTabs")) {
            if (ImGui::BeginTabItem("Create New")) {
                ImGui::InputText("Project Name", m_newProjectName, sizeof(m_newProjectName));
                ImGui::InputText("Project Path", m_newProjectPath, sizeof(m_newProjectPath));
                ImGui::SameLine();
                if (ImGui::Button("Browse...")) {
                    const char* home = std::getenv("HOME");
                    m_projectPicker.PickFolder(home ? home : "/", "Select Project Folder");
                }
                ImGui::Spacing();
                if (ImGui::Button("Create Project", ImVec2(150, 30))) {
                    if (Galeshi::ProjectManager::CreateProject(m_newProjectPath, m_newProjectName)) {
                        m_projectLoaded = true;
                        StartAsyncLoad("Creating " + std::string(m_newProjectName), [this]() {
                            InitializeEditor();
                        }, true);
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Open Existing")) {
                if (ImGui::Button("Browse ...", ImVec2(200, 30))) {
                    m_projectPicker.Open(std::getenv("HOME"), "Open Scene", { ".json" });
                    m_pickingScene = true;
                }
                
                ImGui::Spacing();
                ImGui::TextDisabled("Recent Projects:");
                ImGui::BeginChild("RecentList", ImVec2(0, 200), true);
                if (m_recentProjects.empty()) {
                    ImGui::TextDisabled("No recent projects.");
                } else {
                    for (size_t i = 0; i < m_recentProjects.size(); ++i) {
                        ImGui::PushID(static_cast<int>(i));
                        // Copy before AddRecentProject reorders the vector.
                        const std::string projectPath = m_recentProjects[i];
                        std::string filename = std::filesystem::path(projectPath).filename().string();
                        
                        if (ImGui::Selectable(filename.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                if (Galeshi::ProjectManager::OpenProject(projectPath)) {
                                    m_projectLoaded = true;
                                    AddRecentProject(projectPath);
                                    StartAsyncLoad(filename, [this, projectPath]() {
                                        InitializeEditor();
                                        if (m_loadingState.cancelRequested) return;
                                        if (m_serializer &&
                                            m_serializer->Load(projectPath, &m_loadingState.cancelRequested)) {
                                            m_currentScenePath = std::filesystem::absolute(projectPath)
                                                .lexically_normal().string();
                                            m_loadingState.sceneLoaded.store(
                                                true, std::memory_order_release);
                                        } else {
                                            spdlog::error("[ProjectManager] Failed to load scene: {}", projectPath);
                                        }
                                    }, true);
                                }
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\nDouble-click to open.", projectPath.c_str());
                        }
                        ImGui::PopID();
                    }
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        std::string chosenPath = m_projectPicker.Draw();
        if (!chosenPath.empty()) {
            if (m_pickingScene) {
                m_pickingScene = false;
                if (Galeshi::ProjectManager::OpenProject(chosenPath)) {
                    m_projectLoaded = true;
                    AddRecentProject(chosenPath);
                    StartAsyncLoad(std::filesystem::path(chosenPath).filename().string(), [this, chosenPath]() {
                        InitializeEditor();
                        if (m_loadingState.cancelRequested) return;
                        if (m_serializer &&
                            m_serializer->Load(chosenPath, &m_loadingState.cancelRequested)) {
                            m_currentScenePath = std::filesystem::absolute(chosenPath)
                                .lexically_normal().string();
                            m_loadingState.sceneLoaded.store(
                                true, std::memory_order_release);
                        } else {
                            spdlog::error("[ProjectManager] Failed to load scene: {}", chosenPath);
                        }
                    }, true);
                }
            } else {
                std::strncpy(m_newProjectPath, chosenPath.c_str(), sizeof(m_newProjectPath) - 1);
            }
        }

        ImGui::End();
    }

    bool PlayerInputActive() const {
        return m_isGameMode || m_editor.isPlaying;
    }

    void InitializeEditor() {
        // Scene loading must stay fast: keep BC7 texture compression off by
        // default. It can be enabled from Details -> Texture Settings.
        Galeshi::Texture::SetCompressionEnabled(false);
        spdlog::info("[Editor] Texture compression on load: {}",
                     Galeshi::Texture::CompressionEnabled() ? "on" : "off");

        // --- Auto-inserted subsystem init ---
        spdlog::info("[Subsystem] m_audio.Init() begin");
        if (!m_audio.Init()) spdlog::error("[Subsystem] m_audio.Init() failed");
        else spdlog::info("[Subsystem] m_audio.Init() done");
        // Assets must be available before scripts run so Lua can declare
        // skeletal animation models. The AssetManager is also used by the
        // scene serializer and renderer below.
        m_assets = std::make_unique<Galeshi::AssetManager>();
        m_audio.SetPathResolver([this](const std::string& path) {
            return m_assets ? m_assets->ResolvePath(path) : path;
        });
        m_uiRuntime.SetAssetManager(m_assets.get());
        m_uiPreviewRuntime.SetAssetManager(m_assets.get());
        const auto reloadUiFonts = []() {
            Galeshi::ImGuiBgfx::ReloadFontTexture();
        };
        m_uiRuntime.SetFontAtlasChangedCallback(reloadUiFonts);
        m_uiPreviewRuntime.SetFontAtlasChangedCallback(reloadUiFonts);
        m_scripts.BindAssets(m_assets.get());
        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        spdlog::info("[Subsystem] m_physics->Init() begin");
        if (!m_physics->Init()) spdlog::error("[Subsystem] m_physics->Init() failed");
        else spdlog::info("[Subsystem] m_physics->Init() done");
        m_scripts.BindFunctions();
        m_scripts.BindECS(m_scene);
        // Gameplay scripts may only read player input while playing. The
        // editor still reads the same devices directly for its own controls.
        m_scripts.BindInput([this](const std::string& key) -> bool {
            return PlayerInputActive() && GetInput().IsKeyDown(key);
        });
        m_scripts.BindInputAxes(
            [this](const std::string& axis) {
                return PlayerInputActive() ? GetInput().GetAxis(axis) : 0.0f;
            },
            [this](const std::string& axis) {
                return PlayerInputActive() ? GetInput().GetAxisRaw(axis) : 0.0f;
            },
            [this]() {
                return PlayerInputActive() ? GetInput().MousePosition()
                                           : glm::vec2(0.0f);
            },
            [this]() {
                return PlayerInputActive() ? GetInput().MouseDelta()
                                           : glm::vec2(0.0f);
            },
            [this]() {
                return PlayerInputActive() ? GetInput().MouseWheel() : 0.0f;
            },
            [this](const std::string& button) {
                return PlayerInputActive() &&
                       GetInput().IsMouseButtonDown(button);
            });
        m_scripts.BindImpulse([this](uint32_t eid, const glm::vec3& impulse) {
            entt::entity e = static_cast<entt::entity>(eid);
            auto& reg = m_scene.Registry();
            if (reg.valid(e) && reg.all_of<Galeshi::RigidBodyComponent>(e)) {
                auto& rb = reg.get<Galeshi::RigidBodyComponent>(e);
                if (rb.bodyId != 0xFFFFFFFF) {
                    m_physics->AddImpulse(rb.bodyId, impulse);
                }
            }
        });
        m_scripts.BindVelocity(
            [this](uint32_t eid) -> glm::vec3 {
                entt::entity e = static_cast<entt::entity>(eid);
                auto& reg = m_scene.Registry();
                if (reg.valid(e) && reg.all_of<Galeshi::RigidBodyComponent>(e)) {
                    const auto& rb = reg.get<Galeshi::RigidBodyComponent>(e);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        return m_physics->GetLinearVelocity(rb.bodyId);
                    }
                }
                return glm::vec3(0.0f);
            },
            [this](uint32_t eid, const glm::vec3& velocity) {
                entt::entity e = static_cast<entt::entity>(eid);
                auto& reg = m_scene.Registry();
                if (reg.valid(e) && reg.all_of<Galeshi::RigidBodyComponent>(e)) {
                    const auto& rb = reg.get<Galeshi::RigidBodyComponent>(e);
                    if (rb.bodyId != 0xFFFFFFFF) {
                        m_physics->SetLinearVelocity(rb.bodyId, velocity);
                    }
                }
            });
        m_scripts.BindAudio(
            [this](const std::string& path, bool loop) {
                m_audio.PlaySound(path, loop);
            },
            [this](const std::string& path) { m_audio.StopSound(path); },
            [this](const std::string& path) {
                return m_audio.IsSoundPlaying(path);
            },
            [this]() { m_audio.StopAll(); },
            [this](const std::string& path) {
                return m_audio.PauseSound(path);
            },
            [this](const std::string& path) {
                return m_audio.ResumeSound(path);
            },
            [this](const std::string& path, float volume) {
                return m_audio.SetSoundVolume(path, volume);
            },
            [this](const std::string& path, float pitch) {
                return m_audio.SetSoundPitch(path, pitch);
            },
            [this](const std::string& path, bool loop) {
                return m_audio.SetSoundLooping(path, loop);
            },
            [this](const std::string& path, float seconds) {
                return m_audio.SeekSound(path, seconds);
            },
            [this](const std::string& path) {
                return m_audio.SoundDuration(path);
            },
            [this](const std::string& path) {
                return m_audio.SoundPosition(path);
            });
        m_scripts.BindAudio3D(
            [this](uint32_t entityId) {
                auto& registry = m_scene.Registry();
                const entt::entity entity =
                    static_cast<entt::entity>(entityId);
                if (!registry.valid(entity) ||
                    !registry.all_of<Galeshi::AudioSourceComponent>(entity)) {
                    return false;
                }
                auto& source =
                    registry.get<Galeshi::AudioSourceComponent>(entity);
                glm::vec3 position(0.0f);
                if (registry.all_of<Galeshi::TransformComponent>(entity)) {
                    const glm::mat4 world = EntityWorldMatrix(entity);
                    glm::vec3 scale, translation, skew;
                    glm::vec4 perspective;
                    glm::quat rotation;
                    glm::decompose(world, scale, rotation, translation,
                                   skew, perspective);
                    position = translation + rotation * source.offset;
                }
                const bool playing = m_audio.PlaySource(
                    entity, source, position);
                source.playing = playing;
                return playing;
            },
            [this](uint32_t entityId) {
                const entt::entity entity =
                    static_cast<entt::entity>(entityId);
                m_audio.StopSource(entity);
                auto& registry = m_scene.Registry();
                if (registry.valid(entity) &&
                    registry.all_of<Galeshi::AudioSourceComponent>(entity)) {
                    registry.get<Galeshi::AudioSourceComponent>(entity)
                        .playing = false;
                }
            },
            [this](uint32_t entityId) {
                return m_audio.IsSourcePlaying(
                    static_cast<entt::entity>(entityId));
            });
        m_scripts.BindAI();
        Galeshi::ScriptEngine::PhysicsBindings scriptPhysics;
        scriptPhysics.createBox =
            [this](const glm::vec3& halfExtents, const glm::vec3& position,
                   entt::entity entity, bool isStatic) {
                return m_physics->CreateBox(halfExtents, position, entity,
                                            isStatic);
            };
        scriptPhysics.createSphere =
            [this](float radius, const glm::vec3& position,
                   entt::entity entity, bool isStatic) {
                return m_physics->CreateSphere(radius, position, entity,
                                               isStatic);
            };
        scriptPhysics.createCapsule =
            [this](float halfHeight, float radius, const glm::vec3& position,
                   entt::entity entity, bool isStatic) {
                return m_physics->CreateCapsule(halfHeight, radius, position,
                                                entity, isStatic);
            };
        scriptPhysics.destroyBody = [this](uint32_t bodyId) {
            if (m_physics) m_physics->DestroyBody(bodyId);
        };
        m_scripts.BindPhysics(scriptPhysics);
        m_scripts.BindEngine([this]() { RequestExit(); });
        m_scripts.BindCamera(
            [this](int mode) { return SetGameplayCameraMode(mode); },
            [this]() { return GetGameplayCameraMode(); },
            [this]() { return GetGameplayCameraMoveForward(); },
            [this]() { return GetGameplayCameraMoveRight(); });
        m_scripts.BindPlayer(
            [this]() { return m_playerStrafing; },
            [this](bool enabled) { m_playerStrafing = enabled; });
        m_scripts.BindShader(
            [this](const std::string& path, std::string& error) {
                CompileShaderPreview(path);
                error = m_shaderPreviewError;
                return error.empty();
            });
        // Project-local key bindings override the built-in axis defaults
        // before scripts start reading input.
        LoadInputBindings();
        spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) begin");
        if (!m_scripts.RunScript("assets/scripts/init.lua"))
            spdlog::error("[Subsystem] m_scripts.RunScript(init.lua) failed");
        else
            spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) done");
        // --- End auto-inserted ---

        m_uiRuntime.SetEventHandler([this](const Galeshi::UIEvent& event) {
            DispatchUIEvent(event);
        });
        m_scripts.BindUIState([this]() {
            return m_uiRuntime.IsPointerOverUI();
        });
        m_scripts.BindUIText(
            [this](uint32_t canvasEntity, const std::string& canvasId,
                   const std::string& widgetId) {
                return m_uiRuntime.TextValue(
                    static_cast<int>(canvasEntity), canvasId, widgetId);
            });

        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        m_physics->Init();
        m_physics->SetOnContactCallback([this](entt::entity e1, entt::entity e2) {
            // Simple demo: play beep on any collision involving an AudioSource entity
            auto& reg = m_scene.Registry();
            for (entt::entity e : { e1, e2 }) {
                if (reg.valid(e) && reg.all_of<Galeshi::AudioSourceComponent>(e)) {
                    auto& a = reg.get<Galeshi::AudioSourceComponent>(e);
                    if (!a.soundPath.empty()) {
                        glm::vec3 position(0.0f);
                        if (reg.all_of<Galeshi::TransformComponent>(e)) {
                            const glm::mat4 world = EntityWorldMatrix(e);
                            glm::vec3 scale, translation, skew;
                            glm::vec4 perspective;
                            glm::quat rotation;
                            glm::decompose(world, scale, rotation, translation,
                                           skew, perspective);
                            position = translation + rotation * a.offset;
                        }
                        m_audio.PlaySource(e, a, position);
                        a.playing = m_audio.IsSourcePlaying(e);
                    }
                }
            }
        });
        m_physics->SetOnTriggerBeginOverlap(
            [this](entt::entity trigger, entt::entity other) {
                if (!m_editor.isPlaying) return;
                m_scripts.DispatchTriggerBeginOverlap(
                    static_cast<uint32_t>(trigger),
                    static_cast<uint32_t>(other));
            });
        m_physics->SetOnTriggerEndOverlap(
            [this](entt::entity trigger, entt::entity other) {
                if (!m_editor.isPlaying) return;
                m_scripts.DispatchTriggerEndOverlap(
                    static_cast<uint32_t>(trigger),
                    static_cast<uint32_t>(other));
            });
        m_navSystem = std::make_unique<Galeshi::NavigationSystem>();
        m_navSystem->Init();
        m_scripts.BindNavigation(
            [this](const glm::vec3& start, const glm::vec3& end,
                   std::vector<glm::vec3>& path) {
                return m_navSystem &&
                       m_navSystem->FindPath(start, end, path);
            },
            [this]() {
                return m_navSystem && m_navSystem->BuildFromScene(m_scene);
            });
        m_debug = std::make_unique<Galeshi::DebugRenderer>();
        m_debug->Init();
        
        m_render  = std::make_unique<Galeshi::RenderSystem>();
        m_render->Init(m_assets.get());
        m_particles = std::make_unique<Galeshi::ParticleSystem>();
        m_particles->Init();
        m_postProcess.Init();
        ConfigureProfilerViewNames();
        m_render->SetDeferredEnabled(true);
        {
            Galeshi::ScriptEngine::RenderingBindings rendering;
            rendering.width = [this]() {
                const auto* renderer = GetRenderer();
                return renderer ? renderer->Width() : 0u;
            };
            rendering.height = [this]() {
                const auto* renderer = GetRenderer();
                return renderer ? renderer->Height() : 0u;
            };
            rendering.getDeferred = [this]() {
                return m_render && m_render->DeferredEnabled();
            };
            rendering.setDeferred = [this](bool value) {
                if (m_render) m_render->SetDeferredEnabled(value);
            };
            rendering.getLod = [this]() {
                return m_render && m_render->LodEnabled();
            };
            rendering.setLod = [this](bool value) {
                if (m_render) m_render->SetLodEnabled(value);
            };
            rendering.getOcclusion = [this]() {
                return m_render && m_render->OcclusionCullingEnabled();
            };
            rendering.setOcclusion = [this](bool value) {
                if (m_render) m_render->SetOcclusionCullingEnabled(value);
            };
            m_scripts.BindRendering(rendering);
        }
        m_scripts.BindLandscapeBake(
            [this](uint32_t entityId) {
                Galeshi::Entity entity(
                    static_cast<entt::entity>(entityId), &m_scene);
                if (!entity ||
                    !entity.HasComponent<
                        Galeshi::LandscapeComponent>()) {
                    return false;
                }
                return BakeLandscapeTextures(entity);
            });
        // Enable SSAO/SSR on frame 2 to avoid sampling an uninitialized Vulkan target.
        m_pendingEffectsEnable = true;
        for (int i = 0; i < 3; ++i) {
            m_shadowMaps[i] = bgfx::createTexture2D(2048, 2048, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            m_shadowFbos[i] = bgfx::createFrameBuffer(1, &m_shadowMaps[i], false);
        }
        


        // Content Browser shows the open project's assets folder, not the
        // engine's own assets directory.
        const std::string projectPath = Galeshi::ProjectManager::GetProjectPath();
        const std::string browserRoot = projectPath.empty()
            ? std::string("assets")
            : (std::filesystem::path(projectPath) / "assets").string();
        {
            std::error_code ec;
            std::filesystem::create_directories(browserRoot, ec);
        }
        m_browser    = std::make_unique<Galeshi::ContentBrowser>(
            browserRoot, m_assets.get());
        m_serializer = std::make_unique<Galeshi::SceneSerializer>(m_scene, *m_assets);
        LoadRecentScenes();

        // The editor deliberately starts with an empty scene.  Scenes are
        // loaded explicitly through File -> Load Scene or File -> Recent Scenes.
        spdlog::info("[Editor] Starting with an empty scene");

        m_history.clear();
        m_historyIndex = -1;
        PushHistory();
        
        // Force layout reset to match the tabbed design
        m_resetLayout = true;

        m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees),
            static_cast<float>(GetRenderer()->Width()) / static_cast<float>(GetRenderer()->Height()),
            0.1f, 1000.0f);
    }

    void OnUpdate(float dt) override {
        UpdateLoadingState();
        if (m_loadingState.isLoading) return; // Pause engine during load
        if (!m_projectLoaded) return;

        // ImGui locks its font atlas during NewFrame/EndFrame, so custom UI
        // fonts must be added here, before the frame starts.
        m_uiRuntime.PreloadFonts(m_scene);
        m_uiPreviewRuntime.PreloadFonts(m_scene);

        // This update path is independent of m_realtimeRendering. Pausing
        // render passes must not stop scene simulation: physics, scripting,
        // AI, animation, groom, particles and time continue every frame.
        const auto updateStart = ProfileClock::now();
        m_cpuProfile = CpuProfileSample{};
        m_cpuProfile.frameMs = dt * 1000.0f;

        spdlog::trace("[Main] OnUpdate");
        // --- Deferred effect enable (frame 2+) ---
        m_renderFrameCounter++;
        if (m_pendingEffectsEnable && m_renderFrameCounter >= 2) {
            // Temporary debug hook: GALESHI_EFFECTS is a bitmask used to
            // bisect Vulkan layout problems. DoF is intentionally off here:
            // post-process volumes enable it for their own settings.
            int effectMask = 0x3EF;
            if (const char* env = std::getenv("GALESHI_EFFECTS")) {
                effectMask = std::atoi(env);
            }
            m_postProcess.SetSsaoEnabled((effectMask & 1) != 0);
            m_postProcess.SetSsrEnabled((effectMask & 2) != 0);
            m_postProcess.SetSmaaEnabled((effectMask & 4) != 0);
            m_postProcess.SetFxaaEnabled((effectMask & 8) != 0);
            m_postProcess.SetDofEnabled((effectMask & 16) != 0);
            m_postProcess.SetMotionBlurEnabled((effectMask & 32) != 0);
            m_postProcess.SetAtmosphereEnabled((effectMask & 64) != 0);
            m_postProcess.SetGodRaysEnabled((effectMask & 128) != 0);
            m_postProcess.SetTaaEnabled((effectMask & 256) != 0);
            m_postProcess.SetBloomEnabled((effectMask & 512) != 0);
            m_pendingEffectsEnable = false;
            spdlog::info("[Effects] post-processing enabled on frame {} mask=0x{:x}",
                         m_renderFrameCounter, effectMask);
        }

        // --- Auto-inserted physics update ---
        {
            const auto physicsStart = ProfileClock::now();
            if (m_physics) {
                m_physics->Update(m_scene, dt);
                m_physics->SyncTransformsToECS(m_scene);
            }
            m_cpuProfile.physicsMs = ProfileElapsedMs(physicsStart, ProfileClock::now());
        }

        // Audio: listener, 3D source positions, autoplay and mixer sync.
        {
            const auto audioStart = ProfileClock::now();
            UpdateAudio(dt);
            m_cpuProfile.audioMs = ProfileElapsedMs(audioStart, ProfileClock::now());
        }
        // --- End auto-inserted ---

        m_time += dt;
        m_frameDelta = dt;

        if (m_dynamicResolution) {
            const float clampedDelta = std::clamp(dt, 1.0f / 240.0f, 1.0f / 15.0f);
            m_smoothedFrameTime = m_smoothedFrameTime * 0.9f + clampedDelta * 0.1f;
            ++m_dynamicFrameCounter;
            if (m_dynamicFrameCounter >= 20) {
                const float target = 1.0f / std::max(m_dynamicTargetFps, 1.0f);
                if (m_smoothedFrameTime > target * 1.10f) {
                    m_dynamicScale -= 0.05f;
                } else if (m_smoothedFrameTime < target * 0.92f) {
                    m_dynamicScale += 0.05f;
                }
                m_dynamicScale = std::clamp(m_dynamicScale,
                                             m_dynamicMinScale, 1.0f);
                m_dynamicFrameCounter = 0;
            }
        }
        {
            const auto scriptsStart = ProfileClock::now();
            m_scripts.RunAIScripts(m_scene, dt);
            m_scripts.Update(dt);
            m_cpuProfile.scriptsMs = ProfileElapsedMs(scriptsStart, ProfileClock::now());
        }

        {
            const auto aiStart = ProfileClock::now();
            Galeshi::AISystem::Update(m_scene, dt, m_navSystem.get());
            m_cpuProfile.aiMs = ProfileElapsedMs(aiStart, ProfileClock::now());
        }

        // Skeletal playback and animation state machines. This samples the
        // active clip and blends the previous state when a transition fires.
        {
            const auto animationStart = ProfileClock::now();
            Galeshi::AnimationSystem::Update(m_scene, *m_assets, dt);
            m_cpuProfile.animationMs = ProfileElapsedMs(animationStart, ProfileClock::now());
        }
        // Hair follows the freshly sampled skeleton, then simulates its own
        // spring chains before the render pass builds camera-facing ribbons.
        {
            const auto groomStart = ProfileClock::now();
            Galeshi::GroomSystem::Update(m_scene, *m_assets, dt);
            m_cpuProfile.groomMs = ProfileElapsedMs(groomStart, ProfileClock::now());
        }

        // Particle simulation (world-space billboards).
        {
            const auto particlesStart = ProfileClock::now();
            if (m_particles) m_particles->Update(m_scene, dt);
            m_cpuProfile.particlesMs = ProfileElapsedMs(particlesStart, ProfileClock::now());
        }

        const auto cameraStart = ProfileClock::now();
        if (m_usingGameCamera) {
            // Play is driven by the primary gameplay camera. The editor orbit
            // state remains untouched so Stop can immediately restore it.
            EndEditorMouseLook();
            UpdateGameplayCamera();
        } else {
            // The viewport camera must remain free to navigate through the
            // scene, including over collision-shape wireframes and their
            // transform gizmos. Only an actively manipulated gizmo may block
            // camera input; merely hovering it must not.
            const bool cameraControllable =
                (m_viewportHovered || m_editorLookDragging) &&
                (!m_uiOverlayHovered || m_editorLookDragging) &&
                !m_editor.GizmoLocked() && !ImGuizmo::IsUsing();
            
            // Mouse wheel dollies the viewport camera forward/back along its
            // view direction. Moving the orbit target as well keeps the
            // camera orientation unchanged while it travels through the scene.
            if (cameraControllable && !ImGui::GetIO().WantTextInput) {
                const float wheel = GetInput().MouseWheel();
                if (wheel != 0.0f) {
                    const float cp = std::cos(m_editor.camPitch);
                    const float sp = std::sin(m_editor.camPitch);
                    const float sy = std::sin(m_editor.camYaw);
                    const float cy = std::cos(m_editor.camYaw);
                    const glm::vec3 forward{-cp * sy, -sp, -cp * cy};
                    const float step = std::max(m_editor.camRadius, 0.5f) *
                                       m_preferences.cameraWheelSpeed;
                    m_editor.camTarget += forward * (wheel * step);
                }
            }

            // Godot-style RMB look: only start rotating after the mouse
            // actually moves, then capture the pointer and rotate in place.
            const bool lookInput =
                cameraControllable &&
                !ImGui::GetIO().WantTextInput &&
                !ImGui::GetIO().AppFocusLost &&
                ImGui::IsMouseDown(ImGuiMouseButton_Right);
            if (lookInput) {
                if (!m_editorLooking) {
                    // Capture on press, but skip the synthetic relative
                    // motion caused by switching modes. A plain RMB click
                    // therefore does not rotate the camera.
                    m_editorLooking = true;
                    m_editorLookDragging = true;
                    SetMouseCapture(true);
                    m_editorLookSkipFrames = 2;
                }

                if (m_editorLookSkipFrames > 0) {
                    --m_editorLookSkipFrames;
                } else {
                    const glm::vec2 d = GetInput().MouseDelta();
                    const glm::vec3 eye = m_camera.Position();
                    m_editor.camYaw -=
                        d.x * m_preferences.cameraOrbitSensitivity;
                    m_editor.camPitch +=
                        d.y * m_preferences.cameraOrbitSensitivity;
                    m_editor.camPitch =
                        glm::clamp(m_editor.camPitch, -1.5f, 1.5f);

                    const float cp = std::cos(m_editor.camPitch);
                    const float sp = std::sin(m_editor.camPitch);
                    const glm::vec3 offset{
                        m_editor.camRadius * cp * std::sin(m_editor.camYaw),
                        m_editor.camRadius * sp,
                        m_editor.camRadius * cp * std::cos(m_editor.camYaw)};
                    m_editor.camTarget = eye - offset;
                }
            } else {
                EndEditorMouseLook();
            }

            const float aspect = m_viewportH
                ? static_cast<float>(m_viewportW) / m_viewportH
                : 16.0f / 9.0f;
            // Keep depth precision usable when the orbit camera moves far away
            // from its target. A fixed 0.1/1000 range causes z-fighting and
            // flickering on distant, nearly coplanar geometry.
            const float orbitDistance = std::max(m_editor.camRadius, 0.5f);
            const float nearZ = glm::clamp(orbitDistance * 0.0025f,
                                           0.1f, 0.75f);
            const float farZ = std::max(1000.0f, orbitDistance * 4.0f + 500.0f);
            m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees),
                                    aspect, nearZ, farZ);
            m_camera.Orbit(m_editor.camTarget, m_editor.camYaw, m_editor.camPitch, m_editor.camRadius);
        }
        m_cpuProfile.cameraMs = ProfileElapsedMs(cameraStart, ProfileClock::now());
        m_cpuProfile.updateMs = ProfileElapsedMs(updateStart, ProfileClock::now());
        m_cpuProfile.gameLogicMs = m_cpuProfile.physicsMs + m_cpuProfile.audioMs +
            m_cpuProfile.scriptsMs + m_cpuProfile.aiMs +
            m_cpuProfile.animationMs + m_cpuProfile.groomMs +
            m_cpuProfile.particlesMs + m_cpuProfile.cameraMs;
    }

    void OnRender() override {}

    void OnImGui() override {
        if (!m_projectLoaded) {
            DrawProjectManager();
            return;
        }
        if (m_isGameMode) {
            DrawViewport();
            m_uiRuntime.RenderInRect(m_scene, m_viewportImagePos.x,
                                     m_viewportImagePos.y, m_viewportImageSize.x,
                                     m_viewportImageSize.y);
            return;
        }

        if (m_loadingState.isLoading) {
            DrawLoadingModal();
            return;
        }

        // Escape is the runtime stop shortcut. It takes priority over an
        // active key-rebind capture so pressing it while playing always
        // returns to the editor instead of binding Escape.
        if (!m_showPreferences) {
            m_rebindAxis = -1;
            m_rebindBinding = -1;
        }
        if (!m_isGameMode && m_editor.isPlaying &&
            ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            m_rebindAxis = -1;
            m_rebindBinding = -1;
            StopPlay();
        } else {
            UpdateInputRebind();
        }

        const auto imguiStart = ProfileClock::now();
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F8)) {
            m_showProfiler = !m_showProfiler;
        }
        DrawMainMenuBar();
        UpdateProfilerDebugState();

        // Hotkeys
        // Preferences works even while a text field is active.
        if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_Comma)) {
            m_showPreferences = true;
        }

        // Focus Camera on F (works even if some widget is active)
        if (!ImGui::GetIO().WantTextInput && ImGui::IsKeyPressed(ImGuiKey_F)) {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::TransformComponent>()) {
                m_editor.camTarget = sel.GetComponent<Galeshi::TransformComponent>().position;
                m_editor.camRadius = 10.0f;
                spdlog::info("[Editor] Focused camera on entity at ({}, {}, {})", 
                    m_editor.camTarget.x, m_editor.camTarget.y, m_editor.camTarget.z);
            } else {
                spdlog::warn("[Editor] F pressed but nothing selected");
            }
        }
        
        if (!ImGui::GetIO().WantTextInput && !ImGui::IsAnyItemActive()) {
            if (ImGui::IsKeyPressed(ImGuiKey_W)) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::IsKeyPressed(ImGuiKey_E)) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::IsKeyPressed(ImGuiKey_R)) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            if (ImGui::IsKeyPressed(ImGuiKey_Delete)) DeleteSelected();
            if (ImGui::IsKeyPressed(ImGuiKey_F5)) {
                spdlog::info("[Script] Hot-reload init.lua and C++ modules");
                m_scripts.HotReload("assets/scripts/init.lua");
                m_scripts.ReloadCppScripts(false);
            }
            // Space is now used by Lua (Jump)

            if (ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_N)) NewScene();
                if (ImGui::IsKeyPressed(ImGuiKey_I)) OpenFileDialog();
                if (ImGui::IsKeyPressed(ImGuiKey_S)) {
                    SaveScene(ImGui::GetIO().KeyShift);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene();
                if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
                if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
            }
        }

        DrawDockspace();
        {
            const auto renderStart = ProfileClock::now();
            DrawViewport();
            m_cpuProfile.renderCpuMs = ProfileElapsedMs(renderStart, ProfileClock::now());
        }
        if (m_pendingLandscapeBake != entt::null &&
            !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            Galeshi::Entity pending(m_pendingLandscapeBake, &m_scene);
            if (pending &&
                pending.HasComponent<Galeshi::LandscapeComponent>() &&
                !pending.GetComponent<Galeshi::LandscapeComponent>()
                     .layers.empty()) {
                BakeLandscapeTextures(pending);
            }
            m_pendingLandscapeBake = entt::null;
        }
        DrawOutliner();
        DrawDetails();
        DrawContentBrowser();
        DrawCodeEditor();
        DrawLuaConsole();
        DrawScriptComponents();
        DrawUIDesigner();
        DrawOutputLog();
        DrawAudioMixer();
        DrawEnvironmentPaletteWindow();
        DrawStateMachineEditor();
        DrawPreferencesWindow();
        DrawHistoryModal();

        // Play mode draws the authored canvases inside the viewport image.
        if (m_editor.isPlaying && !m_uiPreview) {
            m_uiRuntime.RenderInRect(m_scene, m_viewportImagePos.x,
                                     m_viewportImagePos.y, m_viewportImageSize.x,
                                     m_viewportImageSize.y);
        }

        
        std::string chosenOpen = m_filePicker.Draw();
        if (!chosenOpen.empty()) {
            SpawnFromModel(chosenOpen, m_editor.camTarget);
        }

        std::string chosenSave = m_filePickerSave.Draw();
        if (!chosenSave.empty()) {
            SaveSceneToPath(chosenSave);
        }

        // Load Scene dialog (.json)
        std::string chosenLoad = m_scenePicker.Draw();
        if (!chosenLoad.empty()) {
            LoadSceneFromPath(chosenLoad);
        }

        // Character model replacement picker (glTF/GLB only).
        std::string chosenCharacterModel = m_characterModelPicker.Draw();
        if (!chosenCharacterModel.empty() &&
            m_characterModelTarget != entt::null) {
            ReplaceCharacterModel(m_characterModelTarget, chosenCharacterModel);
            m_characterModelTarget = entt::null;
        }

        // Separate animation picker for the state-machine state field.
        std::string chosenAnimationAsset = m_animationClipPicker.Draw();
        if (!chosenAnimationAsset.empty() &&
            m_animationClipTarget != entt::null &&
            m_animationClipState >= 0) {
            auto& animRegistry = m_scene.Registry();
            if (animRegistry.valid(m_animationClipTarget) &&
                animRegistry.all_of<Galeshi::AnimationStateMachineComponent>(
                    m_animationClipTarget)) {
                auto& animMachine =
                    animRegistry.get<Galeshi::AnimationStateMachineComponent>(
                        m_animationClipTarget);
                if (m_animationClipState <
                    static_cast<int>(animMachine.states.size())) {
                    AssignStateAnimationAsset(
                        Galeshi::Entity(m_animationClipTarget, &m_scene),
                        m_animationClipState, chosenAnimationAsset);
                }
            }
            m_animationClipTarget = entt::null;
            m_animationClipState = -1;
        }

        std::string chosenAudio = m_audioPicker.Draw();
        if (!chosenAudio.empty() && m_audioPickerTarget != entt::null) {
            auto& audioRegistry = m_scene.Registry();
            if (audioRegistry.valid(m_audioPickerTarget) &&
                audioRegistry.all_of<Galeshi::AudioSourceComponent>(
                    m_audioPickerTarget)) {
                PushHistory();
                auto& source =
                    audioRegistry.get<Galeshi::AudioSourceComponent>(
                        m_audioPickerTarget);
                m_audio.StopSource(m_audioPickerTarget);
                source.soundPath = m_assets
                    ? m_assets->MakeRelativePath(chosenAudio)
                    : chosenAudio;
                source.playing = false;
            }
            m_audioPickerTarget = entt::null;
        } else if (!m_audioPicker.IsOpen()) {
            m_audioPickerTarget = entt::null;
        }

        // Static tree-mesh picker for the selected landscape vegetation.
        std::string chosenTreeMesh = m_vegetationMeshPicker.Draw();
        if (!chosenTreeMesh.empty()) {
            auto& registry = m_scene.Registry();
            if (m_vegetationMeshTarget != entt::null &&
                registry.valid(m_vegetationMeshTarget)) {
                AssignLandscapeTreeModel(
                    Galeshi::Entity(m_vegetationMeshTarget, &m_scene),
                    chosenTreeMesh, m_vegetationMeshTypeIndex);
            }
            m_vegetationMeshTarget = entt::null;
            m_vegetationMeshTypeIndex = -1;
        } else if (!m_vegetationMeshPicker.IsOpen()) {
            m_vegetationMeshTarget = entt::null;
            m_vegetationMeshTypeIndex = -1;
        }

        // Capture property edits that do not have explicit PushHistory calls.
        // The active item is observed after the change; the pre-edit state is
        // already the latest snapshot, so Undo still returns one full step.
        if (!m_isGameMode && !m_editor.isPlaying) {
            if (ImGui::IsAnyItemActive()) {
                m_uiItemWasActive = true;
            } else if (m_uiItemWasActive) {
                m_uiItemWasActive = false;
                if (m_suppressHistory) {
                    m_suppressHistory = false;
                } else {
                    PushHistory();
                }
            } else if (m_suppressHistory) {
                // Undo/Redo via a keyboard shortcut has no item to release.
                m_suppressHistory = false;
            }
        }

        if (m_editorLookDragging) {
            // SDL relative mode captures the pointer; hide the visible cursor
            // while the user is actively looking around.
            ImGui::SetMouseCursor(ImGuiMouseCursor_None);
        }

        m_cpuProfile.imguiMs = ProfileElapsedMs(imguiStart, ProfileClock::now());
        if (m_showProfiler) {
            UpdateGpuProfilerStats();
        } else {
            m_gpuFrameMs = 0.0f;
        }
        PushProfilerHistory();
        DrawProfilerWindow();
    }

    void OnShutdown() override {
        spdlog::info("[Editor] OnShutdown: begin");

        // Never leave the OS cursor captured when the process exits.
        EndEditorMouseLook();
        EndGameMouseLook();

        // Never let the loader touch bgfx/SDL after they have been shut down.
        if (m_loadingState.loaderThread.joinable()) {
            m_loadingState.cancelRequested = true;
            m_loadingState.loaderThread.join();
        }

        // Autosave is disabled; scenes are saved only through File -> Save Scene (Ctrl+S).
        SavePreferences();

        // Release scene-owned resources while the Vulkan device is still
        // alive. In particular, do not let SDL tear down the window/display
        // before bgfx has waited for its GPU work.
        ClearSceneForShutdown();
        if (m_particles) m_particles->Shutdown();
        if (m_debug) m_debug->Shutdown();
        if (m_render) m_render->Shutdown();
        ShutdownShaderPreview();
        m_postProcess.Shutdown();
        if (m_framebuffer) m_framebuffer->Destroy();
        for (auto& fbo : m_shadowFbos) {
            if (bgfx::isValid(fbo)) bgfx::destroy(fbo);
            fbo = BGFX_INVALID_HANDLE;
        }
        for (auto& map : m_shadowMaps) {
            if (bgfx::isValid(map)) bgfx::destroy(map);
            map = BGFX_INVALID_HANDLE;
        }
        m_audio.Shutdown();
        if (m_navSystem) m_navSystem->Shutdown();

        // ImGui owns bgfx buffers and its font texture; destroy those before
        // bgfx shuts down. Then stop SDL only after the renderer is down.
        Galeshi::ImGuiBgfx::Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        if (auto* renderer = GetRenderer()) renderer->Shutdown();
        SDL_Quit();

        spdlog::info("[Editor] OnShutdown: complete");
        std::fflush(stdout);
        std::fflush(stderr);
        std::_Exit(0);
    }

private:
    // -------------------------------------------------------------------
    // UI: Menu Bar
    // -------------------------------------------------------------------
    void StartAsyncLoad(const std::string& path,
                        std::function<void()> customLoad = nullptr,
                        bool cancelReturnsToProjectManager = false) {
        auto& s = m_loadingState;
        if (s.isLoading) return;
        if (s.loaderThread.joinable()) s.loaderThread.join();

        s.isLoading = true;
        s.cancelRequested = false;
        s.workerRunning = true;
        s.sceneLoaded = false;
        s.progress = 0.0f;
        s.stage = LoadingState::Stage::Preparing;
        s.scenePath = path;
        s.cancelReturnsToProjectManager = cancelReturnsToProjectManager;
        m_displayProgress = 0.0f;

        s.loaderThread = std::thread([this, path, customLoad]() {
            auto& state = m_loadingState;
            const auto cancelled = [&state] {
                return state.cancelRequested.load(std::memory_order_relaxed);
            };

            // Preparation and parsing are short, cancellable stages. The
            // blocking part (customLoad / SceneSerializer::Load) cannot be
            // interrupted, so the worker only checks the flag around it.
            for (int i = 1; i <= 8 && !cancelled(); ++i) {
                state.progress.store(i * 0.0125f, std::memory_order_relaxed); // 0..10%
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }

            if (!cancelled()) {
                state.stage.store(LoadingState::Stage::Parsing, std::memory_order_relaxed);
                state.progress.store(0.12f, std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(40));
            }

            if (!cancelled()) {
                state.stage.store(LoadingState::Stage::Loading, std::memory_order_relaxed);
                state.progress.store(0.15f, std::memory_order_relaxed);

                if (customLoad) {
                    customLoad();
                } else if (m_serializer) {
                    if (m_serializer->Load(path, &m_loadingState.cancelRequested)) {
                        m_currentScenePath =
                            std::filesystem::absolute(path).lexically_normal().string();
                        state.sceneLoaded.store(true,
                                                std::memory_order_release);
                    }
                }
            }

            if (cancelled()) {
                // Keep isLoading set: the UI thread joins the worker and
                // releases the partially loaded scene and GPU resources.
                state.stage.store(LoadingState::Stage::Cancelling, std::memory_order_relaxed);
                state.workerRunning = false;
                return;
            }

            state.stage.store(LoadingState::Stage::Finalizing, std::memory_order_relaxed);
            for (int i = 0; i <= 12 && !cancelled(); ++i) {
                state.progress.store(0.85f + 0.15f * (i / 12.0f), std::memory_order_relaxed);
                std::this_thread::sleep_for(std::chrono::milliseconds(12));
            }

            if (cancelled()) {
                state.stage.store(LoadingState::Stage::Cancelling, std::memory_order_relaxed);
                state.workerRunning = false;
                return;
            }

            state.progress.store(1.0f, std::memory_order_relaxed);
            state.workerRunning = false;
            state.isLoading = false;
        });
    }

    // Called from OnUpdate every frame: joins the worker and, after a cancel,
    // destroys everything the loader may have already created.
    void UpdateLoadingState() {
        auto& s = m_loadingState;

        if (s.isLoading) {
            if (s.cancelRequested && !s.workerRunning) {
                if (s.loaderThread.joinable()) s.loaderThread.join();
                FinishCancelledLoad();
            }
            return;
        }

        // Successful load: reap the finished thread so the next load starts
        // from a clean state, then release assets left behind by the scene
        // that was replaced.
        if (s.loaderThread.joinable() && !s.workerRunning) {
            s.loaderThread.join();
            ReleaseUnusedSceneAssets();
            if (s.sceneLoaded.exchange(false, std::memory_order_acquire)) {
                ApplySceneViewState();
                RestoreCodeEditorStateFromSerializer();
                if (m_browser && !m_serializer->ContentBrowserPath().empty()) {
                    m_browser->SetDirectory(
                        m_serializer->ContentBrowserPath());
                }
                // A loaded scene is a new history root; snapshots from the
                // previous document must never be reachable through Undo.
                m_history.clear();
                m_historyIndex = -1;
                PushHistory();
                AddRecentScene(m_currentScenePath);
            }
        }
    }

    void FinishCancelledLoad() {
        UnloadSceneContent();

        m_loadingState.isLoading = false;
        m_loadingState.cancelRequested = false;
        m_loadingState.stage = LoadingState::Stage::Idle;
        m_loadingState.progress = 0.0f;
        m_displayProgress = 0.0f;

        // A cancelled initial project open returns to the project manager only
        // if InitializeEditor() never ran. Re-running it on an already
        // initialized editor is not supported, so otherwise stay in the editor
        // with an empty scene.
        if (m_loadingState.cancelReturnsToProjectManager && !m_assets) {
            m_projectLoaded = false;
        }
        m_loadingState.cancelReturnsToProjectManager = false;

        spdlog::info("[Loading] Cancelled: scene data and video memory released");
    }

    // Shutdown variant of UnloadSceneContent: release all scene references
    // and runtime state without pushing an undo snapshot or reinitializing
    // navigation. Called while bgfx is still valid.
    void ClearSceneForShutdown() {
        m_editor.ClearSelection();
        m_editor.isPlaying = false;
        m_usingGameCamera = false;
        m_activeGameCamera = entt::null;
        m_audio.StopAll();

        if (m_physics) m_physics->Reset();
        if (m_navSystem) m_navSystem->Shutdown();
        if (m_particles) m_particles->Reset();

        m_scene.Registry().clear();
        m_scene.AudioBuses().fill(Galeshi::AudioBusSettings{});
        if (m_assets) m_assets->Clear();
        m_lastViewportTexture = BGFX_INVALID_HANDLE;

        m_currentScenePath.clear();
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        m_history.clear();
        m_historyIndex = -1;
    }

    // The scene must release its shared mesh/material/texture references
    // before the AssetManager cache is cleared; only then are the bgfx buffers
    // and textures actually destroyed.
    void UnloadSceneContent(bool pushUndoSnapshot = true) {
        if (!m_assets) return;

        m_editor.ClearSelection();
        m_editor.isPlaying = false;
        m_usingGameCamera = false;
        m_activeGameCamera = entt::null;
        m_pendingLandscapeBake = entt::null;
        m_wasSculpting = false;
        m_scripts.ClearUIEvents();
        m_scripts.ClearTriggerEvents();
        m_audio.StopAll();

        if (m_physics) m_physics->Reset();
        if (m_navSystem) {
            m_navSystem->Shutdown();
            m_navSystem->Init();
        }

        m_scene.Registry().clear();
        m_assets->Clear();
        if (m_particles) m_particles->Reset();
        m_lastViewportTexture = BGFX_INVALID_HANDLE;

        m_history.clear();
        m_historyIndex = -1;
        m_currentScenePath.clear();
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        if (pushUndoSnapshot && m_serializer) {
            PushHistory(); // empty-scene snapshot for Undo/Redo
        }
    }

    void DrawLoadingModal() {
        auto& s = m_loadingState;
        if (!s.isLoading) return;

        ImGuiViewport* vp = ImGui::GetMainViewport();

        ImGui::GetBackgroundDrawList()->AddRectFilled(
            vp->Pos,
            ImVec2(vp->Pos.x + vp->Size.x, vp->Pos.y + vp->Size.y),
            IM_COL32(0, 0, 0, 180));

        ImGui::SetNextWindowPos(
            ImVec2(vp->Pos.x + vp->Size.x * 0.5f, vp->Pos.y + vp->Size.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(500, 0), ImGuiCond_Always);

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 6.0f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.15f, 0.15f, 0.15f, 1.0f));
        ImGui::Begin("Loading Modal", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
            ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoScrollbar);

        ImGui::TextColored(ImVec4(0.9f, 0.9f, 0.9f, 1.0f), "Loading");
        ImGui::Separator();
        ImGui::Spacing();

        ImGui::Text("Loading scene %s", s.scenePath.c_str());
        ImGui::Spacing();
        ImGui::Spacing();

        // Smoothly approach the loader milestone. While the blocking asset
        // load runs the bar creeps towards 80% instead of freezing at 15%.
        const float dt = std::min(ImGui::GetIO().DeltaTime, 0.1f);
        const float target = s.progress.load();
        if (s.cancelRequested) {
            // Freeze the bar while the worker is winding down.
        } else if (s.stage.load() == LoadingState::Stage::Loading) {
            m_displayProgress += (0.80f - m_displayProgress) * std::min(1.0f, dt * 0.6f);
            m_displayProgress = std::min(m_displayProgress, 0.80f);
        } else {
            m_displayProgress += (target - m_displayProgress) * std::min(1.0f, dt * 8.0f);
        }

        const float shown = std::clamp(m_displayProgress, 0.0f, 1.0f);
        char overlay[16];
        std::snprintf(overlay, sizeof(overlay), "%d%%",
                      static_cast<int>(shown * 100.0f + 0.5f));

        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::ProgressBar(shown, ImVec2(-1, 20), overlay);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        const bool cancelling = s.cancelRequested.load();
        ImGui::TextDisabled("%s", cancelling ? "Cancelling..." : LoadingStageText(s.stage.load()));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        const float buttonWidth = 120.0f;
        ImGui::SetCursorPosX((ImGui::GetWindowWidth() - buttonWidth) * 0.5f);
        ImGui::BeginDisabled(cancelling);
        if (ImGui::Button(cancelling ? "Cancelling..." : "Cancel", ImVec2(buttonWidth, 30))) {
            s.cancelRequested = true;
        }
        ImGui::EndDisabled();

        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar();
    }

    // -------------------------------------------------------------------
    // Preferences
    // -------------------------------------------------------------------
    void DrawHistoryModal() {
        if (m_showHistoryModal) {
            ImGui::OpenPopup("Undo History");
            m_showHistoryModal = false;
        }
        if (!ImGui::BeginPopupModal(
                "Undo History", nullptr,
                ImGuiWindowFlags_AlwaysAutoResize)) {
            return;
        }

        ImGui::Text("Undo / Redo History");
        ImGui::TextDisabled("Current snapshot: %d / %d",
                            m_historyIndex + 1,
                            static_cast<int>(m_history.size()));
        ImGui::Separator();
        ImGui::BeginChild("history_snapshots", ImVec2(520.0f, 260.0f),
                          true);
        if (m_history.empty()) {
            ImGui::TextDisabled("No scene snapshots yet.");
        } else {
            for (int i = 0; i < static_cast<int>(m_history.size()); ++i) {
                char label[64];
                std::snprintf(label, sizeof(label), "Snapshot %d%s", i + 1,
                              i == m_historyIndex ? "   (current)" : "");
                ImGui::Selectable(label, i == m_historyIndex);
            }
        }
        ImGui::EndChild();
        ImGui::Separator();

        ImGui::BeginDisabled(m_historyIndex <= 0);
        if (ImGui::Button("Undo", ImVec2(110.0f, 0.0f))) Undo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(
            m_historyIndex + 1 >= static_cast<int>(m_history.size()));
        if (ImGui::Button("Redo", ImVec2(110.0f, 0.0f))) Redo();
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Close", ImVec2(110.0f, 0.0f))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }

    void DrawPreferencesWindow() {
        if (!m_showPreferences) return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(
            ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
                   viewport->Pos.y + viewport->Size.y * 0.5f),
            ImGuiCond_FirstUseEver, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(ImVec2(920.0f, 600.0f), ImGuiCond_FirstUseEver);

        if (!ImGui::Begin("Preferences", &m_showPreferences,
                          ImGuiWindowFlags_NoDocking |
                              ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            if (!m_showPreferences) SavePreferences();
            return;
        }

        ImGui::BeginChild("##PreferencesSidebar", ImVec2(210.0f, 0.0f), true);
        ImGui::TextDisabled("PREFERENCES");
        ImGui::Spacing();

        auto sidebarItem = [this](const char* label, PreferencesSection section) {
            if (ImGui::Selectable(label, m_preferencesSection == section,
                                  ImGuiSelectableFlags_None, ImVec2(0.0f, 28.0f))) {
                m_preferencesSection = section;
            }
        };

        sidebarItem("General", PreferencesSection::General);
        sidebarItem("Viewport", PreferencesSection::Viewport);
        sidebarItem("Input Axes", PreferencesSection::InputAxes);
        sidebarItem("About", PreferencesSection::About);

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("Camera and input settings");
        ImGui::TextDisabled("apply immediately.");
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##PreferencesContent", ImVec2(0.0f, 0.0f), false);

        switch (m_preferencesSection) {
        case PreferencesSection::General: {
            ImGui::Text("General");
            ImGui::Separator();
            ImGui::Spacing();

            const std::string projectName =
                Galeshi::ProjectManager::IsProjectOpen()
                    ? Galeshi::ProjectManager::GetProjectName()
                    : std::string();
            const std::string projectPath =
                Galeshi::ProjectManager::IsProjectOpen()
                    ? Galeshi::ProjectManager::GetProjectPath()
                    : std::string();

            ImGui::Text("Project");
            ImGui::TextDisabled("%s",
                                projectName.empty() ? "No project open"
                                                    : projectName.c_str());
            if (!projectPath.empty()) {
                ImGui::TextWrapped("Path: %s", projectPath.c_str());
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Editor");
            if (ImGui::Button("Reset Editor Layout", ImVec2(190.0f, 0.0f))) {
                m_resetLayout = true;
            }
            ImGui::TextDisabled("The dock layout is rebuilt on the next frame.");

            ImGui::Spacing();
            if (ImGui::Button("Reset All Preferences", ImVec2(190.0f, 0.0f))) {
                m_preferences = EditorPreferences{};
                GetInput().ResetDefaults();
                m_selectedAxis = 0;
                m_rebindAxis = -1;
                m_rebindBinding = -1;
                SaveInputBindings();
            }

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::TextWrapped(
                "Preferences are saved when this window closes and when the "
                "editor exits.");
            const std::filesystem::path path = PreferencesPath();
            if (!path.empty()) {
                ImGui::TextDisabled("%s", path.string().c_str());
            }
            break;
        }

        case PreferencesSection::Viewport: {
            ImGui::Text("Viewport");
            ImGui::Separator();
            ImGui::TextWrapped(
                "Control the editor camera. These settings do not change the "
                "game camera.");
            ImGui::Spacing();

            ImGui::SetNextItemWidth(260.0f);
            ImGui::SliderFloat("Orbit sensitivity",
                               &m_preferences.cameraOrbitSensitivity,
                               0.001f, 0.05f, "%.3f");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::SliderFloat("Wheel distance", &m_preferences.cameraWheelSpeed,
                               0.02f, 0.5f, "%.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "How far the viewport camera moves per wheel notch,\n"
                    "as a fraction of the current orbit distance.\n"
                    "Wheel up moves forward, wheel down moves backward.");
            }
            ImGui::SetNextItemWidth(260.0f);
            ImGui::SliderFloat("Field of view", &m_preferences.cameraFovDegrees,
                               30.0f, 110.0f, "%.0f deg");

            ImGui::Spacing();
            if (ImGui::Button("Restore Viewport Defaults",
                              ImVec2(190.0f, 0.0f))) {
                m_preferences = EditorPreferences{};
            }
            ImGui::TextDisabled(
                "Hold right mouse to look from the viewport center; "
                "mouse wheel moves forward/back.");
            break;
        }

        case PreferencesSection::InputAxes: {
            ImGui::Text("Input Axes");
            ImGui::Separator();
            ImGui::TextWrapped(
                "Axes combine keyboard keys and mouse input. Scripts can read "
                "them with get_axis(name); use get_axis_raw(name) for the value "
                "before the dead zone.");

            ImGui::Spacing();
            if (ImGui::Button("Reset Input Axes", ImVec2(150.0f, 0.0f))) {
                GetInput().ResetDefaults();
                m_selectedAxis = 0;
                m_rebindAxis = -1;
                m_rebindBinding = -1;
                SaveInputBindings();
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Defaults: Horizontal, Vertical, Jump, Mouse X/Y, Wheel");

            if (ImGui::Button("Reset Keys to Defaults", ImVec2(190.0f, 0.0f))) {
                GetInput().ResetBindingsToDefaults();
                m_rebindAxis = -1;
                m_rebindBinding = -1;
                SaveInputBindings();
            }
            ImGui::SameLine();
            const std::filesystem::path inputPath = InputBindingsPath();
            ImGui::TextDisabled("Saved to: %s", inputPath.string().c_str());

            auto& axes = GetInput().Axes();
            if (axes.empty()) {
                ImGui::TextDisabled("No axes registered.");
                break;
            }

            if (m_selectedAxis < 0 ||
                m_selectedAxis >= static_cast<int>(axes.size())) {
                m_selectedAxis = 0;
            }

            const ImGuiTableFlags tableFlags =
                ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable("##InputAxes", 3, tableFlags)) {
                ImGui::TableSetupColumn("Axis");
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthFixed,
                                        70.0f);
                ImGui::TableSetupColumn("Sensitivity",
                                        ImGuiTableColumnFlags_WidthFixed, 150.0f);
                ImGui::TableHeadersRow();

                for (int i = 0; i < static_cast<int>(axes.size()); ++i) {
                    Galeshi::InputAxis& axis = axes[static_cast<size_t>(i)];
                    ImGui::PushID(i);
                    ImGui::TableNextRow();

                    ImGui::TableSetColumnIndex(0);
                    if (ImGui::Selectable(axis.name.c_str(),
                                          m_selectedAxis == i,
                                          ImGuiSelectableFlags_None,
                                          ImVec2(0.0f, 22.0f))) {
                        if (m_selectedAxis != i) {
                            m_rebindAxis = -1;
                            m_rebindBinding = -1;
                        }
                        m_selectedAxis = i;
                    }

                    ImGui::TableSetColumnIndex(1);
                    ImGui::Text("%+.2f", axis.value);

                    ImGui::TableSetColumnIndex(2);
                    ImGui::SetNextItemWidth(-1.0f);
                    ImGui::DragFloat("##sensitivity", &axis.sensitivity,
                                     0.01f, 0.01f, 10.0f, "%.2f");
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }

            Galeshi::InputAxis& selected =
                axes[static_cast<size_t>(m_selectedAxis)];
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("%s settings", selected.name.c_str());

            ImGui::SetNextItemWidth(220.0f);
            ImGui::SliderFloat("Dead zone", &selected.deadZone, 0.0f, 0.95f,
                               "%.3f");
            ImGui::Checkbox("Invert value", &selected.invert);

            std::string positive;
            std::string negative;
            for (const Galeshi::InputAxisBinding& binding : selected.bindings) {
                const std::string label =
                    Galeshi::InputAxisSystem::BindingLabel(binding);
                if (binding.direction >= 0.0f) {
                    if (!positive.empty()) positive += ", ";
                    positive += label;
                } else {
                    if (!negative.empty()) negative += ", ";
                    negative += label;
                }
            }
            if (selected.mouseSource != Galeshi::InputMouseSource::None) {
                if (!positive.empty()) positive += ", ";
                positive += Galeshi::InputAxisSystem::MouseSourceLabel(
                    selected.mouseSource);
            }

            ImGui::Text("Positive: %s", positive.empty() ? "None" : positive.c_str());
            ImGui::Text("Negative: %s", negative.empty() ? "None" : negative.c_str());

            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Text("Key bindings for %s", selected.name.c_str());
            ImGui::TextDisabled(
                "Select Horizontal for A/D or Vertical for W/S, then click a "
                "key to rebind it.");
            for (size_t bindingIndex = 0;
                 bindingIndex < selected.bindings.size(); ++bindingIndex) {
                auto& binding = selected.bindings[bindingIndex];
                ImGui::PushID(static_cast<int>(bindingIndex));
                ImGui::TextUnformatted(binding.direction >= 0.0f ? "+" : "-");
                ImGui::SameLine();
                const bool waiting =
                    m_rebindAxis == m_selectedAxis &&
                    m_rebindBinding == static_cast<int>(bindingIndex);
                const std::string bindingLabel =
                    waiting ? "Press a key..."
                            : Galeshi::InputAxisSystem::BindingLabel(binding);
                if (ImGui::Button(bindingLabel.c_str(), ImVec2(170.0f, 0.0f))) {
                    m_rebindAxis = m_selectedAxis;
                    m_rebindBinding = static_cast<int>(bindingIndex);
                    GetInput().BeginKeyCapture();
                }
                ImGui::SameLine();
                if (waiting) {
                    if (ImGui::SmallButton("Cancel")) {
                        m_rebindAxis = -1;
                        m_rebindBinding = -1;
                    }
                } else if (ImGui::SmallButton("Remove")) {
                    selected.bindings.erase(
                        selected.bindings.begin() +
                        static_cast<std::ptrdiff_t>(bindingIndex));
                    m_rebindAxis = -1;
                    m_rebindBinding = -1;
                    SaveInputBindings();
                    ImGui::PopID();
                    break;
                }
                ImGui::PopID();
            }

            if (ImGui::Button("+ Add Key Binding")) {
                Galeshi::InputAxisBinding binding;
                binding.type = Galeshi::InputBindingType::Key;
                binding.code.clear();
                binding.direction = 1.0f;
                selected.bindings.push_back(std::move(binding));
                m_rebindAxis = m_selectedAxis;
                m_rebindBinding = static_cast<int>(selected.bindings.size()) - 1;
                GetInput().BeginKeyCapture();
            }

            const glm::vec2 mouse = GetInput().MousePosition();
            const glm::vec2 mouseDelta = GetInput().MouseDelta();
            ImGui::Spacing();
            ImGui::TextDisabled("Mouse: (%.0f, %.0f)  delta: (%.1f, %.1f)  wheel: %.2f",
                                mouse.x, mouse.y, mouseDelta.x, mouseDelta.y,
                                GetInput().MouseWheel());
            ImGui::TextDisabled(
                "Mouse axes report per-frame deltas; use them directly for "
                "look input.");
            break;
        }

        case PreferencesSection::About: {
            ImGui::Text("About");
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::Text("GaleshiEngine");
            ImGui::TextDisabled("Version 0.3.0 - alpha");
            ImGui::Separator();
            ImGui::TextWrapped(
                "A minimalist ECS game engine and editor written in C++20. "
                "It targets Linux/X11 and uses bgfx with a Vulkan backend.");
            ImGui::Spacing();
            ImGui::TextDisabled("Renderer: bgfx / Vulkan");
            ImGui::TextDisabled("Scripting: Lua (sol2)");
            ImGui::TextDisabled("Dear ImGui %s", IMGUI_VERSION);
            ImGui::Spacing();
            ImGui::TextDisabled("Input axes: %d",
                              static_cast<int>(GetInput().Axes().size()));
            break;
        }
        }

        ImGui::EndChild();
        ImGui::End();

        if (!m_showPreferences) {
            SavePreferences();
        }
    }

    // -------------------------------------------------------------------
    // Profiler
    // -------------------------------------------------------------------
    void ConfigureProfilerViewNames() {
        if (!bgfx::getCaps()) return;
        auto setView = [this](bgfx::ViewId id, const char* name) {
            if (id < m_profilerViewNames.size()) m_profilerViewNames[id] = name;
            bgfx::setViewName(id, name);
        };
        setView(0, "Occlusion Queries");
        setView(1, "Main G-Buffer");
        for (int i = 0; i < 3; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "PSSM Cascade %d", i + 1);
            setView(static_cast<bgfx::ViewId>(2 + i), name);
        }
        for (int face = 0; face < 6; ++face) {
            char name[32];
            std::snprintf(name, sizeof(name), "Point Shadow %d", face + 1);
            setView(static_cast<bgfx::ViewId>(5 + face), name);
        }
        for (int i = 0; i < 4; ++i) {
            char name[32];
            std::snprintf(name, sizeof(name), "Spot Shadow %d", i + 1);
            setView(static_cast<bgfx::ViewId>(11 + i), name);
        }
        setView(15, "AO Evaluate");
        setView(16, "AO Upsample");
        setView(17, "AO Blur H");
        setView(18, "AO Blur V");
        setView(19, "Planar Reflection");
        setView(20, "Deferred Lighting");
        setView(21, "Particles");
        setView(22, "SSGI Trace");
        setView(23, "Motion Velocity");
        setView(24, "SSGI + SSR Composite");
        setView(25, "Motion Blur");
        setView(26, "Atmosphere");
        setView(27, "God Rays");
        setView(28, "DoF CoC");
        setView(29, "DoF Blur H");
        setView(30, "DoF Blur V");
        setView(31, "DoF Composite");
        setView(32, "Bloom Extract");
        setView(33, "Bloom Blur H");
        setView(34, "Bloom Blur V");
        setView(35, "Bloom Combine");
        setView(36, "TAA Resolve");
        setView(37, "TAA History");
        setView(38, "Scene Color/Depth Copy");
        setView(39, "Water & Hair");
        setView(40, "Scene Grade + ACES");
        setView(41, "Environment Palette");
        setView(42, "Vignette");
        setView(43, "Chromatic Aberration");
        setView(44, "SMAA Edge Detection");
        setView(45, "SMAA Blend");
        setView(46, "FXAA");
        setView(47, "Invert");
        setView(48, "Final Scene Copy");
        setView(49, "Glass");
        setView(50, "Debug Lines");
        setView(51, "ImGui");

        // One-time IBL/BRDF passes use view IDs 190..254.
        setView(190, "BRDF LUT");
        for (int face = 0; face < 6; ++face) {
            char name[32];
            std::snprintf(name, sizeof(name), "IBL Sky %d", face + 1);
            setView(static_cast<bgfx::ViewId>(200 + face), name);
        }
        setView(206, "IBL Sky Blit");
        for (int face = 0; face < 6; ++face) {
            char name[32];
            std::snprintf(name, sizeof(name), "IBL Irradiance %d", face + 1);
            setView(static_cast<bgfx::ViewId>(210 + face), name);
        }
        setView(216, "IBL Irradiance Blit");
        for (int mip = 0; mip < 5; ++mip) {
            for (int face = 0; face < 6; ++face) {
                char name[40];
                std::snprintf(name, sizeof(name), "IBL Prefilter Mip %d Face %d",
                              mip, face + 1);
                setView(static_cast<bgfx::ViewId>(220 + mip * 7 + face), name);
            }
            char name[40];
            std::snprintf(name, sizeof(name), "IBL Prefilter Blit Mip %d", mip);
            setView(static_cast<bgfx::ViewId>(220 + mip * 7 + 6), name);
        }
    }

    void UpdateProfilerDebugState() {
        if (m_profilerDebugEnabled == m_showProfiler) return;
        bgfx::setDebug(m_showProfiler ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
        m_profilerDebugEnabled = m_showProfiler;
    }

    void UpdateGpuProfilerStats() {
        m_gpuPasses.clear();
        m_gpuFrameMs = 0.0f;
        const bgfx::Stats* stats = bgfx::getStats();
        if (!stats) return;

        const double gpuFreq = static_cast<double>(stats->gpuTimerFreq);
        const double cpuFreq = static_cast<double>(stats->cpuTimerFreq);
        if (gpuFreq > 0.0) {
            const int64_t elapsed = stats->gpuTimeEnd - stats->gpuTimeBegin;
            if (elapsed > 0) m_gpuFrameMs = static_cast<float>(
                static_cast<double>(elapsed) * 1000.0 / gpuFreq);
        }

        if (!stats->viewStats) return;
        for (uint16_t i = 0; i < stats->numViews; ++i) {
            const bgfx::ViewStats& view = stats->viewStats[i];
            std::string name = view.name;
            if (name.empty() && view.view < m_profilerViewNames.size()) {
                name = m_profilerViewNames[view.view];
            }
            if (name.empty()) name = "View " + std::to_string(view.view);

            float gpuMs = 0.0f;
            float cpuMs = 0.0f;
            if (gpuFreq > 0.0) {
                const int64_t elapsed = view.gpuTimeEnd - view.gpuTimeBegin;
                if (elapsed > 0) gpuMs = static_cast<float>(
                    static_cast<double>(elapsed) * 1000.0 / gpuFreq);
            }
            if (cpuFreq > 0.0) {
                const int64_t elapsed = view.cpuTimeEnd - view.cpuTimeBegin;
                if (elapsed > 0) cpuMs = static_cast<float>(
                    static_cast<double>(elapsed) * 1000.0 / cpuFreq);
            }
            m_gpuPasses.push_back(GpuPassSample{std::move(name), view.view,
                                                gpuMs, cpuMs});
        }

        std::sort(m_gpuPasses.begin(), m_gpuPasses.end(),
                  [](const GpuPassSample& a, const GpuPassSample& b) {
                      if (a.gpuMs != b.gpuMs) return a.gpuMs > b.gpuMs;
                      return a.cpuMs > b.cpuMs;
                  });
    }

    void PushProfilerHistory() {
        PushProfileHistory(m_frameHistory, m_cpuProfile.frameMs);
        PushProfileHistory(m_gameLogicHistory, m_cpuProfile.gameLogicMs);
        PushProfileHistory(m_gpuHistory, m_gpuFrameMs);
    }

    void DrawProfilerWindow() {
        if (!m_showProfiler) return;

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        const ImVec2 center(viewport->Pos.x + viewport->Size.x * 0.5f,
                            viewport->Pos.y + viewport->Size.y * 0.5f);
        ImGui::SetNextWindowPos(center, ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
        ImGui::SetNextWindowSize(
            ImVec2(std::min(720.0f, viewport->Size.x - 40.0f),
                   std::min(620.0f, viewport->Size.y - 40.0f)),
            ImGuiCond_Appearing);
        if (!ImGui::Begin("Profiler", &m_showProfiler,
                          ImGuiWindowFlags_NoCollapse)) {
            ImGui::End();
            return;
        }

        const bgfx::Stats* stats = bgfx::getStats();
        const bool gpuTimerAvailable = stats && stats->gpuTimerFreq > 0;
        const CpuProfileSample& profile = m_cpuProfile;

        ImGui::Text("FPS: %.1f   Frame: %.2f ms", ImGui::GetIO().Framerate,
                    profile.frameMs);
        ImGui::TextDisabled("CPU update %.2f ms | OnImGui %.2f ms | "
                            "Render submit %.2f ms | GPU %.2f ms",
                            profile.updateMs, profile.imguiMs,
                            profile.renderCpuMs, m_gpuFrameMs);
        if (ImGui::Button("Reset History")) {
            m_frameHistory.fill(0.0f);
            m_gameLogicHistory.fill(0.0f);
            m_gpuHistory.fill(0.0f);
        }
        ImGui::SameLine();
        ImGui::TextDisabled("History: %d frames",
                            static_cast<int>(kProfileHistorySize));

        if (ImGui::BeginTabBar("##profiler_tabs")) {
            if (ImGui::BeginTabItem("Overview")) {
                auto plotMax = [](const std::array<float, kProfileHistorySize>& values) {
                    const float maxValue = *std::max_element(values.begin(),
                                                              values.end());
                    return std::max(16.7f, maxValue * 1.15f);
                };

                ImGui::Text("Frame time (ms)");
                ImGui::PlotLines("##cpu_frame_history", m_frameHistory.data(),
                                 static_cast<int>(kProfileHistorySize), 0, nullptr,
                                 0.0f, plotMax(m_frameHistory), ImVec2(-1, 90));
                ImGui::Text("Game logic (ms)");
                ImGui::PlotLines("##game_logic_history", m_gameLogicHistory.data(),
                                 static_cast<int>(kProfileHistorySize), 0, nullptr,
                                 0.0f, plotMax(m_gameLogicHistory), ImVec2(-1, 90));
                ImGui::Text("GPU frame (ms)");
                if (gpuTimerAvailable) {
                    ImGui::PlotLines("##gpu_frame_history", m_gpuHistory.data(),
                                     static_cast<int>(kProfileHistorySize), 0, nullptr,
                                     0.0f, plotMax(m_gpuHistory), ImVec2(-1, 90));
                } else {
                    ImGui::TextDisabled("GPU timer queries are not available on this renderer.");
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Rolling history of the last submitted frames.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("CPU / Game Logic")) {
                const ImGuiTableFlags cpuTableFlags =
                    ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                    ImGuiTableFlags_SizingStretchProp;
                if (ImGui::BeginTable("##cpu_profile_sections", 2, cpuTableFlags)) {
                    ImGui::TableSetupColumn("Section");
                    ImGui::TableSetupColumn("Time (ms)",
                                            ImGuiTableColumnFlags_WidthFixed, 100.0f);
                    ImGui::TableHeadersRow();
                    auto row = [](const char* name, float ms) {
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(name);
                        ImGui::TableSetColumnIndex(1);
                        ImGui::Text("%.2f", ms);
                    };
                    row("Update / Game Logic", profile.gameLogicMs);
                    row("  Physics", profile.physicsMs);
                    row("  Audio", profile.audioMs);
                    row("  Scripts", profile.scriptsMs);
                    row("  AI / Navigation", profile.aiMs);
                    row("  Animation", profile.animationMs);
                    row("  Groom Hair", profile.groomMs);
                    row("  Particles", profile.particlesMs);
                    row("  Camera / Input", profile.cameraMs);
                    row("Update total", profile.updateMs);
                    row("OnImGui / Editor total", profile.imguiMs);
                    row("  Render submit (total)", profile.renderCpuMs);
                    row("    PSSM shadows", profile.pssmMs);
                    row("    Point shadows", profile.pointShadowsMs);
                    row("    Spot shadows", profile.spotShadowsMs);
                    row("    Occlusion", profile.occlusionMs);
                    row("    G-Buffer", profile.gbufferMs);
                    row("    Planar reflection", profile.planarReflectionMs);
                    row("    PostFX", profile.postFxMs);
                    row("    Water", profile.waterMs);
                    row("    Hair", profile.hairMs);
                    const float profiledRender = profile.pssmMs +
                        profile.pointShadowsMs + profile.spotShadowsMs +
                        profile.occlusionMs + profile.gbufferMs +
                        profile.planarReflectionMs + profile.postFxMs +
                        profile.waterMs + profile.hairMs;
                    row("    Other / editor",
                        std::max(0.0f, profile.renderCpuMs - profiledRender));
                    ImGui::EndTable();
                }
                ImGui::Spacing();
                ImGui::TextDisabled("Game logic is the sum of the update subsystems; "
                                    "update total also includes editor overhead.");
                ImGui::TextDisabled("PostFX excludes Water, Hair and debug gizmos; "
                                    "all render sub-timings are CPU command submission.");
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("GPU Render Passes")) {
                if (stats) {
                    ImGui::TextDisabled("Views: %u | Draws: %u | Compute: %u | "
                                        "Textures: %u | GPU frame: %.2f ms",
                                        static_cast<unsigned>(stats->numViews),
                                        stats->numDraw, stats->numCompute,
                                        stats->numTextures, m_gpuFrameMs);
                }
                if (!gpuTimerAvailable) {
                    ImGui::TextDisabled("GPU timer queries are not available on "
                                        "this renderer.");
                } else if (m_gpuPasses.empty()) {
                    ImGui::TextDisabled("Waiting for the first profiled frame...");
                } else {
                    ImGui::BeginChild("##gpu_pass_scroll", ImVec2(0.0f, 0.0f), true);
                    const ImGuiTableFlags gpuTableFlags =
                        ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg |
                        ImGuiTableFlags_SizingStretchProp;
                    if (ImGui::BeginTable("##gpu_profile_passes", 4, gpuTableFlags)) {
                        ImGui::TableSetupColumn("Render Pass",
                                                ImGuiTableColumnFlags_WidthStretch);
                        ImGui::TableSetupColumn("View",
                                                ImGuiTableColumnFlags_WidthFixed, 48.0f);
                        ImGui::TableSetupColumn("GPU ms",
                                                ImGuiTableColumnFlags_WidthFixed, 75.0f);
                        ImGui::TableSetupColumn("CPU ms",
                                                ImGuiTableColumnFlags_WidthFixed, 75.0f);
                        ImGui::TableHeadersRow();
                        for (const GpuPassSample& pass : m_gpuPasses) {
                            ImGui::TableNextRow();
                            ImGui::TableSetColumnIndex(0);
                            ImGui::TextUnformatted(pass.name.c_str());
                            ImGui::TableSetColumnIndex(1);
                            ImGui::Text("%u", static_cast<unsigned>(pass.view));
                            ImGui::TableSetColumnIndex(2);
                            ImGui::Text("%.3f", pass.gpuMs);
                            ImGui::TableSetColumnIndex(3);
                            ImGui::Text("%.3f", pass.cpuMs);
                        }
                        ImGui::EndTable();
                    }
                    ImGui::EndChild();
                }
                ImGui::TextDisabled("Per-pass GPU timings are from the previous "
                                    "submitted frame.");
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::End();
    }

    void DrawMainMenuBar() {
        // The initial 22 px badge plus the requested size increases.
        constexpr float kLogoSize = 57.0f;
        constexpr float kLogoMargin = 2.0f;
        const float menuBarHeight = kLogoSize + kLogoMargin * 2.0f;
        const ImGuiStyle& style = ImGui::GetStyle();
        const float tallFramePaddingY =
            std::max(style.FramePadding.y,
                     (menuBarHeight - ImGui::GetFontSize()) * 0.5f);

        // BeginMainMenuBar derives the bar height from FramePadding, so use
        // tall padding only while the bar is being created.
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,
                            ImVec2(style.FramePadding.x, tallFramePaddingY));
        const bool menuBarOpen = ImGui::BeginMainMenuBar();
        ImGui::PopStyleVar();
        if (!menuBarOpen) return;

        std::string recentSceneToLoad;
        const float barHeight = ImGui::GetWindowSize().y;
        const float rowHeight = ImGui::GetFrameHeight();
        const float rowOffset = (barHeight - rowHeight) * 0.5f;

        // Restore the normal menu-row metrics used by items and popups.
        ImGuiWindow* menuBar = ImGui::GetCurrentWindow();
        menuBar->DC.CurrLineSize.y = rowHeight;
        menuBar->DC.CurrLineTextBaseOffset = style.FramePadding.y;
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + rowOffset);

        // Round engine badge in the top-left corner.
        if (m_logoTexture && m_logoTexture->Valid()) {
            const ImVec2 cursor = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(kLogoSize, rowHeight));

            const ImVec2 logoMin(cursor.x, cursor.y - rowOffset + kLogoMargin);
            const ImVec2 logoMax(logoMin.x + kLogoSize, logoMin.y + kLogoSize);
            // Texture::LoadFromFile flips rows for mesh UVs; undo that for
            // ImGui, whose texture origin is the top-left corner.
            ImGui::GetWindowDrawList()->AddImage(
                static_cast<ImTextureID>(m_logoTexture->Handle().idx), logoMin, logoMax,
                ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            if (ImGui::IsMouseHoveringRect(logoMin, logoMax)) {
                ImGui::SetTooltip("Galeshi Engine");
            }
            ImGui::SameLine(0.0f, 6.0f);
        }

        // Play/Stop
        if (ImGui::Button(m_editor.isPlaying ? "Stop" : "Play", ImVec2(60, 0))) {
            // Never show the previous mode's cached viewport after Play/Stop.
            m_lastViewportTexture = BGFX_INVALID_HANDLE;
            if (!m_editor.isPlaying) {
                PushHistory();
                m_editor.isPlaying = true;
                EnsurePhysicsBodies();
                ActivateGameplayCamera();
            } else {
                StopPlay();
            }
        }
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

                if (ImGui::BeginMenu("Post-Process")) {
            // Edit the active source (highest-priority volume or global
            // settings). The manager toggles are kept in sync so disabling
            // the volume falls back to the same global state.
            auto& pp = ActivePostProcessSettings();
            ImGui::TextDisabled(
                "Editing: %s",
                ActivePostProcessVolume() ? "active Post-Process Volume"
                                          : "global Post-Process settings");
            ImGui::Separator();
            auto menuToggle = [&](const char* label, bool& value,
                                  void (Galeshi::PostProcessManager::*setter)(bool)) {
                bool next = value;
                if (ImGui::MenuItem(label, nullptr, &next)) {
                    value = next;
                    (m_postProcess.*setter)(next);
                }
            };

            menuToggle("Bloom", pp.bloomEnabled,
                       &Galeshi::PostProcessManager::SetBloomEnabled);
            menuToggle("Invert Colors", pp.invertEnabled,
                       &Galeshi::PostProcessManager::SetInvertEnabled);
            menuToggle("FXAA", pp.fxaaEnabled,
                       &Galeshi::PostProcessManager::SetFxaaEnabled);
            if (ImGui::BeginMenu("Ambient Occlusion")) {
                menuToggle("Enabled", pp.ssaoEnabled,
                           &Galeshi::PostProcessManager::SetSsaoEnabled);
                ImGui::Separator();
                if (ImGui::MenuItem("SSAO", nullptr,
                        pp.aoMode == Galeshi::AmbientOcclusionMode::SSAO)) {
                    pp.aoMode = Galeshi::AmbientOcclusionMode::SSAO;
                    m_globalAoMode = pp.aoMode;
                }
                if (ImGui::MenuItem("HBAO", nullptr,
                        pp.aoMode == Galeshi::AmbientOcclusionMode::HBAO)) {
                    pp.aoMode = Galeshi::AmbientOcclusionMode::HBAO;
                    m_globalAoMode = pp.aoMode;
                }
                if (ImGui::MenuItem("GTAO", nullptr,
                        pp.aoMode == Galeshi::AmbientOcclusionMode::GTAO)) {
                    pp.aoMode = Galeshi::AmbientOcclusionMode::GTAO;
                    m_globalAoMode = pp.aoMode;
                }
                ImGui::EndMenu();
            }
            menuToggle("SMAA", pp.smaaEnabled,
                       &Galeshi::PostProcessManager::SetSmaaEnabled);
            menuToggle("TAA", pp.taaEnabled,
                       &Galeshi::PostProcessManager::SetTaaEnabled);
            menuToggle("Depth of Field", pp.dofEnabled,
                       &Galeshi::PostProcessManager::SetDofEnabled);
            menuToggle("Motion Blur", pp.motionBlurEnabled,
                       &Galeshi::PostProcessManager::SetMotionBlurEnabled);
            menuToggle("Atmosphere", pp.atmosphereEnabled,
                       &Galeshi::PostProcessManager::SetAtmosphereEnabled);
            menuToggle("Volumetric Light", pp.godRaysEnabled,
                       &Galeshi::PostProcessManager::SetGodRaysEnabled);
            menuToggle("Fog", pp.fogEnabled,
                       &Galeshi::PostProcessManager::SetFogEnabled);
            if (ImGui::MenuItem("Volumetric Fog", nullptr,
                                &pp.fogVolumetric)) {
                m_postProcess.SetFogVolumetric(pp.fogVolumetric);
                if (pp.fogVolumetric) {
                    pp.fogEnabled = true;
                    m_postProcess.SetFogEnabled(true);
                }
            }
            menuToggle("SSR", pp.ssrEnabled,
                       &Galeshi::PostProcessManager::SetSsrEnabled);
            menuToggle("SSGI (Indirect Lighting)", pp.ssgiEnabled,
                       &Galeshi::PostProcessManager::SetSsgiEnabled);
            ImGui::Separator();
            ImGui::MenuItem("Environment Palette...", nullptr,
                            &m_showEnvironmentPalette);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset All Effects")) {
                pp.invertEnabled = false;
                pp.fxaaEnabled = false;
                pp.ssaoEnabled = false;
                pp.ssrEnabled = false;
                pp.ssgiEnabled = false;
                pp.fogEnabled = false;
                pp.fogVolumetric = false;
                m_postProcess.SetInvertEnabled(false);
                m_postProcess.SetFxaaEnabled(false);
                m_postProcess.SetSsaoEnabled(false);
                m_postProcess.SetSsrEnabled(false);
                m_postProcess.SetSsgiEnabled(false);
                m_postProcess.SetFogEnabled(false);
                m_postProcess.SetFogVolumetric(false);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Rendering")) {
            bool realtime = m_realtimeRendering;
            if (ImGui::MenuItem("Realtime Rendering", nullptr, &realtime)) {
                m_realtimeRendering = realtime;
                // The camera can move while paused; do not reuse stale TAA
                // history when the viewport starts rendering again.
                m_postProcess.ResetTaaHistory();
                spdlog::info("[Render] Render passes {}",
                             realtime ? "resumed; scene updates unchanged"
                                      : "paused; scene updates continue");
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Skip GPU render passes only. Physics, animation, scripts, "
                    "AI, particles and scene time keep running.\n"
                    "The viewport shows the last completed frame until "
                    "rendering is resumed.");
            }
            ImGui::Separator();

            bool lodEnabled = m_render->LodEnabled();
            if (ImGui::MenuItem("LOD", nullptr, &lodEnabled))
                m_render->SetLodEnabled(lodEnabled);

            bool occlusionEnabled = m_render->OcclusionCullingEnabled();
            if (ImGui::MenuItem("Occlusion Culling", nullptr, &occlusionEnabled))
                m_render->SetOcclusionCullingEnabled(occlusionEnabled);

            float lodBias = m_render->LodBias();
            if (ImGui::SliderFloat("LOD Bias", &lodBias, 0.25f, 4.0f, "%.2f"))
                m_render->SetLodBias(lodBias);

            if (ImGui::BeginMenu("Resolution")) {
                if (m_dynamicResolution) ImGui::BeginDisabled();
                ImGui::SliderFloat("Render Scale", &m_renderScale,
                                   0.5f, 1.0f, "%.2f");
                if (m_dynamicResolution) ImGui::EndDisabled();
                if (ImGui::Checkbox("Dynamic Resolution", &m_dynamicResolution) &&
                    m_dynamicResolution) {
                    m_dynamicScale = m_renderScale;
                    m_dynamicFrameCounter = 0;
                }
                if (m_dynamicResolution) {
                    ImGui::Text("Dynamic scale: %.2f", m_dynamicScale);
                    ImGui::SliderFloat("Minimum Scale", &m_dynamicMinScale,
                                       0.5f, 1.0f, "%.2f");
                    ImGui::DragFloat("Target FPS", &m_dynamicTargetFps,
                                     0.5f, 30.0f, 144.0f, "%.0f");
                }
                ImGui::EndMenu();
            }

            if (ImGui::BeginMenu("PSSM Shadows")) {
                ImGui::DragFloat("Depth Bias (texels)", &m_pssmDepthBiasTexels,
                                 0.01f, 0.0f, 4.0f, "%.2f");
                ImGui::DragFloat("Slope Bias", &m_pssmSlopeBias,
                                 0.00001f, 0.0f, 0.002f, "%.5f");
                ImGui::DragFloat("Normal Offset (texels)",
                                 &m_pssmNormalOffsetTexels,
                                 0.05f, 0.0f, 4.0f, "%.2f");
                ImGui::SliderFloat("Normal Slope", &m_pssmNormalOffsetSlope,
                                   0.0f, 2.0f, "%.2f");
                ImGui::SliderFloat("Shadow Sharpness",
                                   &m_pssmShadowSharpness,
                                   0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("1.0 = hard single-tap shadows, 0.0 = soft PCF.\n"
                                      "This is a global directional-shadow setting.");
                }
                ImGui::TextDisabled("PCF radius: %.2f texels", PssmPcfRadius());
                bool sharpShadows = m_pssmShadowSharpness >= 0.999f;
                if (ImGui::Checkbox("Sharp Shadows", &sharpShadows)) {
                    m_pssmShadowSharpness = sharpShadows ? 1.0f : 0.625f;
                }
                if (ImGui::Button("Reset PSSM Tuning")) {
                    m_pssmDepthBiasTexels = 0.75f;
                    m_pssmSlopeBias = 0.00025f;
                    m_pssmNormalOffsetTexels = 1.75f;
                    m_pssmNormalOffsetSlope = 0.5f;
                    m_pssmShadowSharpness = 0.625f;
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();
            ImGui::Text("Occluded: %u", m_render->OccludedObjectCount());
            ImGui::EndMenu();
        }
if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("New Scene", "Ctrl+N")) NewScene();
            if (ImGui::MenuItem("Load Scene...", "Ctrl+O")) LoadScene();
            if (ImGui::BeginMenu("Recent Scenes")) {
                if (m_recentScenes.empty()) {
                    ImGui::MenuItem("No recent scenes", nullptr, false, false);
                } else {
                    for (size_t i = 0; i < m_recentScenes.size(); ++i) {
                        const std::string scenePath = m_recentScenes[i];
                        std::error_code ec;
                        const bool exists =
                            std::filesystem::is_regular_file(scenePath, ec);
                        std::string label = RecentSceneLabel(scenePath);
                        if (label.empty()) label = scenePath;
                        if (!exists) label += " (missing)";

                        ImGui::PushID(static_cast<int>(i));
                        if (ImGui::MenuItem(
                                label.c_str(), nullptr,
                                scenePath == m_currentScenePath, exists)) {
                            recentSceneToLoad = scenePath;
                        }
                        if (ImGui::IsItemHovered(
                                ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip(
                                "%s%s", scenePath.c_str(),
                                exists ? "\nClick to load."
                                       : "\nFile not found.");
                        }
                        ImGui::PopID();
                    }
                    ImGui::Separator();
                    if (ImGui::MenuItem("Clear Recent Scenes")) {
                        ClearRecentScenes();
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
            if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                SaveScene(true);
            }
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) OpenFileDialog();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) RequestExit();
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) Redo();
            if (ImGui::MenuItem("History...")) m_showHistoryModal = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Translate", "W")) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::MenuItem("Rotate",    "E")) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::MenuItem("Scale",     "R")) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...", "Ctrl+,")) m_showPreferences = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Audio")) {
            ImGui::MenuItem("Audio Mixer", nullptr, &m_showAudioMixer);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            // Every create action records the pre-action scene so Undo steps
            // through construction operations, not only gizmo edits.
            auto createItem = [&](const char* label) {
                if (!ImGui::MenuItem(label)) return false;
                PushHistory();
                return true;
            };
            if (createItem("Empty Entity"))
                m_editor.Select(m_scene.CreateEntity("New Entity"));

            if (ImGui::BeginMenu("UI")) {
                if (ImGui::MenuItem("UI Canvas")) CreateUICanvas("UICanvas");
                ImGui::Separator();
                if (ImGui::MenuItem("Main Menu"))    CreateUICanvas("MainMenu", 0);
                if (ImGui::MenuItem("HUD"))          CreateUICanvas("HUD_Root", 1);
                if (ImGui::MenuItem("Pause Menu"))   CreateUICanvas("PauseMenu", 2);
                if (ImGui::MenuItem("Loading Modal"))CreateUICanvas("LoadingModal", 3);
                if (ImGui::MenuItem("Settings"))     CreateUICanvas("Settings", 4);
                if (ImGui::MenuItem("Inventory"))    CreateUICanvas("Inventory", 5);
                if (ImGui::MenuItem("Dialogue"))     CreateUICanvas("Dialogue", 6);
                ImGui::EndMenu();
            }
            
            if (createItem("Landscape")) {
                // Only one landscape is allowed per scene. If one already
                // exists, select it instead of creating a duplicate.
                auto landscapeView =
                    m_scene.Registry().view<Galeshi::LandscapeComponent>();
                if (!landscapeView.empty()) {
                    const entt::entity existing = *landscapeView.begin();
                    m_editor.Select(Galeshi::Entity(existing, &m_scene));
                    spdlog::warn("[Editor] Landscape already exists; duplicate discarded");
                } else {
                    auto e = m_scene.CreateEntity("Landscape");
                    auto& lc = e.AddComponent<Galeshi::LandscapeComponent>();
                    lc.sizeX = 100; lc.sizeZ = 100; lc.scale = 1.0f;
                    lc.heights.resize(lc.sizeX * lc.sizeZ, 0.0f);

                    auto mesh = std::make_shared<Galeshi::Mesh>();
                    Galeshi::LandscapeMesh::Rebuild(lc, *mesh);

                    e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "landscape" });

                    auto mat = std::make_shared<Galeshi::Material>();
                    mat->albedoFactor = {0.4f, 0.6f, 0.3f, 1.0f};
                    mat->roughnessFactor = 0.9f;
                    e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

                    auto& veg = e.AddComponent<Galeshi::VegetationComponent>();
                    Galeshi::VegetationGenerator::Generate(lc, veg);
                    spdlog::info("[Editor] Landscape vegetation: {} trees, {} grass instances",
                                 veg.treeInstances.size(), veg.grassInstances.size());

                    m_editor.Select(e);
                }
            }
            if (createItem("Cube")) {
                auto e = MakeCube("Cube", {0, 0, 0});
                m_editor.Select(e);
            }
            if (createItem("Camera")) {
                auto e = m_scene.CreateEntity("Camera");
                auto& camera = e.AddComponent<Galeshi::CameraComponent>();
                auto& tf = e.GetComponent<Galeshi::TransformComponent>();
                auto selected = m_editor.Selected(&m_scene);
                if (selected && IsSkeletalCharacter(selected)) {
                    // Convenient default for character cameras: attach at
                    // eye height. Move local Position in Details/Viewport for
                    // first- or third-person distance.
                    e.AddComponent<Galeshi::ParentComponent>(
                        Galeshi::ParentComponent{ selected.Handle() });
                    tf.position = {0.0f, 1.6f, 0.0f};
                    camera.priority = 1;
                } else {
                    tf.position = m_editor.camTarget;
                }
                m_editor.Select(e);
            }
            if (createItem("Physics Sphere")) {
                auto e = SpawnPhysicsSphere("PhysicsSphere", {0, 5, 0}, 0.5f);
                m_editor.Select(e);
            }
            if (createItem("Physics Capsule")) {
                auto e = SpawnPhysicsCapsule("PhysicsCapsule", {0, 5, 0}, 0.5f, 0.5f);
                m_editor.Select(e);
            }
            if (ImGui::BeginMenu("Collision Shapes")) {
                if (createItem("Box"))    SpawnCollisionShape("CollisionBox", Galeshi::ColliderShapeType::Box);
                if (createItem("Sphere")) SpawnCollisionShape("CollisionSphere", Galeshi::ColliderShapeType::Sphere);
                if (createItem("Capsule"))SpawnCollisionShape("CollisionCapsule", Galeshi::ColliderShapeType::Capsule);
                ImGui::EndMenu();
            }
            if (createItem("Cloth (SoftBody)")) {
                auto e = m_scene.CreateEntity("Cloth");
                auto& sb = e.AddComponent<Galeshi::SoftBodyComponent>();
                sb.gridX = 10; sb.gridY = 10;
                sb.origin = {0, 5, 0};
                sb.softBodyId = m_physics->CreateCloth(sb.gridX, sb.gridY, sb.spacing, sb.origin, static_cast<entt::entity>(e));

                auto mesh = std::make_shared<Galeshi::Mesh>();
                std::vector<Galeshi::Vertex> verts;
                std::vector<uint32_t> idxs;
                for (int y = 0; y < sb.gridY; ++y) {
                    for (int x = 0; x < sb.gridX; ++x) {
                        Galeshi::Vertex v{};
                        v.position = sb.origin + glm::vec3(x * sb.spacing, 0, y * sb.spacing);
                        v.normal = glm::vec3(0, 1, 0);
                        v.uv = glm::vec2(float(x)/sb.gridX, float(y)/sb.gridY);
                        verts.push_back(v);
                    }
                }
                for (int y = 0; y < sb.gridY - 1; ++y) {
                    for (int x = 0; x < sb.gridX - 1; ++x) {
                        uint32_t i0 = y * sb.gridX + x;
                        uint32_t i1 = i0 + 1;
                        uint32_t i2 = i0 + sb.gridX;
                        uint32_t i3 = i2 + 1;
                        idxs.push_back(i0); idxs.push_back(i2); idxs.push_back(i1);
                        idxs.push_back(i1); idxs.push_back(i2); idxs.push_back(i3);
                    }
                }
                mesh->Upload(verts, idxs);
                e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "cloth" });
                auto mat = std::make_shared<Galeshi::Material>();
                mat->albedoFactor = {0.8f, 0.2f, 0.2f, 1.0f};
                e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

                m_editor.Select(e);
            }
            if (createItem("Physics Cube")) {
                auto e = SpawnPhysicsCube("PhysicsCube", {0, 5, 0});
                m_editor.Select(e);
            }
            if (createItem("AI Agent (Behavior Tree)")) {
                auto e = m_scene.CreateEntity("AI_Agent_BT");
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 1.15f, 0};
                tc.scale = {1.0f, 1.0f, 1.0f};

                auto mesh = m_assets->CreateCapsuleMesh(0.8f, 0.35f);
                e.AddComponent<Galeshi::MeshComponent>(
                    Galeshi::MeshComponent{ mesh, "", "capsule" });
                auto mat = std::make_shared<Galeshi::Material>();
                mat->name = "AI_Agent_Material";
                mat->albedoFactor = {0.75f, 0.18f, 0.12f, 1.0f};
                mat->roughnessFactor = 0.7f;
                e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });

                e.AddComponent<Galeshi::NavAgentComponent>();
                e.AddComponent<Galeshi::PerceptionComponent>();
                auto& tree = e.AddComponent<Galeshi::BehaviorTreeComponent>();
                tree = Galeshi::BehaviorTreeSystem::MakeDefaultBehaviorTree();
                m_editor.Select(e);
            }
            if (createItem("AI Agent (Lua Patrol)")) {
                auto e = m_scene.CreateEntity("AI_Agent");
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 0, 0};
                tc.scale = {1.0f, 1.0f, 1.0f};
                
                e.AddComponent<Galeshi::NavAgentComponent>();
                auto& aic = e.AddComponent<Galeshi::AIControllerComponent>();
                aic.scriptPath = "assets/scripts/ai_patrol.lua";
                m_editor.Select(e);
            }
                if (ImGui::MenuItem("Build NavMesh")) {
                    spdlog::info("[Editor] Build NavMesh menu clicked");
                    if (!m_navSystem) {
                        spdlog::error("[Editor] m_navSystem is NULL");
                    } else {
                        if (m_navSystem->BuildFromScene(m_scene)) {
                            spdlog::info("[Editor] NavMesh generated");
                        } else {
                            spdlog::error("[Editor] BuildFromScene failed");
                        }
                    }
                    spdlog::info("[Editor] Build NavMesh menu done");
                }
                if (createItem("Directional Light")) {
                auto e = m_scene.CreateEntity("DirectionalLight");
                auto& dl = e.HasComponent<Galeshi::DirectionalLightComponent>()
                           ? e.GetComponent<Galeshi::DirectionalLightComponent>()
                           : e.AddComponent<Galeshi::DirectionalLightComponent>();
                dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
                dl.intensity = 3.0f;
                m_editor.Select(e);
            }
            if (createItem("Point Light")) {
                auto e = m_scene.CreateEntity("PointLight");
                auto& pl = e.HasComponent<Galeshi::PointLightComponent>()
                           ? e.GetComponent<Galeshi::PointLightComponent>()
                           : e.AddComponent<Galeshi::PointLightComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 2, 0};
                m_editor.Select(e);
            }
            if (createItem("Spot Light")) {
                auto e = m_scene.CreateEntity("SpotLight");
                auto& sl = e.HasComponent<Galeshi::SpotLightComponent>()
                           ? e.GetComponent<Galeshi::SpotLightComponent>()
                           : e.AddComponent<Galeshi::SpotLightComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 3, 0};
                m_editor.Select(e);
            }
            if (createItem("Particle Emitter")) {
                auto e = m_scene.CreateEntity("ParticleEmitter");
                e.AddComponent<Galeshi::ParticleEmitterComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 2, 0};
                m_editor.Select(e);
            }
            if (createItem("Water Surface")) {
                auto e = m_scene.CreateEntity("Water");
                e.AddComponent<Galeshi::WaterComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {m_editor.camTarget.x, 0.0f, m_editor.camTarget.z};
                m_editor.Select(e);
            }
            {
                Galeshi::Entity selected = m_editor.Selected(&m_scene);
                const bool canAttachGroom =
                    selected && IsSkeletalCharacter(selected);
                if (ImGui::MenuItem("Groom Hair", nullptr, false,
                                    canAttachGroom)) {
                    PushHistory();
                    selected.AddComponent<Galeshi::GroomComponent>();
                    m_editor.Select(selected);
                }
                if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) &&
                    !canAttachGroom) {
                    ImGui::SetTooltip(
                        "Select a skeletal character or one of its skinned meshes.");
                }
            }
            if (createItem("Audio Source")) {
                auto e = m_scene.CreateEntity("AudioSource");
                e.AddComponent<Galeshi::AudioSourceComponent>();
                e.GetComponent<Galeshi::TransformComponent>().position =
                    m_editor.camTarget;
                m_editor.Select(e);
            }
            if (createItem("Audio Listener")) {
                auto e = m_scene.CreateEntity("AudioListener");
                e.AddComponent<Galeshi::AudioListenerComponent>();
                e.GetComponent<Galeshi::TransformComponent>().position =
                    m_editor.camTarget;
                m_editor.Select(e);
            }
            if (createItem("Post-Process Volume")) {
                auto e = m_scene.CreateEntity("PostProcessVolume");
                auto& volume =
                    e.AddComponent<Galeshi::PostProcessVolumeComponent>();
                e.GetComponent<Galeshi::TransformComponent>().position =
                    m_editor.camTarget;
                m_editor.Select(e);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Animation")) {
            ImGui::MenuItem("State Machine Editor", nullptr,
                            &m_showStateMachineEditor);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Testing")) {
            if (ImGui::MenuItem("Reset Layout")) m_resetLayout = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Build Scene")) {
                // Build Scene replaces the current document, not append to it.
                // Unload scene-owned runtime state and GPU assets first, then
                // drop the previous snapshot history so Undo cannot restore
                // the scene that was just replaced.
                UnloadSceneContent();
                m_history.clear();
                m_historyIndex = -1;
                m_uiCanvasEntity = entt::null;
                m_uiWidgetIndex = -1;
                m_scene.AudioBuses().fill(Galeshi::AudioBusSettings{});
                m_postProcess.ResetTaaHistory();
                spdlog::info("[Editor] Build Scene: previous scene cleared");
                StartAsyncLoad("Built Scene", [this]() {
                    BuildTestScene();
                    PushHistory();
                });
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Stats")) {
            ImGui::MenuItem("Output Log", nullptr, &m_showOutputLog);
            ImGui::MenuItem("Profiler", "F8", &m_showProfiler);
            ImGui::MenuItem("AI Perception Debug", nullptr, &m_showAIDebug);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();

        // Start loading after the menu popup has been closed, so the loading
        // state cannot invalidate the menu while it is still being drawn.
        if (!recentSceneToLoad.empty()) {
            LoadSceneFromPath(recentSceneToLoad);
        }
    }

    // -------------------------------------------------------------------
    // Undo / Redo
    // -------------------------------------------------------------------
    void PushHistory() {
        if (m_isRestoringHistory) return;
        std::string snap = m_serializer->SaveToString();
        if (m_historyIndex >= 0 && m_history[m_historyIndex] == snap) return;

        m_history.resize(m_historyIndex + 1);
        m_history.push_back(snap);
        m_historyIndex = (int)m_history.size() - 1;

        if (m_history.size() > 50) {
            m_history.erase(m_history.begin());
            m_historyIndex--;
        }
    }

    void Undo() {
        // Some actions (delete, replace, create) snapshot the pre-action
        // state; make the current post-action state the redo point first so
        // one Undo returns to that pre-action state instead of skipping it.
        PushHistory();
        if (m_historyIndex <= 0) return;
        m_historyIndex--;
        m_isRestoringHistory = true;
        m_suppressHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        // Normalize the restored snapshot so the next comparison does not
        // mistake serialization order for a new user edit.
        m_history[m_historyIndex] = m_serializer->SaveToString();
        if (m_particles) m_particles->Reset();
        ReleaseUnusedSceneAssets();
        m_isRestoringHistory = false;
    }

    void Redo() {
        // A change made after Undo invalidates the redo tail.
        PushHistory();
        if (m_historyIndex + 1 >= (int)m_history.size()) return;
        m_historyIndex++;
        m_isRestoringHistory = true;
        m_suppressHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        m_history[m_historyIndex] = m_serializer->SaveToString();
        if (m_particles) m_particles->Reset();
        ReleaseUnusedSceneAssets();
        m_isRestoringHistory = false;
    }

    // -------------------------------------------------------------------
    // Save / Load / Delete
    // -------------------------------------------------------------------
    void SaveSceneToPath(const std::string& path) {
        if (!m_serializer || path.empty()) return;
        CaptureSceneViewState();
        SyncCodeEditorStateToSerializer();
        if (m_browser) {
            std::error_code ec;
            const std::filesystem::path relative = std::filesystem::relative(
                m_browser->CurrentDirectory(), m_browser->RootDirectory(), ec);
            m_serializer->SetContentBrowserPath(
                ec ? std::string{} : relative.generic_string());
        }
        if (m_serializer->Save(path)) {
            m_currentScenePath =
                std::filesystem::absolute(path).lexically_normal().string();
            AddRecentScene(m_currentScenePath);
            spdlog::info("[Editor] Scene saved to {}", m_currentScenePath);
        } else {
            spdlog::error("[Editor] Failed to save scene to {}", path);
        }
    }

    void SaveScene(bool saveAs = false) {
        if (!saveAs && !m_currentScenePath.empty()) {
            SaveSceneToPath(m_currentScenePath);
            return;
        }
        const std::string start = SceneStartDirectory();
        const std::string fileName = m_currentScenePath.empty()
            ? "scene.json"
            : std::filesystem::path(m_currentScenePath).filename().string();
        m_filePickerSave.SaveAs(
            start, fileName, saveAs ? "Save Scene As" : "Save Scene",
            { ".json" });
    }
    
    void LoadScene() {
        m_scenePicker.Open(SceneStartDirectory(), "Load Scene", { ".json" });
    }

    void StopPlay() {
        if (!m_editor.isPlaying) return;
        EndEditorMouseLook();
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
        m_editor.isPlaying = false;
        m_scripts.ClearTriggerEvents();
        m_audio.StopAll();
        if (m_physics) m_physics->Reset();
        if (m_particles) m_particles->Reset();

        // The Play snapshot recreates every entity, invalidating the ASME
        // selection handle. Remember the character name so the editor can
        // stay on the same character after the scene is restored.
        std::string stateMachineCharacterName;
        if (m_stateMachineEditorEntity != entt::null) {
            auto& registry = m_scene.Registry();
            if (registry.valid(m_stateMachineEditorEntity) &&
                registry.all_of<Galeshi::NameComponent>(
                    m_stateMachineEditorEntity)) {
                stateMachineCharacterName =
                    registry.get<Galeshi::NameComponent>(
                        m_stateMachineEditorEntity).name;
            }
        }

        if (m_serializer && m_historyIndex >= 0 &&
            m_historyIndex < static_cast<int>(m_history.size())) {
            m_serializer->LoadFromString(m_history[m_historyIndex]);
        }

        if (!stateMachineCharacterName.empty()) {
            Galeshi::Entity restored =
                m_scene.FindEntityByName(stateMachineCharacterName);
            if (restored) {
                m_stateMachineEditorEntity = restored.Handle();
            }
        }
        DeactivateGameplayCamera();
    }

    void StopPlayForSceneChange() {
        if (!m_editor.isPlaying && !m_usingGameCamera) return;
        EndEditorMouseLook();
        const bool wasPlaying = m_editor.isPlaying;
        m_editor.isPlaying = false;
        if (m_usingGameCamera) DeactivateGameplayCamera();
        if (wasPlaying) {
            m_scripts.ClearTriggerEvents();
            m_audio.StopAll();
            if (m_physics) m_physics->Reset();
            if (m_particles) m_particles->Reset();
        }
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
    }

    void LoadSceneFromPath(const std::string& path) {
        if (m_loadingState.isLoading) return;
        const std::string normalized = NormalizeRecentScenePath(path);
        if (normalized.empty()) return;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(normalized, ec)) {
            spdlog::error("[Editor] Scene not found: {}", normalized);
            RemoveRecentScene(normalized);
            return;
        }

        StopPlayForSceneChange();
        m_editor.ClearSelection();
        StartAsyncLoad(normalized);
    }

    void NewScene() {
        if (!m_assets || !m_serializer) return;
        spdlog::info("[Editor] New Scene: releasing previous scene and GPU assets");

        // This is the same safe replacement path used by Build Scene and
        // cancelled loads: entities release their shared resources first,
        // then AssetManager::Clear destroys the cached meshes and textures.
        // Skip the intermediate undo snapshot; the empty scene gets a clean
        // snapshot after all scene-level state has been reset below.
        UnloadSceneContent(false);

        m_scene.AudioBuses().fill(Galeshi::AudioBusSettings{});
        m_scene.CameraState() = Galeshi::SceneCameraState{};
        m_scene.GlobalPostProcess() = Galeshi::PostProcessSettings{};
        m_scene.SetHasGlobalPostProcess(false);
        m_postProcess.ResetTaaHistory();
        m_iblGenerated = false;
        m_lastIblSun = {999.0f, 999.0f, 999.0f};

        m_history.clear();
        m_historyIndex = -1;
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        m_lastViewportTexture = BGFX_INVALID_HANDLE;
        RestoreEditorCamera();

        PushHistory();
        spdlog::info("[Editor] New empty scene created; previous VRAM resources released");
    }

    void CollectEntityHierarchy(entt::entity root,
                                std::unordered_set<entt::entity>& out) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(root) || !out.insert(root).second) return;
        auto childView = reg.view<Galeshi::ParentComponent>();
        for (auto child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent == root) {
                CollectEntityHierarchy(child, out);
            }
        }
    }

    bool IsSkeletalCharacter(Galeshi::Entity entity) {
        if (!entity) return false;
        auto& registry = m_scene.Registry();
        entt::entity current = entity.Handle();
        int depth = 0;
        while (registry.valid(current) && depth++ < 128) {
            if (registry.all_of<Galeshi::SkeletalComponent>(current)) {
                return true;
            }
            if (!registry.all_of<Galeshi::ParentComponent>(current)) break;
            current = registry.get<Galeshi::ParentComponent>(current).parent;
        }
        return false;
    }

    void ReleaseUnusedSceneAssets() {
        if (!m_assets) return;
        std::vector<std::string> retainedModels;
        auto skeletonView = m_scene.Registry().view<Galeshi::SkeletalComponent>();
        for (auto e : skeletonView) {
            const auto& skeleton = skeletonView.get<Galeshi::SkeletalComponent>(e);
            if (!skeleton.sourcePath.empty()) {
                retainedModels.push_back(skeleton.sourcePath);
            }
        }
        auto vegetationView =
            m_scene.Registry().view<Galeshi::VegetationComponent>();
        for (auto e : vegetationView) {
            const auto& vegetation =
                vegetationView.get<Galeshi::VegetationComponent>(e);
            if (!vegetation.treeMeshPath.empty()) {
                retainedModels.push_back(vegetation.treeMeshPath);
            }
            for (const auto& type : vegetation.treeTypes) {
                if (!type.meshPath.empty()) {
                    retainedModels.push_back(type.meshPath);
                }
            }
        }
        m_assets->ReleaseUnusedAssets(retainedModels);
    }

    void DeleteSelected() {
        if (!m_editor.HasSelection()) return;

        std::unordered_set<entt::entity> toDestroy;
        for (auto entity : m_editor.SelectedAll(&m_scene)) {
            // A skeletal mesh owns its imported child hierarchy. Deleting an
            // individual child would corrupt the character model; only the
            // skeletal root itself may be deleted as a whole.
            if (HasSkeletalParent(entity.Handle())) {
                spdlog::warn("[Editor] Cannot delete '{}': parent is a "
                             "skeletal mesh",
                             entity.HasComponent<Galeshi::NameComponent>()
                                 ? entity.GetComponent<Galeshi::NameComponent>().name
                                 : std::to_string(static_cast<uint32_t>(
                                       entity.Handle())));
                continue;
            }
            CollectEntityHierarchy(entity.Handle(), toDestroy);
        }
        if (toDestroy.empty()) {
            m_editor.ClearSelection();
            return;
        }

        PushHistory();

        // Remove non-ECS runtime owners before the components disappear.
        for (entt::entity e : toDestroy) {
            if (!m_scene.Registry().valid(e)) continue;
            m_audio.StopSource(e);

            if (m_physics) {
                if (auto* rb = m_scene.Registry().try_get<Galeshi::RigidBodyComponent>(e)) {
                    m_physics->DestroyBody(rb->bodyId);
                    rb->bodyId = 0xFFFFFFFFu;
                }
                if (auto* soft = m_scene.Registry().try_get<Galeshi::SoftBodyComponent>(e)) {
                    m_physics->DestroyBody(soft->softBodyId);
                    soft->softBodyId = 0xFFFFFFFFu;
                }
            }
            if (m_particles) m_particles->RemoveEmitter(e);
        }

        for (entt::entity e : toDestroy) {
            if (m_scene.Registry().valid(e)) {
                m_scene.DestroyEntity(Galeshi::Entity(e, &m_scene));
            }
        }

        if (m_uiCanvasEntity != entt::null && toDestroy.count(m_uiCanvasEntity)) {
            m_uiCanvasEntity = entt::null;
            m_uiWidgetIndex = -1;
        }

        spdlog::info("[Editor] Deleted {} entities", toDestroy.size());
        m_editor.ClearSelection();
        if (m_debug) m_debug->Begin();
        m_postProcess.ResetTaaHistory();
        ReleaseUnusedSceneAssets();
    }

    // -------------------------------------------------------------------
    // Dockspace
    // -------------------------------------------------------------------
    // -------------------------------------------------------------------
    // UI Designer
    // -------------------------------------------------------------------
    static std::string ToLowerCopy(std::string value) {
        for (char& c : value) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return value;
    }

    static const char* const* UIWidgetTypeNames() {
        static const char* names[] = {
            "None", "Panel", "Text", "Button", "Image", "Slider", "Checkbox",
            "Progress", "Input", "Dropdown", "Scroll List", "Toggle",
            "Layout Group"};
        return names;
    }

    static const char* UIWidgetTypeName(Galeshi::UIWidgetType type) {
        const int index = static_cast<int>(type);
        if (index < 0 || index > 12) return "None";
        return UIWidgetTypeNames()[index];
    }

    static const char* UIWidgetIdPrefix(Galeshi::UIWidgetType type) {
        switch (type) {
        case Galeshi::UIWidgetType::Panel:       return "UIPanel";
        case Galeshi::UIWidgetType::Text:        return "UIText";
        case Galeshi::UIWidgetType::Button:      return "UIButton";
        case Galeshi::UIWidgetType::Image:       return "UIImage";
        case Galeshi::UIWidgetType::Slider:      return "UISlider";
        case Galeshi::UIWidgetType::CheckBox:    return "UICheckbox";
        case Galeshi::UIWidgetType::Progress:    return "UIProgress";
        case Galeshi::UIWidgetType::Input:       return "UIInput";
        case Galeshi::UIWidgetType::Dropdown:    return "UIDropdown";
        case Galeshi::UIWidgetType::ScrollList:  return "UIScrollList";
        case Galeshi::UIWidgetType::Toggle:      return "UIToggle";
        case Galeshi::UIWidgetType::LayoutGroup: return "UILayoutGroup";
        default:                                 return "UIWidget";
        }
    }

    static glm::vec2 DefaultUIWidgetSize(Galeshi::UIWidgetType type) {
        switch (type) {
        case Galeshi::UIWidgetType::Panel:       return {320.0f, 200.0f};
        case Galeshi::UIWidgetType::Text:        return {160.0f, 24.0f};
        case Galeshi::UIWidgetType::Button:      return {160.0f, 40.0f};
        case Galeshi::UIWidgetType::Image:       return {120.0f, 120.0f};
        case Galeshi::UIWidgetType::Slider:      return {200.0f, 24.0f};
        case Galeshi::UIWidgetType::CheckBox:    return {160.0f, 24.0f};
        case Galeshi::UIWidgetType::Progress:    return {220.0f, 18.0f};
        case Galeshi::UIWidgetType::Input:       return {200.0f, 24.0f};
        case Galeshi::UIWidgetType::Dropdown:    return {200.0f, 24.0f};
        case Galeshi::UIWidgetType::ScrollList:  return {220.0f, 140.0f};
        case Galeshi::UIWidgetType::Toggle:      return {160.0f, 24.0f};
        case Galeshi::UIWidgetType::LayoutGroup: return {240.0f, 120.0f};
        default:                                 return {120.0f, 24.0f};
        }
    }

    static const char* DefaultUIWidgetLabel(Galeshi::UIWidgetType type) {
        switch (type) {
        case Galeshi::UIWidgetType::Panel:       return "Panel";
        case Galeshi::UIWidgetType::Text:        return "Text";
        case Galeshi::UIWidgetType::Button:      return "Button";
        case Galeshi::UIWidgetType::Image:       return "Image";
        case Galeshi::UIWidgetType::Slider:      return "Slider";
        case Galeshi::UIWidgetType::CheckBox:    return "Checkbox";
        case Galeshi::UIWidgetType::Progress:    return "Progress";
        case Galeshi::UIWidgetType::Input:       return "Input";
        case Galeshi::UIWidgetType::Dropdown:    return "Dropdown";
        case Galeshi::UIWidgetType::ScrollList:  return "Scroll List";
        case Galeshi::UIWidgetType::Toggle:      return "Toggle";
        case Galeshi::UIWidgetType::LayoutGroup: return "Layout Group";
        default:                                 return "Widget";
        }
    }

    static std::string MakeUIWidgetId(const Galeshi::UICanvas& canvas,
                                      const Galeshi::UIWidget& widget) {
        std::string suffix;
        for (char c : widget.label) {
            if (std::isalnum(static_cast<unsigned char>(c))) suffix.push_back(c);
        }
        if (!suffix.empty()) {
            suffix[0] = static_cast<char>(
                std::toupper(static_cast<unsigned char>(suffix[0])));
        }
        const std::string base = std::string(UIWidgetIdPrefix(widget.type)) +
                                 (suffix.empty() ? std::string{} : "_" + suffix);
        std::string id = base;
        int counter = 1;
        auto exists = [&canvas](const std::string& candidate) {
            for (const auto& other : canvas.widgets) {
                if (other.id == candidate) return true;
            }
            return false;
        };
        while (exists(id)) id = base + std::to_string(counter++);
        return id;
    }

    static void SetUIAnchorPreset(Galeshi::UIWidget& widget,
                                  Galeshi::UIAnchorPreset preset) {
        widget.anchorPreset = preset;
        switch (preset) {
        case Galeshi::UIAnchorPreset::TopLeft:
            widget.anchorMin = {0.0f, 0.0f}; widget.anchorMax = {0.0f, 0.0f}; widget.pivot = {0.0f, 0.0f}; break;
        case Galeshi::UIAnchorPreset::TopCenter:
            widget.anchorMin = {0.5f, 0.0f}; widget.anchorMax = {0.5f, 0.0f}; widget.pivot = {0.5f, 0.0f}; break;
        case Galeshi::UIAnchorPreset::TopRight:
            widget.anchorMin = {1.0f, 0.0f}; widget.anchorMax = {1.0f, 0.0f}; widget.pivot = {1.0f, 0.0f}; break;
        case Galeshi::UIAnchorPreset::MiddleLeft:
            widget.anchorMin = {0.0f, 0.5f}; widget.anchorMax = {0.0f, 0.5f}; widget.pivot = {0.0f, 0.5f}; break;
        case Galeshi::UIAnchorPreset::MiddleCenter:
            widget.anchorMin = {0.5f, 0.5f}; widget.anchorMax = {0.5f, 0.5f}; widget.pivot = {0.5f, 0.5f}; break;
        case Galeshi::UIAnchorPreset::MiddleRight:
            widget.anchorMin = {1.0f, 0.5f}; widget.anchorMax = {1.0f, 0.5f}; widget.pivot = {1.0f, 0.5f}; break;
        case Galeshi::UIAnchorPreset::BottomLeft:
            widget.anchorMin = {0.0f, 1.0f}; widget.anchorMax = {0.0f, 1.0f}; widget.pivot = {0.0f, 1.0f}; break;
        case Galeshi::UIAnchorPreset::BottomCenter:
            widget.anchorMin = {0.5f, 1.0f}; widget.anchorMax = {0.5f, 1.0f}; widget.pivot = {0.5f, 1.0f}; break;
        case Galeshi::UIAnchorPreset::BottomRight:
            widget.anchorMin = {1.0f, 1.0f}; widget.anchorMax = {1.0f, 1.0f}; widget.pivot = {1.0f, 1.0f}; break;
        case Galeshi::UIAnchorPreset::StretchHorizontal:
            widget.anchorMin = {0.0f, 0.5f}; widget.anchorMax = {1.0f, 0.5f}; widget.pivot = {0.5f, 0.5f}; break;
        case Galeshi::UIAnchorPreset::StretchVertical:
            widget.anchorMin = {0.5f, 0.0f}; widget.anchorMax = {0.5f, 1.0f}; widget.pivot = {0.5f, 0.5f}; break;
        case Galeshi::UIAnchorPreset::StretchAll:
            widget.anchorMin = {0.0f, 0.0f}; widget.anchorMax = {1.0f, 1.0f}; widget.pivot = {0.5f, 0.5f}; break;
        }
    }

    static int UIWidgetDepth(const Galeshi::UICanvas& canvas, size_t index) {
        int depth = 0;
        int parent = canvas.widgets[index].parent;
        int guard = 0;
        while (parent >= 0 &&
               parent < static_cast<int>(canvas.widgets.size()) &&
               guard++ < 32) {
            ++depth;
            parent = canvas.widgets[static_cast<size_t>(parent)].parent;
        }
        return depth;
    }

    static bool UIEventInput(const char* label, std::string& value) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = buffer;
            return true;
        }
        return false;
    }

    void DispatchUIEvent(const Galeshi::UIEvent& event) {
        auto& registry = m_scene.Registry();
        const entt::entity entity = static_cast<entt::entity>(event.canvasEntity);
        if (entity == entt::null || !registry.valid(entity) ||
            !registry.all_of<Galeshi::UICanvasComponent>(entity)) {
            return;
        }
        m_scripts.PushUIEvent(event);
        auto& canvas = registry.get<Galeshi::UICanvasComponent>(entity).canvas;

        Galeshi::UIWidget* widget = nullptr;
        if (!event.widgetId.empty()) {
            for (auto& candidate : canvas.widgets) {
                if (candidate.id == event.widgetId) {
                    widget = &candidate;
                    break;
                }
            }
        }

        std::string handler;
        switch (event.type) {
        case Galeshi::UIEvent::Type::Shown:
            handler = widget ? widget->onShow : canvas.onMenuOpen;
            break;
        case Galeshi::UIEvent::Type::Hover:
            handler = widget ? widget->onHover : canvas.onMenuHover;
            break;
        case Galeshi::UIEvent::Type::Clicked:
        case Galeshi::UIEvent::Type::Toggled:
        case Galeshi::UIEvent::Type::ValueChanged:
            handler = widget ? widget->onClick : canvas.onMenuClick;
            break;
        }
        if (handler.empty()) return;
        m_scripts.CallUIHandler(handler, event.widgetId, event.value,
                                event.checked);
    }

    entt::entity ActiveUICanvasEntity() {
        auto& registry = m_scene.Registry();
        if (m_uiCanvasEntity != entt::null && registry.valid(m_uiCanvasEntity) &&
            registry.all_of<Galeshi::UICanvasComponent>(m_uiCanvasEntity)) {
            return m_uiCanvasEntity;
        }
        for (auto entity : m_editor.SelectedAll(&m_scene)) {
            if (entity.HasComponent<Galeshi::UICanvasComponent>()) {
                m_uiCanvasEntity = entity.Handle();
                return m_uiCanvasEntity;
            }
        }
        auto view = registry.view<Galeshi::UICanvasComponent>();
        if (!view.empty()) {
            m_uiCanvasEntity = *view.begin();
            return m_uiCanvasEntity;
        }
        m_uiCanvasEntity = entt::null;
        return entt::null;
    }

    Galeshi::UICanvas* ActiveUICanvas() {
        const entt::entity entity = ActiveUICanvasEntity();
        if (entity == entt::null) return nullptr;
        return &m_scene.Registry().get<Galeshi::UICanvasComponent>(entity).canvas;
    }

    Galeshi::UIWidget& AddUIWidget(Galeshi::UICanvas& canvas,
                                   Galeshi::UIWidgetType type,
                                   const std::string& label,
                                   glm::vec2 position,
                                   glm::vec2 size) {
        Galeshi::UIWidget widget;
        widget.type = type;
        widget.label = label;
        widget.position = position;
        widget.size = size;
        widget.zOrder = static_cast<int>(canvas.widgets.size());
        if (size.x <= 0.0f && size.y <= 0.0f) {
            widget.size = DefaultUIWidgetSize(type);
        }
        switch (type) {
        case Galeshi::UIWidgetType::Panel:
            widget.useColor = true;
            widget.color = {26.0f / 255.0f, 41.0f / 255.0f, 63.0f / 255.0f, 245.0f / 255.0f};
            widget.radius = 12.0f;
            widget.border = 2.0f;
            break;
        case Galeshi::UIWidgetType::Button:
            widget.buttonHeight = widget.size.y;
            widget.useColor = true;
            widget.color = {37.0f / 255.0f, 99.0f / 255.0f, 170.0f / 255.0f, 1.0f};
            widget.radius = 6.0f;
            break;
        case Galeshi::UIWidgetType::Progress:
            widget.buttonHeight = widget.size.y;
            widget.sliderValue = 0.72f;
            widget.useColor = true;
            widget.color = {0.20f, 0.50f, 0.90f, 1.0f};
            break;
        default:
            break;
        }
        canvas.widgets.push_back(std::move(widget));
        Galeshi::UIWidget& added = canvas.widgets.back();
        added.id = MakeUIWidgetId(canvas, added);
        return added;
    }

    void AddDefaultUIWidget(Galeshi::UIWidgetType type) {
        if (!ActiveUICanvas()) CreateUICanvas("UICanvas");
        Galeshi::UICanvas* canvas = ActiveUICanvas();
        if (!canvas) return;
        const entt::entity entity = ActiveUICanvasEntity();

        const glm::vec2 size = DefaultUIWidgetSize(type);
        const int index = static_cast<int>(canvas->widgets.size());
        const glm::vec2 reference(
            std::max(1.0f, static_cast<float>(canvas->referenceResolution.x)),
            std::max(1.0f, static_cast<float>(canvas->referenceResolution.y)));
        glm::vec2 position((reference.x - size.x) * 0.5f,
                           (reference.y - size.y) * 0.5f +
                               static_cast<float>(index % 8) * 12.0f);
        AddUIWidget(*canvas, type, DefaultUIWidgetLabel(type), position, size);
        m_uiCanvasEntity = entity;
        m_uiWidgetIndex = index;
        m_editor.Select(Galeshi::Entity(entity, &m_scene));
        PushHistory();
    }

    void ApplyUITemplate(Galeshi::UICanvas& canvas, int preset) {
        canvas.onMenuOpen = "on_menu_open";
        canvas.onMenuHover = "on_menu_hover";
        canvas.onMenuClick = "on_menu_click";

        auto add = [this, &canvas](Galeshi::UIWidgetType type, const char* label,
                             glm::vec2 position, glm::vec2 size) -> int {
            const int index = static_cast<int>(canvas.widgets.size());
            AddUIWidget(canvas, type, label, position, size);
            return index;
        };

        // All templates author widgets in the 1920x1080 reference space. The
        // runtime and editor viewport scale the complete canvas to the
        // destination rectangle.
        canvas.referenceResolution = {1920, 1080};
        canvas.screenSize = {1920.0f, 1080.0f};
        canvas.screenPosition = {0.0f, 0.0f};

        if (preset == 0) { // Main Menu
            canvas.canvasId = "MainMenu";
            canvas.title = "Main Menu";
            canvas.backgroundAlpha = 0.0f;
            add(Galeshi::UIWidgetType::Panel, "UIPanel", {720.0f, 160.0f}, {480.0f, 760.0f});
            add(Galeshi::UIWidgetType::Image, "Logo", {912.0f, 210.0f}, {96.0f, 96.0f});
            const char* names[] = {"New Game", "Continue", "Settings", "Credits", "Quit"};
            float y = 360.0f;
            for (const char* name : names) {
                const int index = add(Galeshi::UIWidgetType::Button, name,
                                      {760.0f, y}, {400.0f, 56.0f});
                if (index >= 0 && index < static_cast<int>(canvas.widgets.size())) {
                    if (std::string(name) == "New Game") {
                        canvas.widgets[static_cast<size_t>(index)].onClick = "on_play_clicked";
                    }
                }
                y += 72.0f;
            }
        } else if (preset == 1) { // HUD
            canvas.canvasId = "HUD_Root";
            canvas.title = "HUD";
            canvas.backgroundAlpha = 0.0f;
            canvas.closeable = false;
            add(Galeshi::UIWidgetType::Progress, "HP 72 / 100", {660.0f, 40.0f}, {600.0f, 32.0f});
            add(Galeshi::UIWidgetType::Image, "Minimap", {1560.0f, 780.0f}, {220.0f, 220.0f});
        } else if (preset == 2) { // Pause
            canvas.canvasId = "PauseMenu";
            canvas.title = "Pause Menu";
            canvas.backgroundAlpha = 0.0f;
            add(Galeshi::UIWidgetType::Panel, "PausePanel", {720.0f, 240.0f}, {480.0f, 600.0f});
            add(Galeshi::UIWidgetType::Text, "PAUSED", {760.0f, 290.0f}, {400.0f, 40.0f});
            const char* names[] = {"Resume", "Settings", "Quit"};
            float y = 380.0f;
            for (const char* name : names) {
                add(Galeshi::UIWidgetType::Button, name, {760.0f, y}, {400.0f, 56.0f});
                y += 72.0f;
            }
        } else if (preset == 3) { // Loading modal
            canvas.canvasId = "LoadingModal";
            canvas.title = "Loading";
            canvas.backgroundAlpha = 0.0f;
            add(Galeshi::UIWidgetType::Panel, "LoadingPanel", {720.0f, 440.0f}, {480.0f, 200.0f});
            add(Galeshi::UIWidgetType::Text, "Loading...", {760.0f, 475.0f}, {400.0f, 30.0f});
            add(Galeshi::UIWidgetType::Progress, "45%", {760.0f, 535.0f}, {400.0f, 24.0f});
        } else if (preset == 4) { // Settings
            canvas.canvasId = "Settings";
            canvas.title = "Settings";
            canvas.backgroundAlpha = 0.0f;
            add(Galeshi::UIWidgetType::Panel, "SettingsPanel", {600.0f, 240.0f}, {720.0f, 600.0f});
            add(Galeshi::UIWidgetType::Text, "SETTINGS", {640.0f, 285.0f}, {300.0f, 36.0f});
            add(Galeshi::UIWidgetType::Slider, "Volume", {640.0f, 370.0f}, {640.0f, 28.0f});
            add(Galeshi::UIWidgetType::CheckBox, "Fullscreen", {640.0f, 430.0f}, {240.0f, 28.0f});
            add(Galeshi::UIWidgetType::Button, "Back", {640.0f, 752.0f}, {640.0f, 52.0f});
        } else if (preset == 5) { // Inventory
            canvas.canvasId = "Inventory";
            canvas.title = "Inventory";
            canvas.backgroundAlpha = 0.0f;
            add(Galeshi::UIWidgetType::Panel, "InventoryPanel", {510.0f, 230.0f}, {900.0f, 620.0f});
            add(Galeshi::UIWidgetType::Text, "INVENTORY", {550.0f, 272.0f}, {300.0f, 36.0f});
            add(Galeshi::UIWidgetType::ScrollList, "Items", {550.0f, 330.0f}, {520.0f, 440.0f});
            add(Galeshi::UIWidgetType::Button, "Close", {1090.0f, 330.0f}, {180.0f, 48.0f});
        } else if (preset == 6) { // Dialogue
            canvas.canvasId = "Dialogue";
            canvas.title = "Dialogue";
            canvas.backgroundAlpha = 0.0f;
            add(Galeshi::UIWidgetType::Panel, "DialoguePanel", {360.0f, 760.0f}, {1200.0f, 260.0f});
            add(Galeshi::UIWidgetType::Text, "NPC: Welcome to Galeshi!", {400.0f, 800.0f}, {1120.0f, 80.0f});
            add(Galeshi::UIWidgetType::Button, "Continue", {1360.0f, 940.0f}, {160.0f, 44.0f});
        }
    }

    void CreateUICanvas(const char* name, int preset = -1) {
        auto entity = m_scene.CreateEntity(name);
        auto& canvas = entity.AddComponent<Galeshi::UICanvasComponent>().canvas;
        canvas.canvasId = name;
        canvas.title = name;
        canvas.screenPosition = {0.0f, 0.0f};
        canvas.screenSize = {1920.0f, 1080.0f};
        canvas.referenceResolution = {1920, 1080};
        canvas.backgroundAlpha = 0.0f;
        canvas.resizable = false;
        canvas.closeable = false;
        canvas.movable = false;
        if (preset >= 0) ApplyUITemplate(canvas, preset);
        m_editor.Select(entity);
        m_uiCanvasEntity = entity.Handle();
        m_uiWidgetIndex = -1;
        spdlog::info("[UI Designer] Created canvas '{}'", name);
        // Store the completed operation so Undo can remove the new canvas.
        PushHistory();
    }

    void DeleteUICanvas(entt::entity entity) {
        auto& registry = m_scene.Registry();
        if (entity == entt::null || !registry.valid(entity) ||
            !registry.all_of<Galeshi::UICanvasComponent>(entity)) {
            return;
        }
        PushHistory();
        m_uiPreview = false;
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        m_editor.ClearSelection();
        m_scene.DestroyEntity(Galeshi::Entity(entity, &m_scene));
        spdlog::info("[UI Designer] Deleted canvas entity {}",
                     static_cast<uint32_t>(entity));
    }

    void ReorderSelectedUIWidget(int mode) {
        Galeshi::UICanvas* canvas = ActiveUICanvas();
        if (!canvas) return;
        const int count = static_cast<int>(canvas->widgets.size());
        if (m_uiWidgetIndex < 0 || m_uiWidgetIndex >= count) return;

        auto& widgets = canvas->widgets;
        Galeshi::UIWidget& widget = widgets[static_cast<size_t>(m_uiWidgetIndex)];

        if (mode == 0) { // To Front
            int maxZ = 0;
            for (const auto& other : widgets) maxZ = std::max(maxZ, other.zOrder);
            widget.zOrder = maxZ + 1;
        } else if (mode == 3) { // To Back
            int minZ = 0;
            for (const auto& other : widgets) minZ = std::min(minZ, other.zOrder);
            widget.zOrder = minZ - 1;
        } else if (mode == 1) { // Forward
            int best = -1;
            for (int i = 0; i < count; ++i) {
                if (widgets[static_cast<size_t>(i)].zOrder > widget.zOrder &&
                    (best < 0 ||
                     widgets[static_cast<size_t>(i)].zOrder <
                         widgets[static_cast<size_t>(best)].zOrder)) {
                    best = i;
                }
            }
            if (best >= 0) {
                std::swap(widget.zOrder, widgets[static_cast<size_t>(best)].zOrder);
            }
        } else { // Backward
            int best = -1;
            for (int i = 0; i < count; ++i) {
                if (widgets[static_cast<size_t>(i)].zOrder < widget.zOrder &&
                    (best < 0 ||
                     widgets[static_cast<size_t>(i)].zOrder >
                         widgets[static_cast<size_t>(best)].zOrder)) {
                    best = i;
                }
            }
            if (best >= 0) {
                std::swap(widget.zOrder, widgets[static_cast<size_t>(best)].zOrder);
            }
        }
        PushHistory();
    }

    void DrawUILayers() {
        Galeshi::UICanvas* active = ActiveUICanvas();

        ImGui::TextDisabled("LAYERS (Z-ORDER)");
        const bool canReorder = active && m_uiWidgetIndex >= 0 &&
                                m_uiWidgetIndex < static_cast<int>(active->widgets.size());
        if (!canReorder) ImGui::BeginDisabled();
        if (ImGui::Button("To Front"))   ReorderSelectedUIWidget(0);
        ImGui::SameLine();
        if (ImGui::Button("Forward"))    ReorderSelectedUIWidget(1);
        ImGui::SameLine();
        if (ImGui::Button("Backward"))   ReorderSelectedUIWidget(2);
        ImGui::SameLine();
        if (ImGui::Button("To Back"))    ReorderSelectedUIWidget(3);
        if (!canReorder) ImGui::EndDisabled();

        ImGui::SameLine();
        ImGui::TextDisabled("drag to reorder");

        auto& registry = m_scene.Registry();
        const float tableHeight = ImGui::GetContentRegionAvail().y;
        if (!ImGui::BeginTable("##UILayersTable", 5,
                               ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
                                   ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp,
                               ImVec2(0.0f, tableHeight))) {
            return;
        }
        ImGui::TableSetupColumn("vis", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("lock", ImGuiTableColumnFlags_WidthFixed, 24.0f);
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Type", ImGuiTableColumnFlags_WidthFixed, 78.0f);
        ImGui::TableSetupColumn("Z", ImGuiTableColumnFlags_WidthFixed, 30.0f);
        ImGui::TableHeadersRow();

        auto view = registry.view<Galeshi::UICanvasComponent>();
        for (auto entity : view) {
            auto& canvas = view.get<Galeshi::UICanvasComponent>(entity).canvas;
            ImGui::PushID(static_cast<int>(entity));

            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Checkbox("##canvas_vis", &canvas.visible);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextDisabled("--");
            ImGui::TableSetColumnIndex(2);
            const bool canvasSelected = (m_uiCanvasEntity == entity &&
                                         m_uiWidgetIndex < 0);
            if (ImGui::Selectable(canvas.canvasId.c_str(), canvasSelected,
                                  ImGuiSelectableFlags_SpanAllColumns)) {
                m_uiCanvasEntity = entity;
                m_uiWidgetIndex = -1;
                m_editor.Select(Galeshi::Entity(entity, &m_scene));
            }
            ImGui::TableSetColumnIndex(3);
            ImGui::TextDisabled("Canvas");
            ImGui::TableSetColumnIndex(4);
            ImGui::TextDisabled("%d", canvas.sortOrder);

            std::vector<size_t> order(canvas.widgets.size());
            std::iota(order.begin(), order.end(), size_t{0});
            std::stable_sort(order.begin(), order.end(),
                             [&](size_t a, size_t b) {
                                 return canvas.widgets[a].zOrder <
                                        canvas.widgets[b].zOrder;
                             });

            for (size_t index : order) {
                auto& widget = canvas.widgets[index];
                ImGui::PushID(static_cast<int>(index));
                ImGui::TableNextRow();

                ImGui::TableSetColumnIndex(0);
                ImGui::Checkbox("##vis", &widget.visible);

                ImGui::TableSetColumnIndex(1);
                ImGui::Checkbox("##lock", &widget.locked);

                ImGui::TableSetColumnIndex(2);
                const int depth = UIWidgetDepth(canvas, index);
                std::string name(static_cast<size_t>(depth) * 2, ' ');
                name += widget.id;
                const bool selected = (m_uiCanvasEntity == entity &&
                                       m_uiWidgetIndex == static_cast<int>(index));
                if (ImGui::Selectable(name.c_str(), selected,
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    m_uiCanvasEntity = entity;
                    m_uiWidgetIndex = static_cast<int>(index);
                    m_editor.Select(Galeshi::Entity(entity, &m_scene));
                }

                ImGui::TableSetColumnIndex(3);
                ImGui::TextDisabled("%s", UIWidgetTypeName(widget.type));

                ImGui::TableSetColumnIndex(4);
                ImGui::TextDisabled("%d", widget.zOrder);
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    void DrawUIDesigner() {
        if (!m_uiShowDesigner) return;
        if (!ImGui::Begin("UI Designer", &m_uiShowDesigner)) {
            ImGui::End();
            return;
        }

        // --- Toolbar ---
        const char* tools[] = {"Select", "Move", "Rect"};
        for (int i = 0; i < 3; ++i) {
            if (i > 0) ImGui::SameLine();
            // Remember the state used for PushStyleColor: clicking the
            // button changes m_uiTool, so checking it after Button() would
            // pop a color that was never pushed on the newly active tool.
            const bool toolActive = (m_uiTool == i);
            if (toolActive) {
                ImGui::PushStyleColor(ImGuiCol_Button,
                                      ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
            }
            if (ImGui::Button(tools[i], ImVec2(64.0f, 0.0f))) m_uiTool = i;
            if (toolActive) ImGui::PopStyleColor();
        }

        auto& registry = m_scene.Registry();
        const entt::entity activeEntity = ActiveUICanvasEntity();
        Galeshi::UICanvas* active = activeEntity != entt::null
                                        ? &registry.get<Galeshi::UICanvasComponent>(activeEntity).canvas
                                        : nullptr;

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Canvas:");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(150.0f);
        const std::string canvasPreview = active ? active->canvasId : "None";
        if (ImGui::BeginCombo("##canvasSelect", canvasPreview.c_str())) {
            auto view = registry.view<Galeshi::UICanvasComponent>();
            for (auto entity : view) {
                auto& canvas = view.get<Galeshi::UICanvasComponent>(entity).canvas;
                const bool selected = (entity == activeEntity);
                if (ImGui::Selectable(canvas.canvasId.c_str(), selected)) {
                    m_uiCanvasEntity = entity;
                    m_uiWidgetIndex = -1;
                    m_editor.Select(Galeshi::Entity(entity, &m_scene));
                }
                if (selected) ImGui::SetItemDefaultFocus();
            }
            ImGui::Separator();
            if (ImGui::Selectable("+ New Canvas")) CreateUICanvas("UICanvas");
            ImGui::EndCombo();
        }
        if (activeEntity != entt::null) {
            ImGui::SameLine();
            if (ImGui::Button("Delete Canvas")) {
                DeleteUICanvas(activeEntity);
                return;
            }
        }

        // Re-fetch after the combo: "+ New Canvas" may have added a component
        // and invalidated the pointer obtained above.
        Galeshi::UICanvas* activeNow = ActiveUICanvas();
        if (activeNow) {
            ImGui::SameLine();
            const char* resolutions[] = {"1280 x 720", "1920 x 1080",
                                         "2560 x 1440", "3840 x 2160", "Custom"};
            int refIndex = 4;
            const glm::ivec2 ref = activeNow->referenceResolution;
            if (ref == glm::ivec2(1280, 720)) refIndex = 0;
            else if (ref == glm::ivec2(1920, 1080)) refIndex = 1;
            else if (ref == glm::ivec2(2560, 1440)) refIndex = 2;
            else if (ref == glm::ivec2(3840, 2160)) refIndex = 3;
            ImGui::SetNextItemWidth(110.0f);
            if (ImGui::Combo("##resolution", &refIndex, resolutions, 5)) {
                if (refIndex == 0) activeNow->referenceResolution = {1280, 720};
                else if (refIndex == 1) activeNow->referenceResolution = {1920, 1080};
                else if (refIndex == 2) activeNow->referenceResolution = {2560, 1440};
                else if (refIndex == 3) activeNow->referenceResolution = {3840, 2160};
            }
        }

        ImGui::SameLine();
        ImGui::TextDisabled("|");
        ImGui::SameLine();
        ImGui::Text("Zoom");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::SliderInt("##zoom", &m_uiZoomPercent, 25, 200, "%d%%");

        ImGui::SameLine();
        ImGui::Checkbox("Snap", &m_uiSnap);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(70.0f);
        ImGui::DragFloat("##snapPx", &m_uiSnapPx, 1.0f, 1.0f, 64.0f, "%.0f px");
        ImGui::SameLine();
        ImGui::Checkbox("Safe Area", &m_uiSafeArea);
        ImGui::SameLine();
        ImGui::Checkbox("Z tags", &m_uiZTags);
        ImGui::SameLine();
        if (ImGui::Button(m_uiPreview ? "Hide Preview" : "Preview UI")) {
            m_uiPreview = !m_uiPreview;
        }
        ImGui::SameLine();
        const bool overlayActive = m_uiCanvasOverlay;
        if (overlayActive) {
            ImGui::PushStyleColor(ImGuiCol_Button,
                                  ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
        }
        if (ImGui::Button("2D Canvas")) m_uiCanvasOverlay = !m_uiCanvasOverlay;
        if (overlayActive) ImGui::PopStyleColor();

        ImGui::Separator();

        // --- Widget palette ---
        ImGui::BeginChild("##UIPalette", ImVec2(320.0f, 0.0f),
                          ImGuiChildFlags_Borders);
        ImGui::TextDisabled("WIDGET PALETTE");
        ImGui::SetNextItemWidth(-1.0f);
        ImGui::InputTextWithHint("##widgetSearch", "Search widgets...",
                                 m_uiSearch, sizeof(m_uiSearch));

        const std::string query = ToLowerCopy(m_uiSearch);
        const float available = ImGui::GetContentRegionAvail().x;
        const float buttonWidth =
            std::max(50.0f, (available - ImGui::GetStyle().ItemSpacing.x * 3.0f) / 4.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(2.0f, 4.0f));
        int column = 0;
        for (int type = 1; type <= 12; ++type) {
            const char* name = UIWidgetTypeNames()[type];
            if (!query.empty() &&
                ToLowerCopy(name).find(query) == std::string::npos) {
                continue;
            }
            const char* displayName = name;
            if (type == static_cast<int>(Galeshi::UIWidgetType::ScrollList)) {
                displayName = "Scroll";
            } else if (type == static_cast<int>(Galeshi::UIWidgetType::LayoutGroup)) {
                displayName = "Layout";
            }
            if (column > 0) ImGui::SameLine();
            if (ImGui::Button(displayName, ImVec2(buttonWidth, 44.0f))) {
                AddDefaultUIWidget(static_cast<Galeshi::UIWidgetType>(type));
            }
            if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", name);
            column = (column + 1) % 4;
            if (column == 0) ImGui::NewLine();
        }
        ImGui::PopStyleVar();
        if (column != 0) ImGui::NewLine();

        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextDisabled("TEMPLATES");
        struct UITemplateButton {
            const char* label;
            int preset;
        };
        // Keep the button order independent from ApplyUITemplate's preset
        // indices (the Loading template is exposed from Create > UI instead).
        const UITemplateButton templates[] = {
            {"Main Menu", 0}, {"HUD", 1},    {"Pause", 2},
            {"Settings", 4},  {"Inventory", 5}, {"Dialogue", 6}};
        const float templateWidth =
            std::max(60.0f, (ImGui::GetContentRegionAvail().x -
                             ImGui::GetStyle().ItemSpacing.x) / 2.0f);
        for (int i = 0; i < 6; ++i) {
            if (i % 2 == 1) ImGui::SameLine();
            if (ImGui::Button(templates[i].label, ImVec2(templateWidth, 0.0f))) {
                CreateUICanvas(templates[i].label, templates[i].preset);
            }
        }
        ImGui::EndChild();

        ImGui::SameLine();
        ImGui::BeginChild("##UILayersPanel", ImVec2(0.0f, 0.0f));
        DrawUILayers();
        ImGui::EndChild();

        ImGui::End();

        // --- Live preview (rendered on top of the editor) ---
        if (m_uiPreview) {
            const entt::entity previewEntity = ActiveUICanvasEntity();
            if (previewEntity != entt::null &&
                registry.valid(previewEntity) &&
                registry.all_of<Galeshi::UICanvasComponent>(previewEntity)) {
                auto& source =
                    registry.get<Galeshi::UICanvasComponent>(previewEntity).canvas;
                m_uiPreviewCanvas = source;
                m_uiPreviewCanvas.title.clear();
                m_uiPreviewCanvas.movable = false;
                m_uiPreviewCanvas.resizable = false;
                m_uiPreviewCanvas.closeable = false;
                ImGuiViewport* viewport = ImGui::GetMainViewport();
                const glm::vec2 reference(
                    std::max(1.0f, static_cast<float>(source.referenceResolution.x)),
                    std::max(1.0f, static_cast<float>(source.referenceResolution.y)));
                const float scale = std::min(
                    (viewport->Size.x * 0.72f) / reference.x,
                    (viewport->Size.y * 0.80f) / reference.y);
                const ImVec2 previewSize(reference.x * scale, reference.y * scale);
                const ImVec2 previewPos(
                    viewport->Pos.x + (viewport->Size.x - previewSize.x) * 0.5f,
                    viewport->Pos.y + (viewport->Size.y - previewSize.y) * 0.5f);
                m_uiPreviewRuntime.RenderCanvasInRect(
                    m_uiPreviewCanvas, static_cast<int>(previewEntity),
                    previewPos.x, previewPos.y, previewSize.x, previewSize.y,
                    "uid_preview");
            }
        }
    }

    void DrawUICanvasProperties(Galeshi::UICanvas& canvas) {
        if (!ImGui::CollapsingHeader("UI Canvas (parent)",
                                     ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        UIEventInput("Name", canvas.canvasId);
        UIEventInput("Title", canvas.title);

        const char* renderModes[] = {"Screen Space - Overlay",
                                     "Screen Space - Camera", "World Space"};
        int renderMode = static_cast<int>(canvas.renderMode);
        if (ImGui::Combo("Render Mode", &renderMode, renderModes, 3)) {
            canvas.renderMode = static_cast<Galeshi::UICanvasRenderMode>(
                std::clamp(renderMode, 0, 2));
        }

        const char* scaleModes[] = {"Scale With Screen Size",
                                    "Constant Pixel Size"};
        int scaleMode = static_cast<int>(canvas.scaleMode);
        if (ImGui::Combo("Scale Mode", &scaleMode, scaleModes, 2)) {
            canvas.scaleMode = static_cast<Galeshi::UICanvasScaleMode>(
                std::clamp(scaleMode, 0, 1));
        }

        ImGui::DragInt2("Ref Res", glm::value_ptr(canvas.referenceResolution),
                        1.0f, 16, 16384);
        ImGui::Checkbox("Pixel Perfect", &canvas.pixelPerfect);
        ImGui::DragInt("Sort Order", &canvas.sortOrder);
        ImGui::SliderFloat("Background Alpha", &canvas.backgroundAlpha, 0.0f, 1.0f);
        ImGui::Checkbox("Visible##canvas", &canvas.visible);

        ImGui::Separator();
        ImGui::TextDisabled("UI EVENTS (LUA)");
        UIEventInput("On Menu Open", canvas.onMenuOpen);
        UIEventInput("On Menu Hover", canvas.onMenuHover);
        UIEventInput("On Menu Click", canvas.onMenuClick);

        if (ImGui::Button("+ Add Widget")) AddDefaultUIWidget(Galeshi::UIWidgetType::Button);
        ImGui::SameLine();
        if (ImGui::Button("Open Script")) ImGui::SetWindowFocus("Code Editor");

        ImGui::Separator();
        if (ImGui::Button("Delete Canvas")) {
            DeleteUICanvas(ActiveUICanvasEntity());
            return;
        }
    }

    void DrawUIWidgetProperties(Galeshi::UICanvas& canvas,
                                Galeshi::UIWidget& widget) {
        if (!ImGui::CollapsingHeader("UI Widget", ImGuiTreeNodeFlags_DefaultOpen)) {
            return;
        }

        int type = static_cast<int>(widget.type);
        if (ImGui::Combo("Widget Type", &type, UIWidgetTypeNames(), 13)) {
            widget.type = static_cast<Galeshi::UIWidgetType>(
                std::clamp(type, 0, 12));
        }
        UIEventInput("Name", widget.label);
        UIEventInput("ID", widget.id);

        ImGui::TextDisabled("Anchor Preset");
        const char* anchorLabels[3][3] = {
            {"TL", "TC", "TR"}, {"ML", "MC", "MR"}, {"BL", "BC", "BR"}};
        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                if (column > 0) ImGui::SameLine();
                ImGui::PushID(row * 3 + column);
                const int preset = row * 3 + column;
                if (ImGui::Button(anchorLabels[row][column], ImVec2(38.0f, 0.0f))) {
                    SetUIAnchorPreset(widget,
                                      static_cast<Galeshi::UIAnchorPreset>(preset));
                }
                ImGui::PopID();
            }
        }
        if (ImGui::Button("Stretch H")) {
            SetUIAnchorPreset(widget, Galeshi::UIAnchorPreset::StretchHorizontal);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stretch V")) {
            SetUIAnchorPreset(widget, Galeshi::UIAnchorPreset::StretchVertical);
        }
        ImGui::SameLine();
        if (ImGui::Button("Stretch All")) {
            SetUIAnchorPreset(widget, Galeshi::UIAnchorPreset::StretchAll);
        }

        ImGui::DragFloat2("Anchor Min", glm::value_ptr(widget.anchorMin), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Anchor Max", glm::value_ptr(widget.anchorMax), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Pivot", glm::value_ptr(widget.pivot), 0.01f, 0.0f, 1.0f);
        ImGui::DragFloat2("Position (px)", glm::value_ptr(widget.position), 1.0f);
        ImGui::DragFloat2("Size (px)", glm::value_ptr(widget.size), 1.0f, 0.0f, 10000.0f);

        const bool textWidget =
            widget.type == Galeshi::UIWidgetType::Text;
        ImGui::Checkbox(
            textWidget ? "Use Legacy Text Color"
                       : "Override Background Color",
            &widget.useColor);
        if (!widget.useColor) ImGui::BeginDisabled();
        ImGui::ColorEdit4(
            textWidget ? "Legacy Text Color" : "Background Color",
            glm::value_ptr(widget.color),
            ImGuiColorEditFlags_Uint8 | ImGuiColorEditFlags_AlphaBar);
        if (!widget.useColor) ImGui::EndDisabled();
        ImGui::Checkbox("Override Text Color", &widget.useTextColor);
        if (!widget.useTextColor) ImGui::BeginDisabled();
        ImGui::ColorEdit4("Text Color", glm::value_ptr(widget.textColor),
                          ImGuiColorEditFlags_Uint8 |
                              ImGuiColorEditFlags_AlphaBar);
        if (!widget.useTextColor) ImGui::EndDisabled();
        ImGui::SliderFloat("Opacity", &widget.opacity, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Radius", &widget.radius, 0.5f, 0.0f, 64.0f);
        ImGui::DragFloat("Border", &widget.border, 0.1f, 0.0f, 16.0f);
        ImGui::Checkbox("Override Border Color", &widget.useBorderColor);
        if (!widget.useBorderColor) ImGui::BeginDisabled();
        ImGui::ColorEdit4("Border Color", glm::value_ptr(widget.borderColor),
                          ImGuiColorEditFlags_Uint8 |
                              ImGuiColorEditFlags_AlphaBar);
        if (!widget.useBorderColor) ImGui::EndDisabled();

        ImGui::Checkbox("Visible##widget", &widget.visible);
        ImGui::SameLine();
        ImGui::Checkbox("Interactable", &widget.interactable);
        ImGui::SameLine();
        ImGui::Checkbox("Block Input", &widget.blockInput);
        UIEventInput("Tooltip", widget.tooltip);

        const auto drawAssetPathField =
            [this](const char* label, std::string& value, bool font) {
                UIEventInput(label, value);
                if (!ImGui::BeginDragDropTarget()) return;
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path =
                        static_cast<const char*>(payload->Data);
                    const bool accepted = path &&
                        (font ? IsFontAssetPath(path)
                              : IsImageAssetPath(path));
                    if (accepted) {
                        PushHistory();
                        value = m_assets ? m_assets->MakeRelativePath(path)
                                         : std::string(path);
                    }
                }
                ImGui::EndDragDropTarget();
            };

        const auto drawImageDropField =
            [this](const char* label, std::string& value) {
                ImGui::PushID(label);
                ImGui::TextUnformatted(label);
                const std::string caption = value.empty()
                    ? "Drop image here"
                    : std::filesystem::path(value).filename().string();
                const float width = ImGui::GetContentRegionAvail().x;
                ImGui::Button(caption.c_str(), ImVec2(width, 32.0f));
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "%s\nDrag a PNG/JPG/TGA/DDS image from the Content Browser.",
                        value.empty() ? "No image assigned."
                                      : value.c_str());
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(
                                "GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        if (path && IsImageAssetPath(path)) {
                            PushHistory();
                            value = m_assets
                                ? m_assets->MakeRelativePath(path)
                                : std::string(path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                if (!value.empty()) {
                    if (ImGui::SmallButton("Clear Image Path")) {
                        PushHistory();
                        value.clear();
                    }
                }
                ImGui::PopID();
            };

        switch (widget.type) {
        case Galeshi::UIWidgetType::Button:
            drawImageDropField("Image Path", widget.imagePath);
            ImGui::DragFloat("Height", &widget.buttonHeight, 0.5f, 0.0f, 400.0f);
            break;
        case Galeshi::UIWidgetType::Text: {
            drawAssetPathField("Font Path", widget.fontPath, true);
            ImGui::DragFloat("Font Size", &widget.fontSize, 0.5f, 0.0f,
                             160.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 uses the widget height.");
            }
            const char* alignLabels[] = {"Left", "Center", "Right"};
            int align = static_cast<int>(widget.textAlign);
            if (ImGui::Combo("Text Align", &align, alignLabels, 3)) {
                widget.textAlign = static_cast<Galeshi::UITextAlign>(
                    std::clamp(align, 0, 2));
            }
            ImGui::DragFloat("Wrap Width", &widget.textWrapWidth, 1.0f,
                             0.0f, 10000.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("0 wraps at the widget width.");
            }
            ImGui::DragFloat2("Text Offset (px)",
                              glm::value_ptr(widget.textOffset), 1.0f);
            ImGui::DragFloat("Text Rotation", &widget.rotation, 0.5f,
                             -360.0f, 360.0f, "%.1f deg");
            break;
        }
        case Galeshi::UIWidgetType::Slider:
        case Galeshi::UIWidgetType::Progress: {
            ImGui::DragFloatRange2("Range", &widget.sliderMin, &widget.sliderMax,
                                   0.01f, -10000.0f, 10000.0f);
            ImGui::SliderFloat("Value", &widget.sliderValue, widget.sliderMin,
                               widget.sliderMax);
            if (widget.type == Galeshi::UIWidgetType::Slider) {
                ImGui::DragFloat("Step", &widget.sliderStep, 0.01f, 0.0f,
                                 1000.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("0 = continuous slider.");
                }
            }
            break;
        }
        case Galeshi::UIWidgetType::CheckBox:
        case Galeshi::UIWidgetType::Toggle:
            ImGui::Checkbox("Checked", &widget.checked);
            break;
        case Galeshi::UIWidgetType::Input:
            ImGui::DragInt("Max Chars", &widget.maxChars, 1.0f, 1, 4096);
            break;
        case Galeshi::UIWidgetType::Dropdown: {
            for (int optionIndex = 0;
                 optionIndex < static_cast<int>(widget.options.size());
                 ++optionIndex) {
                ImGui::PushID(optionIndex);
                const std::string label =
                    "Option " + std::to_string(optionIndex + 1);
                UIEventInput(label.c_str(),
                             widget.options[static_cast<size_t>(optionIndex)]);
                ImGui::PopID();
            }
            if (ImGui::Button("Add Option")) {
                widget.options.push_back(
                    "Option " + std::to_string(widget.options.size() + 1));
            }
            ImGui::SameLine();
            ImGui::BeginDisabled(widget.options.empty());
            if (ImGui::Button("Remove Last Option")) {
                widget.options.pop_back();
            }
            ImGui::EndDisabled();
            if (!widget.options.empty()) {
                widget.selectedOption = std::clamp(
                    widget.selectedOption, 0,
                    static_cast<int>(widget.options.size()) - 1);
                ImGui::SliderInt("Selected Option",
                                 &widget.selectedOption, 0,
                                 static_cast<int>(widget.options.size()) - 1);
            }
            break;
        }
        case Galeshi::UIWidgetType::Image:
            drawImageDropField("Image Path", widget.imagePath);
            break;
        default:
            break;
        }

        ImGui::Separator();
        ImGui::TextDisabled("UI EVENTS (LUA)");
        UIEventInput("On Show", widget.onShow);
        UIEventInput("On Hover", widget.onHover);
        UIEventInput("On Click", widget.onClick);

        if (ImGui::Button("+ Add Widget")) AddDefaultUIWidget(Galeshi::UIWidgetType::Button);
        ImGui::SameLine();
        if (ImGui::Button("Open Script")) ImGui::SetWindowFocus("Code Editor");
        ImGui::SameLine();
        if (ImGui::Button("Delete Widget")) {
            const int index = static_cast<int>(&widget - canvas.widgets.data());
            if (index >= 0 && index < static_cast<int>(canvas.widgets.size())) {
                canvas.widgets.erase(canvas.widgets.begin() + index);
                for (auto& other : canvas.widgets) {
                    if (other.parent == index) other.parent = -1;
                    else if (other.parent > index) --other.parent;
                }
                m_uiWidgetIndex = -1;
                PushHistory();
            }
        }
    }

    bool DrawUICanvasOverlay(Galeshi::UICanvas& canvas, ImVec2 imageMin,
                             ImVec2 imageMax) {
        ImDrawList* draw = ImGui::GetWindowDrawList();
        const ImVec2 imageSize(imageMax.x - imageMin.x, imageMax.y - imageMin.y);
        if (imageSize.x < 64.0f || imageSize.y < 64.0f) return false;

        const glm::vec2 canvasSize(
            std::max(1.0f, static_cast<float>(canvas.referenceResolution.x)),
            std::max(1.0f, static_cast<float>(canvas.referenceResolution.y)));
        const float zoom = static_cast<float>(m_uiZoomPercent) / 100.0f;
        const float fitScale = std::min((imageSize.x - 80.0f) / canvasSize.x,
                                        (imageSize.y - 80.0f) / canvasSize.y);
        float scale = std::min(zoom, fitScale);
        scale = std::max(scale, 0.05f);

        const ImVec2 displaySize(canvasSize.x * scale, canvasSize.y * scale);
        const ImVec2 origin(imageMin.x + (imageSize.x - displaySize.x) * 0.5f,
                            imageMin.y + (imageSize.y - displaySize.y) * 0.5f);
        const ImVec2 canvasMax(origin.x + displaySize.x, origin.y + displaySize.y);

        auto toScreen = [&](glm::vec2 p) {
            return ImVec2(origin.x + p.x * scale, origin.y + p.y * scale);
        };
        auto toU32 = [](const glm::vec4& c) {
            return ImGui::ColorConvertFloat4ToU32(
                ImVec4(c.r, c.g, c.b, std::clamp(c.a, 0.0f, 1.0f)));
        };
        auto scaledTextSize = [](ImFont* font, const char* text,
                                 float fontSize) {
            return (font ? font : ImGui::GetFont())
                ->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        };
        auto textureFor = [this](const std::string& path) -> ImTextureID {
            if (!m_assets || path.empty()) return 0;
            auto texture = m_assets->LoadTexture(path, true);
            if (!texture || texture == m_assets->WhiteTexture() ||
                !texture->Valid()) {
                return 0;
            }
            return static_cast<ImTextureID>(texture->Handle().idx);
        };
        auto textColorFor = [&](const Galeshi::UIWidget& widget,
                                ImU32 fallback) {
            if (widget.useTextColor) return toU32(widget.textColor);
            if (widget.useColor &&
                widget.type == Galeshi::UIWidgetType::Text) {
                return toU32(widget.color);
            }
            return fallback;
        };
        auto addTextRotated = [&](ImFont* font, float fontSize, ImVec2 pos,
                                  ImU32 color, const char* text,
                                  ImVec2 pivot, float rotation) {
            const int firstVertex = draw->VtxBuffer.Size;
            draw->AddText(font, fontSize, pos, color, text);
            if (rotation == 0.0f) return;
            const float radians =
                rotation * 3.14159265358979323846f / 180.0f;
            const float c = std::cos(radians);
            const float s = std::sin(radians);
            for (int i = firstVertex; i < draw->VtxBuffer.Size; ++i) {
                ImVec2& p = draw->VtxBuffer[i].pos;
                const float x = p.x - pivot.x;
                const float y = p.y - pivot.y;
                p.x = pivot.x + x * c - y * s;
                p.y = pivot.y + x * s + y * c;
            }
        };

        const ImU32 kYellow = IM_COL32(255, 210, 60, 255);
        const ImU32 kBorder = IM_COL32(70, 70, 70, 220);
        const ImU32 kText = IM_COL32(220, 220, 220, 230);

        // Canvas backdrop and outline.
        draw->AddRectFilled(origin, canvasMax, IM_COL32(7, 16, 29, 228));
        draw->AddRect(origin, canvasMax, IM_COL32(0, 170, 255, 220), 0.0f, 0,
                      2.0f);

        // Header labels.
        draw->AddText(ImVec2(origin.x, origin.y - 20.0f),
                      IM_COL32(120, 220, 255, 255), canvas.canvasId.c_str());
        char info[192];
        const char* renderMode =
            canvas.renderMode == Galeshi::UICanvasRenderMode::ScreenSpaceCamera
                ? "Screen Space - Camera"
                : canvas.renderMode == Galeshi::UICanvasRenderMode::WorldSpace
                      ? "World Space"
                      : "Screen Space Overlay";
        std::snprintf(info, sizeof(info), "%s | %d x %d | Z-sorted", renderMode,
                      canvas.referenceResolution.x, canvas.referenceResolution.y);
        draw->AddText(ImVec2(origin.x + 110.0f, origin.y - 20.0f),
                      IM_COL32(200, 200, 200, 200), info);
        std::snprintf(info, sizeof(info), "scale %.0f%%", scale * 100.0f);
        draw->AddText(ImVec2(canvasMax.x - 90.0f, origin.y - 20.0f),
                      IM_COL32(200, 200, 200, 200), info);

        // Safe area.
        if (m_uiSafeArea) {
            const ImVec2 safeMin(origin.x + displaySize.x * 0.05f,
                                 origin.y + displaySize.y * 0.05f);
            const ImVec2 safeMax(canvasMax.x - displaySize.x * 0.05f,
                                 canvasMax.y - displaySize.y * 0.05f);
            draw->AddRect(safeMin, safeMax, IM_COL32(0, 255, 120, 110), 0.0f, 0,
                          1.0f);
            draw->AddText(ImVec2(safeMin.x + 4.0f, safeMin.y + 4.0f),
                          IM_COL32(0, 255, 120, 170), "Safe Area");
        }

        std::vector<size_t> order(canvas.widgets.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::stable_sort(order.begin(), order.end(),
                         [&](size_t a, size_t b) {
                             return canvas.widgets[a].zOrder <
                                    canvas.widgets[b].zOrder;
                         });

        for (size_t index : order) {
            Galeshi::UIWidget& widget = canvas.widgets[index];
            if (!widget.visible) continue;

            const glm::vec2 size(
                std::max(widget.size.x, 8.0f),
                std::max(widget.size.y,
                         widget.type == Galeshi::UIWidgetType::Text ? 18.0f : 8.0f));
            const ImVec2 wmin = toScreen(widget.position);
            const ImVec2 wmax(wmin.x + size.x * scale, wmin.y + size.y * scale);
            const ImU32 accent = widget.useColor ? toU32(widget.color)
                                                 : IM_COL32(37, 99, 170, 255);
            const ImU32 borderAccent =
                widget.useBorderColor ? toU32(widget.borderColor) : kBorder;

            switch (widget.type) {
            case Galeshi::UIWidgetType::Panel:
                draw->AddRectFilled(wmin, wmax,
                                    widget.useColor ? toU32(widget.color)
                                                    : IM_COL32(18, 30, 48, 222),
                                    widget.radius * scale);
                if (widget.border > 0.0f) {
                    draw->AddRect(
                        wmin, wmax,
                        widget.useBorderColor
                            ? borderAccent
                            : IM_COL32(90, 140, 200, 160),
                        widget.radius * scale, 0,
                        std::max(1.0f, widget.border * scale * 0.5f));
                }
                break;
            case Galeshi::UIWidgetType::Button: {
                const ImTextureID image = textureFor(widget.imagePath);
                if (widget.useColor) {
                    draw->AddRectFilled(wmin, wmax, accent,
                                        widget.radius * scale);
                }
                if (image) {
                    draw->AddImageRounded(
                        image, wmin, wmax, ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        IM_COL32(255, 255, 255, 255),
                        widget.radius * scale);
                } else {
                    draw->AddRectFilled(wmin, wmax, accent,
                                        widget.radius * scale);
                }
                ImFont* widgetFont =
                    m_uiRuntime.FontForPath(widget.fontPath);
                const float fontSize = widget.fontSize > 0.0f
                    ? widget.fontSize * scale
                    : ImGui::GetFontSize();
                const ImVec2 textSize = scaledTextSize(
                    widgetFont, widget.label.c_str(), fontSize);
                const ImVec2 textPos(
                    wmin.x + (wmax.x - wmin.x - textSize.x) * 0.5f +
                        widget.textOffset.x * scale,
                    wmin.y + (wmax.y - wmin.y - textSize.y) * 0.5f +
                        widget.textOffset.y * scale);
                addTextRotated(
                    widgetFont ? widgetFont : ImGui::GetFont(), fontSize,
                    textPos,
                    textColorFor(widget, IM_COL32(255, 255, 255, 255)),
                    widget.label.c_str(),
                    ImVec2((wmin.x + wmax.x) * 0.5f,
                           (wmin.y + wmax.y) * 0.5f),
                    widget.rotation);
                break;
            }
            case Galeshi::UIWidgetType::Text: {
                const float fontSize = std::clamp(
                    widget.fontSize > 0.0f
                        ? widget.fontSize * scale
                        : size.y * scale * 0.62f,
                    6.0f, 160.0f);
                ImFont* widgetFont =
                    m_uiRuntime.FontForPath(widget.fontPath);
                const ImVec2 textSize =
                    scaledTextSize(widgetFont, widget.label.c_str(), fontSize);
                float textX = wmin.x + widget.textOffset.x * scale;
                const float textY = wmin.y + widget.textOffset.y * scale;
                if (widget.textAlign == Galeshi::UITextAlign::Center) {
                    textX += (wmax.x - wmin.x - textSize.x) * 0.5f;
                } else if (widget.textAlign == Galeshi::UITextAlign::Right) {
                    textX += wmax.x - wmin.x - textSize.x;
                }
                addTextRotated(widgetFont ? widgetFont : ImGui::GetFont(),
                               fontSize, ImVec2(textX, textY),
                               textColorFor(widget, kText),
                               widget.label.c_str(),
                               ImVec2((wmin.x + wmax.x) * 0.5f,
                                      (wmin.y + wmax.y) * 0.5f),
                               widget.rotation);
                break;
            }
            case Galeshi::UIWidgetType::Image: {
                draw->AddRectFilled(
                    wmin, wmax,
                    widget.useColor ? toU32(widget.color)
                                    : IM_COL32(30, 30, 30, 210),
                    widget.radius * scale);
                if (const ImTextureID image = textureFor(widget.imagePath)) {
                    draw->AddImageRounded(
                        image, wmin, wmax, ImVec2(0.0f, 0.0f),
                        ImVec2(1.0f, 1.0f),
                        IM_COL32(255, 255, 255, 255),
                        widget.radius * scale);
                } else {
                    const char* caption =
                        !widget.imagePath.empty()
                            ? widget.imagePath.c_str()
                            : (!widget.label.empty() ? widget.label.c_str()
                                                     : "[Image]");
                    const float fontSize =
                        std::clamp(12.0f * scale, 9.0f, 18.0f);
                    const ImVec2 textSize =
                        scaledTextSize(nullptr, caption, fontSize);
                    draw->AddText(
                        ImGui::GetFont(), fontSize,
                        ImVec2(wmin.x +
                                   (wmax.x - wmin.x - textSize.x) * 0.5f,
                               wmin.y +
                                   (wmax.y - wmin.y - textSize.y) * 0.5f),
                        textColorFor(widget,
                                     IM_COL32(150, 150, 150, 255)),
                        caption);
                }
                draw->AddRect(wmin, wmax, borderAccent,
                              widget.radius * scale);
                break;
            }
            case Galeshi::UIWidgetType::Progress: {
                draw->AddRectFilled(
                    wmin, wmax,
                    widget.useColor ? toU32(widget.color)
                                    : IM_COL32(25, 25, 25, 220),
                    widget.radius * scale);
                const float fraction = std::clamp(widget.sliderValue, 0.0f, 1.0f);
                draw->AddRectFilled(wmin,
                                    ImVec2(wmin.x + (wmax.x - wmin.x) * fraction,
                                           wmax.y),
                                    accent, widget.radius * scale);
                const ImVec2 textSize = ImGui::CalcTextSize(widget.label.c_str());
                draw->AddText(
                    ImVec2(wmin.x + (wmax.x - wmin.x - textSize.x) * 0.5f,
                           wmin.y + (wmax.y - wmin.y - textSize.y) * 0.5f),
                    textColorFor(widget, IM_COL32(255, 255, 255, 255)),
                    widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::Slider: {
                const float centerY = (wmin.y + wmax.y) * 0.5f;
                draw->AddRectFilled(ImVec2(wmin.x, centerY - 3.0f),
                                    ImVec2(wmax.x, centerY + 3.0f),
                                    IM_COL32(40, 40, 40, 230), 3.0f);
                const float fraction = std::clamp(widget.sliderValue, 0.0f, 1.0f);
                draw->AddRectFilled(ImVec2(wmin.x, centerY - 3.0f),
                                    ImVec2(wmin.x + (wmax.x - wmin.x) * fraction,
                                           centerY + 3.0f),
                                    accent, 3.0f);
                draw->AddCircleFilled(
                    ImVec2(wmin.x + (wmax.x - wmin.x) * fraction, centerY),
                    6.0f, IM_COL32(220, 220, 220, 255));
                draw->AddText(ImVec2(wmin.x, wmin.y - 16.0f),
                              textColorFor(widget, kText),
                              widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::CheckBox:
            case Galeshi::UIWidgetType::Toggle: {
                const float box = std::min(18.0f, wmax.y - wmin.y);
                draw->AddRectFilled(
                    wmin, ImVec2(wmin.x + box, wmin.y + box),
                    widget.useColor ? toU32(widget.color)
                                    : IM_COL32(30, 30, 30, 230),
                    3.0f);
                draw->AddRect(wmin, ImVec2(wmin.x + box, wmin.y + box), kBorder,
                              3.0f);
                if (widget.checked) {
                    draw->AddRectFilled(ImVec2(wmin.x + 4.0f, wmin.y + 4.0f),
                                        ImVec2(wmin.x + box - 4.0f,
                                               wmin.y + box - 4.0f),
                                        accent, 2.0f);
                }
                draw->AddText(ImVec2(wmin.x + box + 6.0f,
                                     wmin.y + (box - ImGui::GetFontSize()) * 0.5f),
                              textColorFor(widget, kText),
                              widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::Input:
            case Galeshi::UIWidgetType::Dropdown:
            case Galeshi::UIWidgetType::ScrollList:
            case Galeshi::UIWidgetType::LayoutGroup: {
                draw->AddRectFilled(
                    wmin, wmax,
                    widget.useColor ? toU32(widget.color)
                                    : IM_COL32(25, 25, 25, 220),
                    widget.radius * scale);
                draw->AddRect(wmin, wmax, borderAccent,
                              widget.radius * scale);
                const char* preview = widget.label.c_str();
                if (widget.type == Galeshi::UIWidgetType::Dropdown &&
                    !widget.options.empty()) {
                    const int selected = std::clamp(
                        widget.selectedOption, 0,
                        static_cast<int>(widget.options.size()) - 1);
                    preview =
                        widget.options[static_cast<size_t>(selected)].c_str();
                }
                draw->AddText(ImVec2(wmin.x + 4.0f, wmin.y + 4.0f),
                              textColorFor(widget,
                                           IM_COL32(160, 160, 160, 255)),
                              preview);
                break;
            }
            default:
                draw->AddRect(wmin, wmax, kBorder);
                break;
            }

            // Z tag.
            if (m_uiZTags) {
                char zTag[16];
                std::snprintf(zTag, sizeof(zTag), "z%d", widget.zOrder);
                const ImVec2 tagSize = ImGui::CalcTextSize(zTag);
                const ImVec2 tagMin(wmax.x + 2.0f, wmin.y);
                draw->AddRectFilled(tagMin,
                                    ImVec2(tagMin.x + tagSize.x + 6.0f,
                                           tagMin.y + tagSize.y + 2.0f),
                                    IM_COL32(0, 0, 0, 170), 2.0f);
                draw->AddText(ImVec2(tagMin.x + 3.0f, tagMin.y + 1.0f),
                              IM_COL32(180, 220, 255, 230), zTag);
            }

            // Selection frame + handles.
            if (m_uiCanvasEntity != entt::null &&
                m_uiWidgetIndex == static_cast<int>(index)) {
                draw->AddRect(ImVec2(wmin.x - 2.0f, wmin.y - 2.0f),
                              ImVec2(wmax.x + 2.0f, wmax.y + 2.0f), kYellow,
                              0.0f, 0, 2.0f);
                const float handle = 3.0f;
                const ImVec2 points[8] = {
                    wmin,
                    ImVec2((wmin.x + wmax.x) * 0.5f, wmin.y),
                    ImVec2(wmax.x, wmin.y),
                    ImVec2(wmax.x, (wmin.y + wmax.y) * 0.5f),
                    wmax,
                    ImVec2((wmin.x + wmax.x) * 0.5f, wmax.y),
                    ImVec2(wmin.x, wmax.y),
                    ImVec2(wmin.x, (wmin.y + wmax.y) * 0.5f)};
                for (const ImVec2& point : points) {
                    draw->AddRectFilled(ImVec2(point.x - handle, point.y - handle),
                                        ImVec2(point.x + handle, point.y + handle),
                                        kYellow);
                }
                char sizeLabel[64];
                std::snprintf(sizeLabel, sizeof(sizeLabel), "%.0f x %.0f",
                              widget.size.x, widget.size.y);
                const ImVec2 labelSize = ImGui::CalcTextSize(sizeLabel);
                const ImVec2 labelMin(
                    wmin.x + (wmax.x - wmin.x - labelSize.x) * 0.5f - 4.0f,
                    wmax.y + 6.0f);
                draw->AddRectFilled(
                    labelMin,
                    ImVec2(labelMin.x + labelSize.x + 8.0f,
                           labelMin.y + labelSize.y + 4.0f),
                    IM_COL32(20, 20, 20, 220), 2.0f);
                draw->AddText(ImVec2(labelMin.x + 4.0f, labelMin.y + 2.0f),
                              kYellow, sizeLabel);
            }
        }

        // Highlight the canvas itself when no widget is selected.
        if (m_uiWidgetIndex < 0) {
            draw->AddRect(ImVec2(origin.x - 2.0f, origin.y - 2.0f),
                          ImVec2(canvasMax.x + 2.0f, canvasMax.y + 2.0f), kYellow,
                          0.0f, 0, 2.5f);
        }

        // Canvas size label under the frame.
        char canvasLabel[64];
        std::snprintf(canvasLabel, sizeof(canvasLabel), "%.0f x %.0f",
                      canvasSize.x, canvasSize.y);
        const ImVec2 canvasLabelSize = ImGui::CalcTextSize(canvasLabel);
        draw->AddText(
            ImVec2(origin.x + (displaySize.x - canvasLabelSize.x) * 0.5f,
                   canvasMax.y + 6.0f),
            IM_COL32(230, 230, 230, 255), canvasLabel);

        // --- Input: select / move / resize widgets ---
        bool consumed = false;
        const ImVec2 mouse = ImGui::GetMousePos();
        const bool hovered = ImGui::IsWindowHovered() &&
                             ImGui::IsMouseHoveringRect(imageMin, imageMax);
        const bool mouseNearCanvas =
            hovered && mouse.x >= origin.x - 6.0f &&
            mouse.x <= canvasMax.x + 6.0f && mouse.y >= origin.y - 6.0f &&
            mouse.y <= canvasMax.y + 6.0f;
        m_uiOverlayHovered = mouseNearCanvas;

        auto widgetScreenBounds = [&](const Galeshi::UIWidget& widget,
                                      ImVec2& wmin, ImVec2& wmax) {
            const glm::vec2 size(std::max(widget.size.x, 8.0f),
                                 std::max(widget.size.y, 8.0f));
            wmin = toScreen(widget.position);
            wmax = ImVec2(wmin.x + size.x * scale, wmin.y + size.y * scale);
        };

        int resizeHandle = -1;
        if (m_uiTool == 2 && m_uiWidgetIndex >= 0 &&
            m_uiWidgetIndex < static_cast<int>(canvas.widgets.size())) {
            const Galeshi::UIWidget& selected =
                canvas.widgets[static_cast<size_t>(m_uiWidgetIndex)];
            if (selected.visible && !selected.locked) {
                ImVec2 wmin, wmax;
                widgetScreenBounds(selected, wmin, wmax);
                const ImVec2 points[8] = {
                    wmin,
                    ImVec2((wmin.x + wmax.x) * 0.5f, wmin.y),
                    ImVec2(wmax.x, wmin.y),
                    ImVec2(wmax.x, (wmin.y + wmax.y) * 0.5f),
                    wmax,
                    ImVec2((wmin.x + wmax.x) * 0.5f, wmax.y),
                    ImVec2(wmin.x, wmax.y),
                    ImVec2(wmin.x, (wmin.y + wmax.y) * 0.5f)};
                for (int handle = 0; handle < 8; ++handle) {
                    if (std::abs(mouse.x - points[handle].x) <= 7.0f &&
                        std::abs(mouse.y - points[handle].y) <= 7.0f) {
                        resizeHandle = handle;
                        break;
                    }
                }
            }
        }

        if (!m_uiDragActive && resizeHandle >= 0) {
            switch (resizeHandle) {
            case 0:
            case 4:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNWSE);
                break;
            case 2:
            case 6:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNESW);
                break;
            case 1:
            case 5:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
                break;
            default:
                ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
                break;
            }
        }

        if (mouseNearCanvas && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            const entt::entity entity = ActiveUICanvasEntity();
            if (entity != entt::null) {
                m_editor.Select(Galeshi::Entity(entity, &m_scene));
            }

            bool startedDrag = false;
            if (resizeHandle >= 0) {
                // Resize with the visible corner/edge handles.  The selected
                // widget's rectangle is used so the handle under the cursor
                // always wins over a widget that happens to overlap it.
                Galeshi::UIWidget& widget =
                    canvas.widgets[static_cast<size_t>(m_uiWidgetIndex)];
                m_uiDragActive = true;
                m_uiResizeMode = true;
                m_uiDragDirty = false;
                m_uiResizeHandle = resizeHandle;
                m_uiDragWidget = m_uiWidgetIndex;
                m_uiDragStartMouse = mouse;
                m_uiDragStartPosition = widget.position;
                m_uiDragStartSize = widget.size;
                startedDrag = true;
            } else {
                int hit = -1;
                for (auto it = order.rbegin(); it != order.rend(); ++it) {
                    const size_t index = *it;
                    Galeshi::UIWidget& widget = canvas.widgets[index];
                    if (!widget.visible || widget.locked) continue;
                    ImVec2 wmin, wmax;
                    widgetScreenBounds(widget, wmin, wmax);
                    if (mouse.x >= wmin.x && mouse.x <= wmax.x &&
                        mouse.y >= wmin.y && mouse.y <= wmax.y) {
                        hit = static_cast<int>(index);
                        break;
                    }
                }

                if (hit >= 0) {
                    m_uiWidgetIndex = hit;
                    Galeshi::UIWidget& widget =
                        canvas.widgets[static_cast<size_t>(hit)];
                    if (m_uiTool == 1) {
                        m_uiDragActive = true;
                        m_uiResizeMode = false;
                        m_uiDragDirty = false;
                        m_uiResizeHandle = -1;
                        m_uiDragWidget = hit;
                        m_uiDragStartMouse = mouse;
                        m_uiDragStartPosition = widget.position;
                        m_uiDragStartSize = widget.size;
                        startedDrag = true;
                    }
                } else {
                    m_uiWidgetIndex = -1;
                }
            }

            if (!startedDrag) {
                m_uiDragActive = false;
                m_uiResizeMode = false;
                m_uiDragDirty = false;
                m_uiResizeHandle = -1;
                m_uiDragWidget = -1;
            }
            consumed = true;
        }

        if (m_uiDragActive && m_uiDragWidget >= 0 &&
            m_uiDragWidget < static_cast<int>(canvas.widgets.size())) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                const ImVec2 delta(mouse.x - m_uiDragStartMouse.x,
                                   mouse.y - m_uiDragStartMouse.y);
                Galeshi::UIWidget& widget =
                    canvas.widgets[static_cast<size_t>(m_uiDragWidget)];
                auto snapValue = [&](float value) {
                    if (!m_uiSnap || m_uiSnapPx <= 0.0f) return value;
                    return std::round(value / m_uiSnapPx) * m_uiSnapPx;
                };
                glm::vec2 newPosition = widget.position;
                glm::vec2 newSize = widget.size;
                if (m_uiResizeMode) {
                    const float dx = delta.x / scale;
                    const float dy = delta.y / scale;
                    float left = m_uiDragStartPosition.x;
                    float top = m_uiDragStartPosition.y;
                    float right = left + m_uiDragStartSize.x;
                    float bottom = top + m_uiDragStartSize.y;
                    const bool moveLeft = m_uiResizeHandle == 0 ||
                                          m_uiResizeHandle == 6 ||
                                          m_uiResizeHandle == 7;
                    const bool moveRight = m_uiResizeHandle == 2 ||
                                           m_uiResizeHandle == 3 ||
                                           m_uiResizeHandle == 4;
                    const bool moveTop = m_uiResizeHandle == 0 ||
                                         m_uiResizeHandle == 1 ||
                                         m_uiResizeHandle == 2;
                    const bool moveBottom = m_uiResizeHandle == 4 ||
                                            m_uiResizeHandle == 5 ||
                                            m_uiResizeHandle == 6;
                    if (moveLeft) left = snapValue(left + dx);
                    if (moveRight) right = snapValue(right + dx);
                    if (moveTop) top = snapValue(top + dy);
                    if (moveBottom) bottom = snapValue(bottom + dy);

                    constexpr float kMinWidgetSize = 16.0f;
                    if (right - left < kMinWidgetSize) {
                        if (moveLeft) left = right - kMinWidgetSize;
                        else right = left + kMinWidgetSize;
                    }
                    if (bottom - top < kMinWidgetSize) {
                        if (moveTop) top = bottom - kMinWidgetSize;
                        else bottom = top + kMinWidgetSize;
                    }

                    newPosition = {left, top};
                    newSize = {right - left, bottom - top};
                } else {
                    newPosition = m_uiDragStartPosition +
                                  glm::vec2(delta.x, delta.y) / scale;
                    newPosition.x = snapValue(newPosition.x);
                    newPosition.y = snapValue(newPosition.y);
                }

                if (newPosition != widget.position || newSize != widget.size) {
                    // Record the pre-drag state on the first actual movement.
                    // The final state is recorded when the mouse is released,
                    // matching the transform-gizmo undo behaviour.
                    if (!m_uiDragDirty) {
                        PushHistory();
                        m_uiDragDirty = true;
                    }
                    widget.position = newPosition;
                    widget.size = newSize;
                }
                consumed = true;
            } else {
                if (m_uiDragDirty) {
                    PushHistory();
                    m_uiDragDirty = false;
                }
                m_uiDragActive = false;
                m_uiResizeMode = false;
                m_uiResizeHandle = -1;
                m_uiDragWidget = -1;
            }
        } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
            m_uiDragActive = false;
            m_uiResizeMode = false;
            m_uiDragDirty = false;
            m_uiResizeHandle = -1;
            m_uiDragWidget = -1;
        }

        return consumed || mouseNearCanvas;
    }

    void DrawDockspace() {
        ImGuiViewport* vp = ImGui::GetMainViewport();
        // Keep docked panels below the main menu bar.
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));

        ImGui::Begin("DockSpace", nullptr,
            ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse |
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
            ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus);

        ImGui::PopStyleVar(3);

        const ImGuiID dockspaceId = ImGui::GetID("GaleshiDockspace");
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr || m_resetLayout) {
            m_resetLayout = false;
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, vp->WorkSize);

            ImGuiID center = dockspaceId;
            ImGuiID right  = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, nullptr, &center);
            ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down,  0.35f, nullptr, &center);
            ImGuiID left   = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left,  0.20f, nullptr, &center);

            // Split the bottom area into Left (Content Browser) and Right (Code Editor)
            ImGuiID bottomLeft = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.20f, nullptr, &bottom);

            ImGui::DockBuilderDockWindow("Viewport",          center);
            ImGui::DockBuilderDockWindow("World Outliner",    left);
            ImGui::DockBuilderDockWindow("Details",           right);
            
            // Left side of bottom panel
            ImGui::DockBuilderDockWindow("Content Browser",   bottomLeft);
            
            // Right side of bottom panel (tabs)
            ImGui::DockBuilderDockWindow("UI Designer",       bottom);
            ImGui::DockBuilderDockWindow("Output Log",        bottom);
            ImGui::DockBuilderDockWindow("Animation State Machine Editor",
                                         bottom);
            ImGui::DockBuilderDockWindow("Code Editor",        bottom);
            ImGui::DockBuilderDockWindow("Lua Console",       bottom);
            ImGui::DockBuilderDockWindow("Script Components", bottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Gameplay camera
    // -------------------------------------------------------------------
    Galeshi::Entity FindGameplayCamera() {
        auto view = m_scene.Registry().view<Galeshi::CameraComponent,
                                             Galeshi::TransformComponent>();
        Galeshi::Entity best;
        int bestPriority = std::numeric_limits<int>::min();
        bool bestAttached = false;
        for (auto e : view) {
            const auto& camera =
                view.get<Galeshi::CameraComponent>(e);
            if (!camera.primary) continue;
            const bool attached =
                m_scene.Registry().all_of<Galeshi::ParentComponent>(e);
            if (!best ||
                camera.priority > bestPriority ||
                (camera.priority == bestPriority && attached && !bestAttached)) {
                best = Galeshi::Entity(e, &m_scene);
                bestPriority = camera.priority;
                bestAttached = attached;
            }
        }
        return best;
    }

    bool IsEntityInHierarchy(entt::entity entity, entt::entity root) {
        if (root == entt::null) return false;
        auto& reg = m_scene.Registry();
        for (int depth = 0; depth < 128 && entity != entt::null; ++depth) {
            if (entity == root) return true;
            if (!reg.all_of<Galeshi::ParentComponent>(entity)) break;
            entity = reg.get<Galeshi::ParentComponent>(entity).parent;
        }
        return false;
    }

    // Camera blocking is limited to authored colliders. Meshes without an
    // active ColliderComponent and trigger volumes never block the view.
    bool IsCameraCollisionBlocker(entt::entity entity,
                                  entt::entity ignoreRoot) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(entity)) return false;
        const auto* collider =
            reg.try_get<Galeshi::ColliderComponent>(entity);
        if (!collider || collider->isTrigger) return false;
        if (ignoreRoot != entt::null &&
            (IsEntityInHierarchy(entity, ignoreRoot) ||
             IsEntityInHierarchy(ignoreRoot, entity))) {
            return false;
        }
        return true;
    }

    bool IsGameplayCameraAttachedToPlayer(entt::entity cameraEntity) {
        auto& reg = m_scene.Registry();
        entt::entity current = cameraEntity;
        for (int depth = 0; depth < 128; ++depth) {
            if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
            current = reg.get<Galeshi::ParentComponent>(current).parent;
            if (current == entt::null) break;
            if (reg.all_of<Galeshi::PlayerTagComponent>(current)) {
                return true;
            }
            if (reg.all_of<Galeshi::NameComponent>(current) &&
                reg.get<Galeshi::NameComponent>(current).name == "Player") {
                return true;
            }
        }
        return false;
    }

    // Player-tagged entities are the orbit centre for the gameplay camera.
    // Prefer a Player in the camera's own hierarchy so a stray Player elsewhere
    // in the scene cannot make an attached camera jump away from its character.
    entt::entity FindGameplayCameraTarget(entt::entity cameraEntity) {
        auto& reg = m_scene.Registry();
        entt::entity characterAncestor = entt::null;
        entt::entity current = cameraEntity;
        for (int depth = 0; depth < 128; ++depth) {
            if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
            current = reg.get<Galeshi::ParentComponent>(current).parent;
            if (current == entt::null) break;
            if (reg.all_of<Galeshi::TransformComponent>(current) &&
                reg.all_of<Galeshi::PlayerTagComponent>(current)) {
                return current;
            }
            if (reg.all_of<Galeshi::TransformComponent>(current) &&
                reg.all_of<Galeshi::NameComponent>(current) &&
                reg.get<Galeshi::NameComponent>(current).name == "Player") {
                return current;
            }
            if (characterAncestor == entt::null &&
                (reg.all_of<Galeshi::SkeletalComponent>(current) ||
                 reg.all_of<Galeshi::ModelComponent>(current) ||
                 reg.all_of<Galeshi::MeshComponent>(current))) {
                characterAncestor = current;
            }
        }

        auto players = reg.view<Galeshi::PlayerTagComponent,
                                Galeshi::TransformComponent>();
        for (auto entity : players) return entity;

        return characterAncestor;
    }

    bool SetGameplayCameraMode(int mode) {
        if (mode != GALESHI_CAMERA_THIRD_PERSON &&
            mode != GALESHI_CAMERA_FIRST_PERSON) {
            return false;
        }
        m_gameCameraMode = static_cast<GaleshiScriptCameraMode>(mode);
        return true;
    }

    int GetGameplayCameraMode() const {
        return static_cast<int>(m_gameCameraMode);
    }

    glm::vec3 GetGameplayCameraMoveForward() const {
        glm::vec3 forward = CameraForward();
        forward.y = 0.0f;
        const float length = glm::length(forward);
        if (length < 1e-5f) return glm::vec3(0.0f, 0.0f, -1.0f);
        return forward / length;
    }

    glm::vec3 GetGameplayCameraMoveRight() const {
        return glm::normalize(glm::cross(
            GetGameplayCameraMoveForward(), glm::vec3(0.0f, 1.0f, 0.0f)));
    }

    // Cache the bounds centre, eye position and initial camera basis in world
    // space. Keeping the basis independent from the target transform prevents
    // character turning from rotating the player's camera.
    void RefreshGameplayCameraTarget(entt::entity target,
                                     entt::entity cameraEntity) {
        m_gameCameraTarget = target;
        m_gameCameraPivotLocal = glm::vec3(0.0f);
        m_gameCameraEyeLocal = glm::vec3(0.0f, 1.6f, 0.0f);
        m_gameCameraBaseOffset = glm::vec3(0.0f);
        m_gameCameraBaseForward = glm::vec3(0.0f, 0.0f, -1.0f);
        m_gameCameraBaseUp = glm::vec3(0.0f, 1.0f, 0.0f);
        m_gameCameraBaseValid = false;
        if (target == entt::null) return;

        const glm::mat4 targetWorld = EntityWorldMatrix(target);
        const glm::mat4 inverseTarget = glm::inverse(targetWorld);

        glm::vec3 center;
        glm::vec3 boundsMin;
        glm::vec3 boundsMax;
        const bool haveBounds = EntityWorldBoundsCenter(
            target, center, &boundsMin, &boundsMax);
        m_gameCameraPivotLocal = glm::vec3(
            inverseTarget * glm::vec4(center, 1.0f));
        const glm::vec3 pivot =
            glm::vec3(targetWorld * glm::vec4(m_gameCameraPivotLocal, 1.0f));

        if (haveBounds) {
            // Eye height comes from the character itself, not from the
            // third-person camera offset.
            const glm::vec3 eyeWorld{
                center.x,
                boundsMin.y + (boundsMax.y - boundsMin.y) * 0.9f,
                center.z,
            };
            m_gameCameraEyeLocal = glm::vec3(
                inverseTarget * glm::vec4(eyeWorld, 1.0f));
        }

        const glm::mat4 cameraWorld = EntityWorldMatrix(cameraEntity);
        glm::vec3 forward = glm::vec3(
            cameraWorld * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 cameraUp = glm::vec3(
            cameraWorld * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        if (glm::dot(forward, forward) < 1e-6f ||
            glm::dot(cameraUp, cameraUp) < 1e-6f) {
            forward = {0.0f, 0.0f, -1.0f};
            cameraUp = {0.0f, 1.0f, 0.0f};
        } else {
            forward = glm::normalize(forward);
            cameraUp = glm::normalize(cameraUp);
        }
        if (std::abs(glm::dot(forward, cameraUp)) > 0.999f) {
            cameraUp = std::abs(forward.y) > 0.9f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        }
        m_gameCameraBaseOffset = glm::vec3(cameraWorld[3]) - pivot;
        m_gameCameraBaseForward = forward;
        m_gameCameraBaseUp = cameraUp;
        m_gameCameraBaseValid = true;
    }

    void UpdateGameplayCamera() {
        Galeshi::Entity cameraEntity = FindGameplayCamera();
        if (!cameraEntity) {
            if (m_usingGameCamera) {
                m_usingGameCamera = false;
                RestoreEditorCamera();
            }
            return;
        }

        const auto& camera =
            cameraEntity.GetComponent<Galeshi::CameraComponent>();
        const glm::mat4 world = EntityWorldMatrix(cameraEntity.Handle());
        glm::vec3 eye = glm::vec3(world[3]);
        glm::vec3 forward = glm::vec3(world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
        glm::vec3 up = glm::vec3(world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
        if (glm::dot(forward, forward) < 1e-6f ||
            glm::dot(up, up) < 1e-6f) {
            forward = {0.0f, 0.0f, -1.0f};
            up = {0.0f, 1.0f, 0.0f};
        } else {
            forward = glm::normalize(forward);
            up = glm::normalize(up);
        }
        if (std::abs(glm::dot(forward, up)) > 0.999f) {
            up = std::abs(forward.y) > 0.9f
                ? glm::vec3(0.0f, 0.0f, 1.0f)
                : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Mouse-look orbits the gameplay camera around the character without
        // changing the scene entity. This keeps gameplay scripts free to
        // move/rotate the character while the player looks around with RMB.
        const bool gameLookInput =
            (m_editor.isPlaying || m_isGameMode) &&
            (m_gameLooking ||
             ((m_viewportHovered || m_isGameMode) &&
              !m_uiRuntime.IsPointerOverUI())) &&
            !ImGui::GetIO().WantTextInput &&
            !ImGui::GetIO().AppFocusLost &&
            ImGui::IsMouseDown(ImGuiMouseButton_Right);
        if (gameLookInput) {
            if (!m_gameLooking) {
                // Relative mode keeps the hidden cursor centered and prevents
                // it from leaving the window while the player looks around.
                m_gameLooking = true;
                SetMouseCapture(true);
                m_gameLookSkipFrames = 2;
            }
            if (m_gameLookSkipFrames > 0) {
                --m_gameLookSkipFrames;
            } else {
                const glm::vec2 delta = GetInput().MouseDelta();
                const float sensitivity =
                    m_preferences.cameraOrbitSensitivity;
                m_gameCameraYawOffset -= delta.x * sensitivity;
                m_gameCameraPitchOffset -= delta.y * sensitivity;
                m_gameCameraPitchOffset = glm::clamp(
                    m_gameCameraPitchOffset, -1.50f, 1.50f);
            }
        } else {
            EndGameMouseLook();
        }

        const bool hasLookOffset =
            std::abs(m_gameCameraYawOffset) > 1e-6f ||
            std::abs(m_gameCameraPitchOffset) > 1e-6f;
        const bool firstPerson =
            m_gameCameraMode == GALESHI_CAMERA_FIRST_PERSON;
        const float aspect = m_viewportH
            ? static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH)
            : 16.0f / 9.0f;
        m_camera.SetPerspective(
            glm::radians(glm::clamp(camera.fovY, 1.0f, 179.0f)), aspect,
            std::max(camera.nearZ, 1e-4f), std::max(camera.farZ, camera.nearZ + 0.001f));

        entt::entity target = FindGameplayCameraTarget(cameraEntity.Handle());
        if (target != m_gameCameraTarget ||
            cameraEntity.Handle() != m_activeGameCamera) {
            RefreshGameplayCameraTarget(target, cameraEntity.Handle());
        }
        const bool validTarget =
            target != entt::null && m_scene.Registry().valid(target);

        const auto applyInPlaceLook = [&]() {
            if (!hasLookOffset) return;
            const float baseYaw = std::atan2(forward.x, forward.z);
            const float basePitch =
                std::asin(glm::clamp(forward.y, -1.0f, 1.0f));
            const float yaw = baseYaw + m_gameCameraYawOffset;
            const float pitch = glm::clamp(
                basePitch + m_gameCameraPitchOffset, -1.52f, 1.52f);
            const float horizontal = std::cos(pitch);
            forward = {std::sin(yaw) * horizontal, std::sin(pitch),
                       std::cos(yaw) * horizontal};
            up = glm::vec3(0.0f, 1.0f, 0.0f);
        };

        if (firstPerson && validTarget) {
            // Stand at the character's authored eye height and rotate the view
            // in place instead of orbiting around the body.
            if (m_gameCameraBaseValid) {
                forward = m_gameCameraBaseForward;
                up = m_gameCameraBaseUp;
            }
            eye = glm::vec3(
                EntityWorldMatrix(target) *
                glm::vec4(m_gameCameraEyeLocal, 1.0f));
            applyInPlaceLook();
            m_camera.SetView(eye, eye + forward, up);
        } else {
            bool orbitActive = false;
            glm::vec3 pivot{0.0f};
            if (validTarget) {
                pivot = glm::vec3(
                    EntityWorldMatrix(target) *
                    glm::vec4(m_gameCameraPivotLocal, 1.0f));
                if (m_gameCameraBaseValid) {
                    eye = pivot + m_gameCameraBaseOffset;
                    forward = m_gameCameraBaseForward;
                    up = m_gameCameraBaseUp;
                }
                if (hasLookOffset) {
                    const glm::vec3 orbitOffset = eye - pivot;
                    const float radius = glm::length(orbitOffset);
                    if (radius > 1e-3f) {
                        // Orbit the authored offset around the character
                        // centre, keeping the current third-person distance.
                        const float baseYaw =
                            std::atan2(orbitOffset.x, orbitOffset.z);
                        const float basePitch = std::asin(glm::clamp(
                            orbitOffset.y / radius, -1.0f, 1.0f));
                        const float yaw =
                            baseYaw + m_gameCameraYawOffset;
                        const float pitch = glm::clamp(
                            basePitch - m_gameCameraPitchOffset,
                            -1.50f, 1.50f);
                        const float horizontal = std::cos(pitch);
                        eye = pivot + glm::vec3(
                            radius * horizontal * std::sin(yaw),
                            radius * std::sin(pitch),
                            radius * horizontal * std::cos(yaw));
                        orbitActive = true;
                    }
                }

                if (m_physics) {
                    const float collisionRadius =
                        std::max(0.2f, camera.nearZ);
                    const auto collisionFilter =
                        [this, target](entt::entity entity) {
                            return IsCameraCollisionBlocker(entity, target);
                        };
                    glm::vec3 collisionPoint;
                    if (m_physics->SphereCast(
                            pivot, eye, collisionRadius,
                            collisionFilter, collisionPoint)) {
                        eye = collisionPoint;
                    }
                }
            }

            if (orbitActive &&
                glm::length(eye - pivot) > 1e-3f) {
                m_camera.SetView(eye, pivot, glm::vec3(0.0f, 1.0f, 0.0f));
            } else {
                applyInPlaceLook();
                m_camera.SetView(eye, eye + forward, up);
            }
        }
        m_usingGameCamera = true;
        m_activeGameCamera = cameraEntity.Handle();
    }

    void EndEditorMouseLook() {
        if (m_editorLookDragging) SetMouseCapture(false);
        m_editorLooking = false;
        m_editorLookDragging = false;
        m_editorLookSkipFrames = 0;
    }

    void EndGameMouseLook() {
        if (m_gameLooking) SetMouseCapture(false);
        m_gameLooking = false;
        m_gameLookSkipFrames = 0;
    }

    void RestoreEditorCamera() {
        EndGameMouseLook();
        const float aspect = m_viewportH
            ? static_cast<float>(m_viewportW) / static_cast<float>(m_viewportH)
            : 16.0f / 9.0f;
        const float orbitDistance = std::max(m_editor.camRadius, 0.5f);
        const float nearZ = glm::clamp(orbitDistance * 0.0025f, 0.1f, 0.75f);
        const float farZ = std::max(1000.0f, orbitDistance * 4.0f + 500.0f);
        m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees),
                                aspect, nearZ, farZ);
        m_camera.Orbit(m_editor.camTarget, m_editor.camYaw,
                       m_editor.camPitch, m_editor.camRadius);
        m_activeGameCamera = entt::null;
    }

    bool ActivateGameplayCamera() {
        m_usingGameCamera = true;
        m_gameCameraTarget = entt::null;
        m_gameCameraPivotLocal = glm::vec3(0.0f);
        m_gameCameraEyeLocal = glm::vec3(0.0f, 1.6f, 0.0f);
        m_gameCameraBaseValid = false;
        m_gameCameraYawOffset = 0.0f;
        m_gameCameraPitchOffset = 0.0f;
        UpdateGameplayCamera();
        if (!m_usingGameCamera) {
            RestoreEditorCamera();
            return false;
        }
        spdlog::info("[Camera] Gameplay camera active: entity {}",
                     static_cast<uint32_t>(m_activeGameCamera));
        return true;
    }

    void DeactivateGameplayCamera() {
        m_usingGameCamera = false;
        RestoreEditorCamera();
    }

    // -------------------------------------------------------------------
    // Post-processing volumes
    // -------------------------------------------------------------------
    void CaptureSceneViewState() {
        Galeshi::SceneCameraState& camera = m_scene.CameraState();
        camera.valid = true;
        camera.position = m_camera.Position();

        // Camera rotation is the inverse of the view-matrix rotation.
        const glm::mat3 cameraToWorld =
            glm::transpose(glm::mat3(m_camera.View()));
        camera.rotation = glm::normalize(glm::quat_cast(cameraToWorld));

        camera.target = m_editor.camTarget;
        camera.yaw = m_editor.camYaw;
        camera.pitch = m_editor.camPitch;
        camera.radius = m_editor.camRadius;

        // Persist the global post-process state.  The manager owns the
        // toggles; the remaining values live in Scene so custom global
        // settings survive save/load without being reset to struct defaults.
        Galeshi::PostProcessSettings& pp = m_scene.GlobalPostProcess();
        pp.bloomEnabled = m_postProcess.BloomEnabled();
        pp.dofEnabled = m_postProcess.DofEnabled();
        pp.motionBlurEnabled = m_postProcess.MotionBlurEnabled();
        pp.ssaoEnabled = m_postProcess.SsaoEnabled();
        pp.aoMode = m_globalAoMode;
        pp.ssrEnabled = m_postProcess.SsrEnabled();
        pp.ssgiEnabled = m_postProcess.SsgiEnabled();
        pp.fxaaEnabled = m_postProcess.FxaaEnabled();
        pp.smaaEnabled = m_postProcess.SmaaEnabled();
        pp.taaEnabled = m_postProcess.TaaEnabled();
        pp.godRaysEnabled = m_postProcess.GodRaysEnabled();
        pp.atmosphereEnabled = m_postProcess.AtmosphereEnabled();
        pp.fogEnabled = m_postProcess.FogEnabled();
        pp.fogVolumetric = m_postProcess.FogVolumetric();
        pp.invertEnabled = m_postProcess.InvertEnabled();
        m_scene.SetHasGlobalPostProcess(true);
    }

    void ApplySceneViewState() {
        const Galeshi::SceneCameraState& camera = m_scene.CameraState();
        if (camera.valid) {
            // Position + target are authoritative.  Reconstructing the orbit
            // controls from them guarantees the restored camera position even
            // if a hand-edited scene has inconsistent yaw/pitch/radius values.
            const glm::vec3 offset = camera.position - camera.target;
            const float offsetLength = glm::length(offset);
            if (offsetLength > 1e-4f) {
                m_editor.camTarget = camera.target;
                m_editor.camRadius = glm::clamp(offsetLength, 0.5f, 200.0f);
                m_editor.camPitch = glm::clamp(
                    std::asin(glm::clamp(offset.y / offsetLength, -1.0f, 1.0f)),
                    -1.5f, 1.5f);
                m_editor.camYaw = std::atan2(offset.x, offset.z);
            } else {
                m_editor.camTarget = camera.target;
                m_editor.camYaw = camera.yaw;
                m_editor.camPitch = glm::clamp(camera.pitch, -1.5f, 1.5f);
                m_editor.camRadius = glm::clamp(camera.radius, 0.5f, 200.0f);
            }

            const float aspect = GetRenderer() && GetRenderer()->Height() > 0
                ? static_cast<float>(GetRenderer()->Width()) /
                  static_cast<float>(GetRenderer()->Height())
                : 16.0f / 9.0f;
            m_camera.SetPerspective(
                glm::radians(m_preferences.cameraFovDegrees), aspect,
                0.1f, 1000.0f);
            m_camera.Orbit(m_editor.camTarget, m_editor.camYaw,
                           m_editor.camPitch, m_editor.camRadius);
            spdlog::info("[Scene] Restored camera position=({}, {}, {}) "
                         "target=({}, {}, {})",
                         m_camera.Position().x, m_camera.Position().y,
                         m_camera.Position().z, camera.target.x, camera.target.y,
                         camera.target.z);
        }

        if (m_scene.HasGlobalPostProcess()) {
            const Galeshi::PostProcessSettings& pp =
                m_scene.GlobalPostProcess();
            m_postProcess.SetBloomEnabled(pp.bloomEnabled);
            m_postProcess.SetDofEnabled(pp.dofEnabled);
            m_postProcess.SetMotionBlurEnabled(pp.motionBlurEnabled);
            m_postProcess.SetSsaoEnabled(pp.ssaoEnabled);
            m_globalAoMode = pp.aoMode;
            m_postProcess.SetSsrEnabled(pp.ssrEnabled);
            m_postProcess.SetSsgiEnabled(pp.ssgiEnabled);
            m_postProcess.SetFxaaEnabled(pp.fxaaEnabled);
            m_postProcess.SetSmaaEnabled(pp.smaaEnabled);
            m_postProcess.SetTaaEnabled(pp.taaEnabled);
            m_postProcess.SetGodRaysEnabled(pp.godRaysEnabled);
            m_postProcess.SetAtmosphereEnabled(pp.atmosphereEnabled);
            m_postProcess.SetFogEnabled(pp.fogEnabled);
            m_postProcess.SetFogVolumetric(pp.fogVolumetric);
            m_postProcess.SetInvertEnabled(pp.invertEnabled);
            spdlog::info("[Scene] Restored global post-process settings "
                         "(environment palette={}, amount={})",
                         pp.environmentPaletteEnabled,
                         pp.environmentPaletteAmount);
        }

        // The view and effect set may have changed completely; never reuse a
        // TAA history from the previous scene/camera.
        m_postProcess.ResetTaaHistory();
    }

    float PssmPcfRadius() const {
        const float sharpness = glm::clamp(m_pssmShadowSharpness, 0.0f, 1.0f);
        return (1.0f - sharpness) * 4.0f;
    }

    Galeshi::PostProcessSettings DefaultPostProcessSettings() const {
        Galeshi::PostProcessSettings s =
            m_scene.HasGlobalPostProcess()
                ? m_scene.GlobalPostProcess()
                : Galeshi::PostProcessSettings{};
        s.bloomEnabled       = m_postProcess.BloomEnabled();
        s.dofEnabled         = m_postProcess.DofEnabled();
        s.motionBlurEnabled  = m_postProcess.MotionBlurEnabled();
        s.ssaoEnabled        = m_postProcess.SsaoEnabled();
        s.aoMode             = m_globalAoMode;
        s.ssrEnabled         = m_postProcess.SsrEnabled();
        s.ssgiEnabled        = m_postProcess.SsgiEnabled();
        s.fxaaEnabled        = m_postProcess.FxaaEnabled();
        s.smaaEnabled        = m_postProcess.SmaaEnabled();
        s.taaEnabled         = m_postProcess.TaaEnabled();
        s.godRaysEnabled     = m_postProcess.GodRaysEnabled();
        s.atmosphereEnabled  = m_postProcess.AtmosphereEnabled();
        s.fogEnabled         = m_postProcess.FogEnabled();
        s.fogVolumetric      = m_postProcess.FogVolumetric();
        s.invertEnabled      = m_postProcess.InvertEnabled();
        return s;
    }

    // The active post-process source is the highest-priority enabled volume,
    // or the scene-global settings when no volume is enabled. Both the
    // Post-Process menu and the volume inspector edit this active source, so
    // the most recently changed control always wins instead of the menu being
    // silently shadowed by a volume.
    Galeshi::PostProcessVolumeComponent* ActivePostProcessVolume() {
        auto view = m_scene.Registry()
            .view<Galeshi::PostProcessVolumeComponent>();
        Galeshi::PostProcessVolumeComponent* best = nullptr;
        int bestPriority = std::numeric_limits<int>::min();
        for (auto e : view) {
            auto& volume =
                view.get<Galeshi::PostProcessVolumeComponent>(e);
            if (!volume.enabled) continue;
            if (volume.priority > bestPriority) {
                bestPriority = volume.priority;
                best = &volume;
            }
        }
        return best;
    }

    Galeshi::PostProcessSettings& ActivePostProcessSettings() {
        if (auto* volume = ActivePostProcessVolume())
            return volume->settings;

        // With no active volume the manager toggles are authoritative. Mirror
        // them into the scene-global settings so the menu always displays the
        // values that are actually being rendered.
        Galeshi::PostProcessSettings& global = m_scene.GlobalPostProcess();
        global.bloomEnabled      = m_postProcess.BloomEnabled();
        global.dofEnabled        = m_postProcess.DofEnabled();
        global.motionBlurEnabled = m_postProcess.MotionBlurEnabled();
        global.ssaoEnabled       = m_postProcess.SsaoEnabled();
        global.aoMode            = m_globalAoMode;
        global.ssrEnabled        = m_postProcess.SsrEnabled();
        global.ssgiEnabled       = m_postProcess.SsgiEnabled();
        global.fxaaEnabled       = m_postProcess.FxaaEnabled();
        global.smaaEnabled       = m_postProcess.SmaaEnabled();
        global.taaEnabled        = m_postProcess.TaaEnabled();
        global.godRaysEnabled    = m_postProcess.GodRaysEnabled();
        global.atmosphereEnabled = m_postProcess.AtmosphereEnabled();
        global.fogEnabled        = m_postProcess.FogEnabled();
        global.fogVolumetric     = m_postProcess.FogVolumetric();
        global.invertEnabled     = m_postProcess.InvertEnabled();
        return global;
    }

    Galeshi::PostProcessSettings ResolvePostProcessSettings() {
        if (auto* volume = ActivePostProcessVolume())
            return volume->settings;
        return DefaultPostProcessSettings();
    }

    glm::vec3 CameraForward() const {
        return glm::normalize(-glm::vec3(
            m_camera.View()[0][2],
            m_camera.View()[1][2],
            m_camera.View()[2][2]));
    }

    float ViewSpaceDepth(const glm::vec3& worldPoint) const {
        const glm::vec3 cameraPos = m_camera.Position();
        const float distance = glm::dot(worldPoint - cameraPos, CameraForward());
        return std::max(distance, 0.05f);
    }

    // Returns the world-space centre of all mesh bounds below a root entity.
    // For skeletal characters the meshes live on child entities, so the root
    // alone usually only has a feet-level transform.
    bool EntityWorldBoundsCenter(entt::entity root, glm::vec3& outCenter,
                                 glm::vec3* outMin = nullptr,
                                 glm::vec3* outMax = nullptr) {
        auto& reg = m_scene.Registry();
        glm::vec3 boundsMin(std::numeric_limits<float>::max());
        glm::vec3 boundsMax(-std::numeric_limits<float>::max());
        bool haveBounds = false;

        for (auto entity : m_scene.AllEntities()) {
            const entt::entity handle = entity.Handle();
            entt::entity current = handle;
            bool belongsToRoot = false;
            int depth = 0;
            while (current != entt::null && depth++ < 128) {
                if (current == root) {
                    belongsToRoot = true;
                    break;
                }
                if (reg.all_of<Galeshi::ParentComponent>(current)) {
                    current = reg.get<Galeshi::ParentComponent>(current).parent;
                } else {
                    break;
                }
            }
            if (!belongsToRoot || !reg.all_of<Galeshi::MeshComponent>(handle)) {
                continue;
            }

            const auto& mesh = reg.get<Galeshi::MeshComponent>(handle).mesh;
            if (!mesh || !mesh->Valid()) continue;

            glm::vec3 localMin, localMax;
            mesh->LocalAABB(localMin, localMax);
            const glm::mat4 world = EntityWorldMatrix(handle);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local{
                    (corner & 1) ? localMax.x : localMin.x,
                    (corner & 2) ? localMax.y : localMin.y,
                    (corner & 4) ? localMax.z : localMin.z,
                };
                const glm::vec3 point =
                    glm::vec3(world * glm::vec4(local, 1.0f));
                boundsMin = glm::min(boundsMin, point);
                boundsMax = glm::max(boundsMax, point);
            }
            haveBounds = true;
        }

        if (haveBounds) {
            outCenter = (boundsMin + boundsMax) * 0.5f;
            if (outMin) *outMin = boundsMin;
            if (outMax) *outMax = boundsMax;
            return true;
        }

        const glm::mat4 world = EntityWorldMatrix(root);
        outCenter = glm::vec3(world[3]);
        if (reg.all_of<Galeshi::SkeletalComponent>(root)) {
            outCenter.y += 1.0f;
        }
        if (outMin) *outMin = outCenter;
        if (outMax) *outMax = outCenter;
        return false;
    }

    entt::entity FindAutoFocusEntity() {
        auto& reg = m_scene.Registry();

        // A tagged player is the intended subject in both editor preview and
        // gameplay.
        auto playerView =
            reg.view<Galeshi::PlayerTagComponent, Galeshi::TransformComponent>();
        for (auto entity : playerView) {
            return entity;
        }

        // A selected character or mesh is an explicit focus request. Ignore
        // volumes, lights and other editor helpers that happen to be selected.
        Galeshi::Entity selected = m_editor.Selected(&m_scene);
        if (selected) {
            const entt::entity handle = selected.Handle();
            if (reg.all_of<Galeshi::SkeletalComponent>(handle) ||
                reg.all_of<Galeshi::MeshComponent>(handle)) {
                return handle;
            }
        }

        // Otherwise focus the closest skeletal character. This keeps actors
        // sharp when the camera target is not exactly on them.
        const glm::vec3 cameraPos = m_camera.Position();
        const glm::vec3 forward = CameraForward();
        const glm::mat4 viewProj = m_camera.Projection() * m_camera.View();
        float bestDistance = std::numeric_limits<float>::max();
        entt::entity best = entt::null;

        auto characterView =
            reg.view<Galeshi::SkeletalComponent, Galeshi::TransformComponent>();
        for (auto entity : characterView) {
            const glm::mat4 world = EntityWorldMatrix(entity);
            // Root transforms are usually at the feet. Use a rough chest
            // position for visibility/nearest tests so a cropped close-up
            // still selects the character whose feet are below the frame.
            const glm::vec3 approxCenter =
                glm::vec3(world[3]) + glm::vec3(0.0f, 1.0f, 0.0f);
            const float depth = glm::dot(approxCenter - cameraPos, forward);
            if (depth <= 0.05f || depth >= bestDistance) continue;

            // Ignore characters clearly outside the visible frame. Keep a
            // generous margin because the exact AABB centre is computed later.
            const glm::vec4 clip = viewProj * glm::vec4(approxCenter, 1.0f);
            if (clip.w <= 0.0f) continue;
            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            if (std::abs(ndc.x) > 1.5f || std::abs(ndc.y) > 1.5f) continue;

            bestDistance = depth;
            best = entity;
        }
        return best;
    }

    float AutoFocusDistance() {
        const entt::entity target = FindAutoFocusEntity();
        glm::vec3 focusPoint = m_editor.camTarget;

        if (target != entt::null) {
            EntityWorldBoundsCenter(target, focusPoint);
        }

        return ViewSpaceDepth(focusPoint);
    }

    float ResolveDofFocusDistance(const Galeshi::PostProcessSettings& s) {
        return s.dofAutoFocus ? AutoFocusDistance() : s.dofFocusDistance;
    }

    void ApplyEnvironmentPalettePreset(
        Galeshi::PostProcessSettings& settings,
        const EnvironmentPalettePreset& preset) {
        settings.environmentPaletteEnabled = true;
        settings.environmentPaletteAmount = preset.amount;
        settings.environmentPaletteShadow = preset.shadow;
        settings.environmentPaletteMid = preset.mid;
        settings.environmentPaletteHighlight = preset.highlight;
    }

    bool DrawEnvironmentPaletteControls(Galeshi::PostProcessSettings& settings) {
        bool changed = false;
        changed |= ImGui::Checkbox("Enabled##environment_palette",
                                   &settings.environmentPaletteEnabled);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Apply a shadow/midtone/highlight palette to the final\n"
                "environment render while preserving luminance.");
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140.0f);
        changed |= ImGui::SliderFloat("Amount##environment_palette",
                                      &settings.environmentPaletteAmount,
                                      0.0f, 1.0f, "%.2f");

        changed |= ImGui::ColorEdit3(
            "Shadows##environment_palette",
            glm::value_ptr(settings.environmentPaletteShadow));
        changed |= ImGui::ColorEdit3(
            "Midtones##environment_palette",
            glm::value_ptr(settings.environmentPaletteMid));
        changed |= ImGui::ColorEdit3(
            "Highlights##environment_palette",
            glm::value_ptr(settings.environmentPaletteHighlight));

        ImGui::TextDisabled("Presets");
        int index = 0;
        for (const EnvironmentPalettePreset& preset :
             kEnvironmentPalettePresets) {
            if ((index % 4) != 0) ImGui::SameLine();
            if (ImGui::Button(preset.name)) {
                ApplyEnvironmentPalettePreset(settings, preset);
                changed = true;
            }
            ++index;
        }
        return changed;
    }

    void DrawEnvironmentPaletteWindow() {
        if (!m_showEnvironmentPalette) return;
        if (!ImGui::Begin("Environment Palette", &m_showEnvironmentPalette)) {
            ImGui::End();
            return;
        }

        ImGui::TextDisabled(
            "A colour palette applied to the final environment render. "
            "The filter runs in the color-correction pass after tonemapping, "
            "so it does not affect the editor UI.");
        auto* activeVolume = ActivePostProcessVolume();
        ImGui::TextDisabled("Editing: %s",
                            activeVolume ? "active Post-Process Volume"
                                         : "global Post-Process settings");
        ImGui::Separator();

        Galeshi::PostProcessSettings& settings =
            activeVolume ? activeVolume->settings
                         : m_scene.GlobalPostProcess();
        ImGui::PushID(&settings);
        const bool changed = DrawEnvironmentPaletteControls(settings);
        ImGui::PopID();
        if (changed && !activeVolume) m_scene.SetHasGlobalPostProcess(true);

        ImGui::Separator();
        ImGui::TextDisabled(
            "An enabled Post-Process Volume overrides these values for the "
            "entire scene. The highest-priority volume wins.");
        ImGui::End();
    }

    void DrawPostProcessVolumeSettings(Galeshi::PostProcessVolumeComponent& volume) {
        auto& s = volume.settings;

        ImGui::Checkbox("Enabled", &volume.enabled);
        ImGui::DragInt("Priority", &volume.priority, 1.0f, -100, 100);
        ImGui::DragFloat3("Editor Bounds", glm::value_ptr(volume.size),
                          0.1f, 0.1f, 10000.0f);
        ImGui::Checkbox("Visible in Editor", &volume.visibleInEditor);

        if (!volume.enabled) {
            ImGui::TextColored(ImVec4(0.65f, 0.65f, 0.65f, 1.0f),
                               "Status: disabled");
        } else if (ActivePostProcessVolume() == &volume) {
            ImGui::TextColored(ImVec4(0.45f, 0.90f, 0.55f, 1.0f),
                               "Status: active (entire scene)");
        } else {
            ImGui::TextColored(ImVec4(0.95f, 0.78f, 0.35f, 1.0f),
                               "Status: overridden by a higher-priority volume");
        }
        ImGui::TextDisabled(
            "Volumes affect the whole scene; the highest-priority enabled "
            "volume overrides global Post-Process settings. The Post-Process "
            "menu edits the active source. Editor Bounds are only a visual "
            "reference and do not limit the effect.");

        if (ImGui::TreeNode("Bloom")) {
            ImGui::Checkbox("Enabled##bloom", &s.bloomEnabled);
            ImGui::DragFloat("Threshold", &s.bloomThreshold,
                             0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Intensity", &s.bloomIntensity,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Radius", &s.bloomRadius,
                             0.01f, 0.01f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Depth of Field")) {
            ImGui::Checkbox("Enabled##dof", &s.dofEnabled);
            ImGui::Checkbox("Auto Focus (Target)", &s.dofAutoFocus);
            if (ImGui::IsItemHovered() && s.dofAutoFocus) {
                ImGui::SetTooltip("Focus follows the tagged player, the selected\n"
                                  "character/mesh, or the nearest skeletal character.\n"
                                  "Falls back to the camera orbit target.");
            }

            if (s.dofAutoFocus) {
                float autoFocus = ResolveDofFocusDistance(s);
                ImGui::BeginDisabled();
                ImGui::DragFloat("Focus Distance##dof", &autoFocus,
                                 0.1f, 0.05f, 10000.0f, "%.3f");
                ImGui::EndDisabled();
                ImGui::TextDisabled("Auto: %.3f m", autoFocus);
            } else {
                ImGui::DragFloat("Focus Distance##dof", &s.dofFocusDistance,
                                 0.1f, 0.05f, 10000.0f, "%.3f");
            }
            ImGui::DragFloat("Blur (px)", &s.dofBlur,
                             0.1f, 0.0f, 64.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Motion Blur")) {
            ImGui::Checkbox("Enabled##motionblur", &s.motionBlurEnabled);
            ImGui::DragFloat("Amount", &s.motionBlurAmount,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Max Velocity (px)", &s.motionBlurMaxVelocity,
                             0.5f, 0.0f, 1000.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Tonemap")) {
            ImGui::DragFloat("Exposure", &s.exposure, 0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Contrast", &s.contrast, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Saturation", &s.saturation, 0.01f, 0.0f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Color Correction")) {
            ImGui::DragFloat("Exposure##cc", &s.colorExposure,
                             0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Contrast##cc", &s.colorContrast,
                             0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Saturation##cc", &s.colorSaturation,
                             0.01f, 0.0f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Environment Palette")) {
            ImGui::PushID(&s);
            DrawEnvironmentPaletteControls(s);
            ImGui::PopID();
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Vignette")) {
            ImGui::DragFloat("Intensity##vignette", &s.vignetteIntensity,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Smoothness", &s.vignetteSmoothness,
                             0.01f, 0.26f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Chromatic Aberration")) {
            ImGui::DragFloat("Intensity##chroma", &s.chromaticAberration,
                             0.0001f, 0.0f, 0.1f, "%.4f");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Ambient Occlusion")) {
            ImGui::Checkbox("Enabled##ao", &s.ssaoEnabled);
            int aoMode = static_cast<int>(s.aoMode);
            const char* aoModes[] = { "SSAO", "HBAO", "GTAO" };
            if (ImGui::Combo("Method", &aoMode, aoModes, 3)) {
                if (aoMode < 0) aoMode = 0;
                if (aoMode > 2) aoMode = 2;
                s.aoMode = static_cast<Galeshi::AmbientOcclusionMode>(aoMode);
            }

            int aoResolution = s.aoResolutionScale <= 0.375f ? 0 : 1;
            const char* aoResolutions[] = { "Quarter (fast)", "Half (quality)" };
            if (ImGui::Combo("Resolution", &aoResolution, aoResolutions, 2)) {
                s.aoResolutionScale = aoResolution == 0 ? 0.25f : 0.5f;
            }

            if (s.aoMode == Galeshi::AmbientOcclusionMode::SSAO) {
                ImGui::DragFloat("Radius", &s.ssaoRadius,
                                 0.5f, 0.0f, 1000.0f);
                ImGui::DragFloat("Bias", &s.ssaoBias,
                                 0.0001f, 0.0f, 1.0f, "%.4f");
                ImGui::DragFloat("Max Distance", &s.ssaoMaxDistance,
                                 0.1f, 0.0f, 1000.0f);
                ImGui::DragFloat("Strength", &s.ssaoStrength,
                                 0.01f, 0.0f, 10.0f);
            } else {
                ImGui::DragFloat("Radius##ao", &s.aoRadius,
                                 0.01f, 0.01f, 100.0f);
                ImGui::DragFloat("Bias##ao", &s.aoBias,
                                 0.001f, 0.0f, 1.0f, "%.4f");
                if (s.aoMode == Galeshi::AmbientOcclusionMode::GTAO) {
                    ImGui::DragFloat("Thickness##ao", &s.aoThickness,
                                     0.01f, 0.01f, 20.0f);
                }
                ImGui::DragFloat("Max Distance##ao", &s.aoMaxDistance,
                                 0.1f, 0.1f, 10000.0f);
                ImGui::DragFloat("Strength##ao", &s.aoStrength,
                                 0.01f, 0.0f, 10.0f);
                if (s.aoMode == Galeshi::AmbientOcclusionMode::HBAO) {
                    ImGui::SliderInt("Directions", &s.aoDirections, 2, 16);
                    ImGui::SliderInt("Steps per Direction", &s.aoSteps, 1, 12);
                } else {
                    ImGui::SliderInt("Slices", &s.aoDirections, 2, 16);
                    ImGui::SliderInt("Steps per Side", &s.aoSteps, 1, 12);
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("SSR")) {
            ImGui::Checkbox("Enabled##ssr", &s.ssrEnabled);
            ImGui::DragFloat("Intensity##ssr", &s.ssrIntensity,
                             0.01f, 0.0f, 5.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Screen-Space GI")) {
            ImGui::Checkbox("Enabled##ssgi", &s.ssgiEnabled);
            ImGui::DragFloat("Intensity##ssgi", &s.ssgiIntensity,
                             0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Radius##ssgi", &s.ssgiRadius,
                             0.05f, 0.05f, 50.0f, "%.2f m");
            ImGui::DragFloat("Thickness##ssgi", &s.ssgiThickness,
                             0.001f, 0.001f, 1.0f, "%.3f");
            ImGui::SliderInt("Rays##ssgi", &s.ssgiRays, 1, 8);
            ImGui::SliderInt("Steps##ssgi", &s.ssgiSteps, 2, 24);
            ImGui::TextDisabled("Half-resolution diffuse bounce");
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Anti-Aliasing")) {
            ImGui::Checkbox("FXAA", &s.fxaaEnabled);
            ImGui::Checkbox("SMAA", &s.smaaEnabled);
            ImGui::Checkbox("TAA", &s.taaEnabled);
            ImGui::DragFloat("TAA Blend", &s.taaBlend,
                             0.005f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("God Rays")) {
            ImGui::Checkbox("Enabled##godrays", &s.godRaysEnabled);
            ImGui::DragFloat("Intensity##godrays", &s.godRaysIntensity,
                             0.01f, 0.0f, 20.0f);
            ImGui::DragFloat("Decay", &s.godRaysDecay,
                             0.005f, 0.0f, 1.0f);
            ImGui::DragFloat("Density", &s.godRaysDensity,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Weight", &s.godRaysWeight,
                             0.01f, 0.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Fog")) {
            ImGui::Checkbox("Enabled##fog", &s.fogEnabled);
            ImGui::SameLine();
            if (ImGui::Checkbox("Volumetric", &s.fogVolumetric) && s.fogVolumetric) {
                s.fogEnabled = true;
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Ray-march the height fog and sample PSSM shadows\n"
                                  "for directional-light shafts.");
            }
            ImGui::ColorEdit3("Color##fog", glm::value_ptr(s.fogColor));
            ImGui::DragFloat("Density", &s.fogDensity,
                             0.0005f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Height Falloff", &s.fogHeightFalloff,
                             0.001f, 0.0f, 1.0f, "%.4f");
            ImGui::DragFloat("Base Height", &s.fogBaseHeight,
                             0.1f, -1000.0f, 1000.0f);
            ImGui::DragFloat("Start Distance", &s.fogStartDistance,
                             0.5f, 0.0f, 10000.0f);
            ImGui::DragFloat("Max Distance", &s.fogMaxDistance,
                             1.0f, 1.0f, 10000.0f);
            ImGui::DragFloat("Sun Scattering", &s.fogSunScattering,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Anisotropy", &s.fogAnisotropy,
                             0.01f, -0.95f, 0.95f);
            if (s.fogVolumetric) {
                ImGui::SliderInt("Steps", &s.fogSteps, 4, 64);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Atmosphere")) {
            ImGui::Checkbox("Enabled##atmosphere", &s.atmosphereEnabled);
            ImGui::DragFloat("Intensity##atmosphere", &s.atmosphereIntensity,
                             0.1f, 0.0f, 1000.0f);
            ImGui::TreePop();
        }

        ImGui::Checkbox("Invert Colors", &s.invertEnabled);

        if (ImGui::Button("Reset Post-Process Settings")) {
            volume.settings = Galeshi::PostProcessSettings{};
            volume.settings.dofEnabled = true;
        }
    }

    void DrawWaterSettings(Galeshi::WaterComponent& water) {
        ImGui::Checkbox("Enabled##water", &water.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Visible in Editor", &water.visibleInEditor);
        ImGui::DragFloat("Width", &water.width, 0.1f, 0.1f, 10000.0f);
        ImGui::DragFloat("Depth", &water.depth, 0.1f, 0.1f, 10000.0f);
        ImGui::TextDisabled("The water surface is centered on the entity and "
                            "uses its Y position.");

        ImGui::Separator();
        ImGui::Text("Presets");
        if (ImGui::Button("Calm Lake")) {
            PushHistory();
            water.waveHeight = 0.12f;
            water.waveScale = 1.4f;
            water.waveSpeed = 0.55f;
            water.choppiness = 0.15f;
            water.detailStrength = 0.20f;
            water.refractionStrength = 0.28f;
            water.reflectionStrength = 0.82f;
            water.clarity = 0.72f;
            water.absorption = 0.32f;
            water.foamDistance = 0.45f;
            water.foamIntensity = 0.35f;
            water.shallowColor = {0.10f, 0.46f, 0.48f};
            water.deepColor = {0.015f, 0.10f, 0.16f};
            water.transparency = 0.88f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Ocean")) {
            PushHistory();
            water.waveHeight = 0.48f;
            water.waveScale = 0.75f;
            water.waveSpeed = 1.25f;
            water.choppiness = 0.55f;
            water.detailStrength = 0.55f;
            water.refractionStrength = 0.34f;
            water.reflectionStrength = 0.92f;
            water.clarity = 0.60f;
            water.absorption = 0.70f;
            water.foamDistance = 0.85f;
            water.foamIntensity = 0.78f;
            water.shallowColor = {0.04f, 0.24f, 0.34f};
            water.deepColor = {0.004f, 0.025f, 0.085f};
            water.transparency = 0.76f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Tropical")) {
            PushHistory();
            water.waveHeight = 0.22f;
            water.waveScale = 1.15f;
            water.waveSpeed = 0.85f;
            water.choppiness = 0.28f;
            water.detailStrength = 0.38f;
            water.refractionStrength = 0.42f;
            water.reflectionStrength = 0.86f;
            water.clarity = 0.76f;
            water.absorption = 0.36f;
            water.foamDistance = 0.65f;
            water.foamIntensity = 0.62f;
            water.shallowColor = {0.03f, 0.62f, 0.58f};
            water.deepColor = {0.005f, 0.10f, 0.22f};
            water.transparency = 0.84f;
        }

        if (ImGui::TreeNode("Waves")) {
            ImGui::DragFloat("Height", &water.waveHeight,
                             0.005f, 0.0f, 5.0f);
            ImGui::DragFloat("Scale", &water.waveScale,
                             0.01f, 0.05f, 20.0f);
            ImGui::DragFloat("Speed", &water.waveSpeed,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Choppiness", &water.choppiness,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Detail Strength", &water.detailStrength,
                             0.01f, 0.0f, 2.0f);
            ImGui::DragFloat2("Flow Direction", glm::value_ptr(water.flowDirection),
                              0.01f, -10.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Optics")) {
            ImGui::Checkbox("Planar Reflections", &water.planarReflections);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Render a mirrored opaque scene for the nearest\n"
                                  "water plane. Other planes use the environment map.");
            }
            ImGui::DragFloat("Refraction", &water.refractionStrength,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat("Reflection", &water.reflectionStrength,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat("Fresnel Power", &water.fresnelPower,
                             0.05f, 1.0f, 20.0f);
            ImGui::DragFloat("Depth Fade", &water.depthFade,
                             0.05f, 0.05f, 100.0f);
            ImGui::DragFloat("Absorption", &water.absorption,
                             0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Clarity", &water.clarity,
                             0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Transparency", &water.transparency,
                             0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Foam & Sparkle")) {
            ImGui::DragFloat("Shore Distance", &water.foamDistance,
                             0.01f, 0.01f, 20.0f);
            ImGui::DragFloat("Foam Intensity", &water.foamIntensity,
                             0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Sparkle", &water.sparkle,
                             0.01f, 0.0f, 2.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Colors")) {
            ImGui::ColorEdit3("Shallow", glm::value_ptr(water.shallowColor));
            ImGui::ColorEdit3("Deep", glm::value_ptr(water.deepColor));
            ImGui::ColorEdit3("Foam", glm::value_ptr(water.foamColor));
            ImGui::TreePop();
        }

        if (ImGui::Button("Reset Water Settings")) {
            PushHistory();
            water = Galeshi::WaterComponent{};
        }
    }

    void DrawGroomSettings(Galeshi::GroomComponent& groom) {
        ImGui::Checkbox("Enabled##groom", &groom.enabled);
        ImGui::SameLine();
        ImGui::Checkbox("Simulate", &groom.simulate);
        if (groom.runtime) {
            ImGui::SameLine();
            ImGui::TextDisabled("%zu guides / %zu strands",
                                groom.runtime->guides.size(),
                                groom.runtime->strands.size());
        }

        char targetBuffer[128];
        std::snprintf(targetBuffer, sizeof(targetBuffer), "%s",
                      groom.targetMeshName.c_str());
        if (ImGui::InputText("Target Mesh", targetBuffer, sizeof(targetBuffer))) {
            groom.targetMeshName = targetBuffer;
        }
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("Leave empty to use this entity's mesh, or the "
                              "highest descendant mesh of a character.");
        }

        ImGui::Separator();
        ImGui::Text("Presets");
        if (ImGui::Button("Short Fur")) {
            PushHistory();
            groom.guideCount = 160;
            groom.strandsPerGuide = 10;
            groom.segments = 6;
            groom.length = 0.075f;
            groom.width = 0.0035f;
            groom.clumpRadius = 0.008f;
            groom.clumping = 0.78f;
            groom.frizz = 0.025f;
            groom.curl = 0.018f;
            groom.volume = 0.18f;
            groom.scalpStart = 0.56f;
            groom.gravity = 0.18f;
            groom.stiffness = 0.85f;
            groom.damping = 0.32f;
            groom.windStrength = 0.08f;
            groom.rootColor = {0.09f, 0.05f, 0.03f};
            groom.tipColor = {0.25f, 0.14f, 0.075f};
            groom.opacity = 0.95f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Bob")) {
            PushHistory();
            groom.guideCount = 120;
            groom.strandsPerGuide = 14;
            groom.segments = 8;
            groom.length = 0.24f;
            groom.width = 0.0075f;
            groom.clumpRadius = 0.014f;
            groom.clumping = 0.68f;
            groom.frizz = 0.045f;
            groom.curl = 0.035f;
            groom.volume = 0.30f;
            groom.scalpStart = 0.60f;
            groom.gravity = 0.30f;
            groom.stiffness = 0.72f;
            groom.damping = 0.26f;
            groom.windStrength = 0.14f;
            groom.rootColor = {0.095f, 0.052f, 0.032f};
            groom.tipColor = {0.31f, 0.17f, 0.09f};
            groom.opacity = 0.90f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Long")) {
            PushHistory();
            groom.guideCount = 96;
            groom.strandsPerGuide = 12;
            groom.segments = 10;
            groom.length = 0.48f;
            groom.width = 0.008f;
            groom.clumpRadius = 0.018f;
            groom.clumping = 0.52f;
            groom.frizz = 0.065f;
            groom.curl = 0.055f;
            groom.volume = 0.34f;
            groom.scalpStart = 0.58f;
            groom.gravity = 0.42f;
            groom.stiffness = 0.58f;
            groom.damping = 0.22f;
            groom.windStrength = 0.22f;
            groom.rootColor = {0.08f, 0.045f, 0.03f};
            groom.tipColor = {0.36f, 0.20f, 0.11f};
            groom.opacity = 0.88f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Wet Look")) {
            PushHistory();
            groom.guideCount = 110;
            groom.strandsPerGuide = 14;
            groom.segments = 8;
            groom.length = 0.28f;
            groom.width = 0.0045f;
            groom.clumpRadius = 0.010f;
            groom.clumping = 0.90f;
            groom.frizz = 0.012f;
            groom.curl = 0.020f;
            groom.volume = 0.20f;
            groom.scalpStart = 0.58f;
            groom.gravity = 0.50f;
            groom.stiffness = 0.80f;
            groom.damping = 0.36f;
            groom.windStrength = 0.08f;
            groom.specular = 0.95f;
            groom.shininess = 80.0f;
            groom.ambient = 0.24f;
            groom.opacity = 0.82f;
            groom.rootColor = {0.055f, 0.03f, 0.022f};
            groom.tipColor = {0.19f, 0.10f, 0.06f};
        }

        if (ImGui::TreeNode("Groom")) {
            ImGui::DragInt("Guides", &groom.guideCount, 1.0f, 1, 4096);
            ImGui::DragInt("Strands / Guide", &groom.strandsPerGuide,
                           1.0f, 1, 64);
            ImGui::DragInt("Segments", &groom.segments, 0.2f, 2, 32);
            ImGui::DragFloat("Length", &groom.length, 0.002f, 0.001f, 5.0f);
            ImGui::DragFloat("Width", &groom.width, 0.0001f, 0.0001f, 0.1f);
            ImGui::DragFloat("Root Width", &groom.rootWidth, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Tip Width", &groom.tipWidth, 0.005f, 0.0f, 2.0f);
            ImGui::DragFloat("Clump Radius", &groom.clumpRadius,
                             0.0005f, 0.0f, 0.2f);
            ImGui::DragFloat("Clumping", &groom.clumping, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Frizz", &groom.frizz, 0.002f, 0.0f, 0.5f);
            ImGui::DragFloat("Curl", &groom.curl, 0.002f, 0.0f, 0.5f);
            ImGui::DragFloat("Volume", &groom.volume, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloatRange2("Scalp Band", &groom.scalpStart,
                                   &groom.scalpEnd, 0.005f, 0.0f, 1.0f);
            ImGui::DragFloat("Normal Threshold", &groom.normalThreshold,
                             0.01f, -1.0f, 1.0f);
            ImGui::DragFloat3("Groom Direction",
                              glm::value_ptr(groom.groomDirection), 0.01f);
            ImGui::InputScalar("Seed", ImGuiDataType_U32, &groom.seed);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Dynamics")) {
            ImGui::DragFloat("Gravity", &groom.gravity, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Stiffness", &groom.stiffness,
                             0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Damping", &groom.damping, 0.01f, 0.0f, 5.0f);
            ImGui::DragFloat("Wind Strength", &groom.windStrength,
                             0.005f, 0.0f, 2.0f);
            ImGui::DragFloat3("Wind Direction",
                              glm::value_ptr(groom.windDirection), 0.01f);
            ImGui::DragFloat("Wind Frequency", &groom.windFrequency,
                             0.01f, 0.0f, 10.0f);
            ImGui::TreePop();
        }

        if (ImGui::TreeNode("Shading")) {
            ImGui::ColorEdit3("Root Color", glm::value_ptr(groom.rootColor));
            ImGui::ColorEdit3("Tip Color", glm::value_ptr(groom.tipColor));
            ImGui::DragFloat("Specular", &groom.specular, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Shininess", &groom.shininess,
                             0.5f, 1.0f, 256.0f);
            ImGui::DragFloat("Ambient", &groom.ambient, 0.01f, 0.0f, 2.0f);
            ImGui::DragFloat("Opacity", &groom.opacity, 0.01f, 0.0f, 1.0f);
            ImGui::DragFloat("Random Tint", &groom.randomTint,
                             0.01f, 0.0f, 1.0f);
            ImGui::TreePop();
        }

        if (ImGui::Button("Rebuild Groom")) {
            groom.runtime.reset();
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset Groom Settings")) {
            PushHistory();
            auto target = groom.targetMeshName;
            groom = Galeshi::GroomComponent{};
            groom.targetMeshName = target;
        }
    }

    // -------------------------------------------------------------------
    // Audio
    // -------------------------------------------------------------------
    void UpdateAudio(float dt) {
        if (!m_audio.IsInitialized()) return;

        // Mixer values live on the scene so they are serialized with it.
        m_audio.ApplyMixerSettings(m_scene.AudioBuses());
        m_audio.PruneSources(m_scene.Registry());

        // Listener: a primary AudioListenerComponent wins; otherwise the
        // editor/game camera acts as the listener.
        glm::vec3 listenerPos = m_camera.Position();
        glm::vec3 listenerForward = -glm::vec3(m_camera.View()[0][2],
                                               m_camera.View()[1][2],
                                               m_camera.View()[2][2]);
        glm::vec3 listenerUp = glm::vec3(m_camera.View()[0][1],
                                         m_camera.View()[1][1],
                                         m_camera.View()[2][1]);

        auto listenerView = m_scene.Registry().view<Galeshi::AudioListenerComponent,
                                                     Galeshi::TransformComponent>();
        for (auto e : listenerView) {
            const auto& listener =
                listenerView.get<Galeshi::AudioListenerComponent>(e);
            if (!listener.primary) continue;

            const glm::mat4 world = EntityWorldMatrix(e);
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(world, scale, rotation, translation, skew, perspective);
            listenerPos = translation;
            listenerForward = glm::normalize(rotation * glm::vec3(0.0f, 0.0f, -1.0f));
            listenerUp = glm::normalize(rotation * glm::vec3(0.0f, 1.0f, 0.0f));
            break;
        }
        m_audio.SetListener(listenerPos, listenerForward, listenerUp, dt);

        auto view = m_scene.Registry().view<Galeshi::AudioSourceComponent>();
        for (auto e : view) {
            auto& source = view.get<Galeshi::AudioSourceComponent>(e);

            glm::vec3 position(0.0f);
            if (m_scene.Registry().all_of<Galeshi::TransformComponent>(e)) {
                const glm::mat4 world = EntityWorldMatrix(e);
                glm::vec3 scale, translation, skew;
                glm::vec4 perspective;
                glm::quat rotation;
                glm::decompose(world, scale, rotation, translation, skew, perspective);
                position = translation + rotation * source.offset;
            }

            const bool playing = m_audio.IsSourcePlaying(e);
            if (source.autoplay && !playing && !source.soundPath.empty()) {
                m_audio.PlaySource(e, source, position);
            } else if (playing) {
                m_audio.UpdateSource(e, source, position, dt);
            }
            source.playing = m_audio.IsSourcePlaying(e);
        }
    }

    void PaintVegetationAt(Galeshi::Entity landscapeEntity,
                           const Galeshi::LandscapeComponent& landscape,
                           const glm::vec3& localPosition,
                           bool paintGrass) {
        if (!m_assets || !landscapeEntity ||
            !landscapeEntity.HasComponent<Galeshi::VegetationComponent>()) {
            return;
        }
        auto& vegetation =
            landscapeEntity.GetComponent<Galeshi::VegetationComponent>();
        if (!vegetation.enabled || vegetation.paintDensity <= 0.0f) return;

        const float radius = std::max(0.05f, m_editor.brushRadius);
        const float halfX = landscape.sizeX * 0.5f * landscape.scale;
        const float halfZ = landscape.sizeZ * 0.5f * landscape.scale;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        std::uniform_real_distribution<float> angle(0.0f, 6.2831853f);

        Galeshi::VegetationTreeType* treeType = nullptr;
        if (!paintGrass && m_paintTreeType >= 0 &&
            m_paintTreeType < static_cast<int>(vegetation.treeTypes.size())) {
            treeType = &vegetation.treeTypes[
                static_cast<size_t>(m_paintTreeType)];
        }

        const int stamps = std::clamp(
            static_cast<int>(std::round(vegetation.paintDensity * 6.0f)),
            0, 96);
        if (stamps == 0) return;

        auto& painted = paintGrass
            ? vegetation.paintedGrassInstances
            : (treeType ? treeType->paintedInstances
                        : vegetation.paintedTreeInstances);
        painted.reserve(painted.size() + static_cast<size_t>(stamps));

        for (int i = 0; i < stamps; ++i) {
            const float a = angle(m_paintRng);
            const float r = std::sqrt(unit(m_paintRng)) * radius;
            const float x = localPosition.x + std::cos(a) * r;
            const float z = localPosition.z + std::sin(a) * r;
            if (std::abs(x) > halfX || std::abs(z) > halfZ) continue;

            const float y = Galeshi::LandscapeMesh::SampleHeight(
                landscape, x, z);
            const float baseSize = paintGrass
                ? vegetation.paintSize
                : vegetation.paintSize * (treeType ? treeType->scale : 1.0f);
            const float scale = baseSize * (0.82f + unit(m_paintRng) * 0.36f);
            if (scale <= 0.0f) continue;
            const float yaw = angle(m_paintRng);

            glm::mat4 transform = glm::translate(
                glm::mat4(1.0f), glm::vec3(x, y, z));
            transform = glm::rotate(transform, yaw,
                                    glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform, glm::vec3(scale));
            painted.push_back(transform);
        }
    }

    void DrawCollisionShapeGizmos() {
        if (!m_debug) return;
        Galeshi::Entity selected = m_editor.Selected(&m_scene);
        auto colliderView =
            m_scene.Registry().view<Galeshi::ColliderComponent>();
        for (auto e : colliderView) {
            const auto& collider =
                colliderView.get<Galeshi::ColliderComponent>(e);
            if (!collider.visibleInEditor) continue;

            const bool isSelected = selected && selected.Handle() == e;
            const glm::vec4 color = isSelected
                ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
                : (collider.isTrigger
                       ? glm::vec4(0.30f, 0.60f, 1.00f, 0.90f)
                       : glm::vec4(0.20f, 1.00f, 0.40f, 0.75f));

            const glm::mat4 world = EntityWorldMatrix(e);
            auto toWorld = [&world](const glm::vec3& p) {
                return glm::vec3(world * glm::vec4(p, 1.0f));
            };
            const glm::vec3 c = collider.offset;

            switch (collider.shapeType) {
                case Galeshi::ColliderShapeType::Box: {
                    const glm::vec3 h = collider.halfExtents;
                    glm::vec3 p[8] = {
                        toWorld(c + glm::vec3(-h.x, -h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x, -h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x, -h.y,  h.z)),
                        toWorld(c + glm::vec3(-h.x, -h.y,  h.z)),
                        toWorld(c + glm::vec3(-h.x,  h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x,  h.y, -h.z)),
                        toWorld(c + glm::vec3( h.x,  h.y,  h.z)),
                        toWorld(c + glm::vec3(-h.x,  h.y,  h.z)),
                    };
                    auto edge = [&](int a, int b) {
                        m_debug->AddLine(p[a], p[b], color);
                    };
                    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
                    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
                    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
                    break;
                }
                case Galeshi::ColliderShapeType::Sphere: {
                    const int segments = 32;
                    const float r = collider.radius;
                    for (int i = 0; i < segments; ++i) {
                        const float a1 = static_cast<float>(i) / segments * 6.2831853f;
                        const float a2 = static_cast<float>(i + 1) / segments * 6.2831853f;
                        m_debug->AddLine(
                            toWorld(c + glm::vec3(std::cos(a1) * r, std::sin(a1) * r, 0.0f)),
                            toWorld(c + glm::vec3(std::cos(a2) * r, std::sin(a2) * r, 0.0f)),
                            color);
                        m_debug->AddLine(
                            toWorld(c + glm::vec3(std::cos(a1) * r, 0.0f, std::sin(a1) * r)),
                            toWorld(c + glm::vec3(std::cos(a2) * r, 0.0f, std::sin(a2) * r)),
                            color);
                        m_debug->AddLine(
                            toWorld(c + glm::vec3(0.0f, std::cos(a1) * r, std::sin(a1) * r)),
                            toWorld(c + glm::vec3(0.0f, std::cos(a2) * r, std::sin(a2) * r)),
                            color);
                    }
                    break;
                }
                case Galeshi::ColliderShapeType::Capsule: {
                    const int segments = 24;
                    const float r = collider.radius;
                    const float hh = collider.halfHeight;
                    for (int ring = 0; ring < 2; ++ring) {
                        const float y = c.y + (ring == 0 ? -hh : hh);
                        for (int i = 0; i < segments; ++i) {
                            const float a1 = static_cast<float>(i) / segments * 6.2831853f;
                            const float a2 = static_cast<float>(i + 1) / segments * 6.2831853f;
                            m_debug->AddLine(
                                toWorld({c.x + std::cos(a1) * r, y, c.z + std::sin(a1) * r}),
                                toWorld({c.x + std::cos(a2) * r, y, c.z + std::sin(a2) * r}),
                                color);
                        }
                    }
                    for (int i = 0; i < 4; ++i) {
                        const float a = static_cast<float>(i) * 1.5707963f;
                        m_debug->AddLine(
                            toWorld({c.x + std::cos(a) * r, c.y - hh, c.z + std::sin(a) * r}),
                            toWorld({c.x + std::cos(a) * r, c.y + hh, c.z + std::sin(a) * r}),
                            color);
                    }
                    break;
                }
            }
        }
    }

    // -------------------------------------------------------------------
    // Viewport & Picking & Sculpting
    // -------------------------------------------------------------------
    void DrawViewport() {
        spdlog::trace("[Main] DrawViewport");
        m_uiOverlayHovered = false;
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
        if (m_isGameMode) {
            ImGui::SetNextWindowPos(ImGui::GetMainViewport()->Pos);
            ImGui::SetNextWindowSize(ImGui::GetMainViewport()->Size);
            ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings);
            m_viewportHovered = true;
        } else {
            ImGui::Begin("Viewport");
            m_viewportHovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        }
        ImGui::PopStyleVar();

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        const uint16_t w = static_cast<uint16_t>(avail.x);
        const uint16_t h = static_cast<uint16_t>(avail.y);

        // The UI/viewport stays at native size; the complete scene and
        // post-process chain render at this scaled internal resolution.
        const float activeRenderScale = std::clamp(
            m_dynamicResolution ? m_dynamicScale : m_renderScale, 0.5f, 1.0f);
        const uint16_t renderW = static_cast<uint16_t>(std::max(
            2, static_cast<int>(std::lround(static_cast<float>(w) * activeRenderScale))));
        const uint16_t renderH = static_cast<uint16_t>(std::max(
            2, static_cast<int>(std::lround(static_cast<float>(h) * activeRenderScale))));

        // Realtime Rendering off keeps the last completed viewport image on
        // screen and skips all GPU scene/post-process work. The game needs at
        // least one frame after enabling it, so an invalid cache always
        // renders once even while the editor is paused.
        const bool renderThisFrame =
            m_realtimeRendering || m_isGameMode ||
            !bgfx::isValid(m_lastViewportTexture);

        if (renderThisFrame && w > 0 && h > 0) {
            if (!m_framebuffer || m_framebuffer->Width() != renderW ||
                m_framebuffer->Height() != renderH) {
                // Recreating an FBO that bgfx still considers bound can make it
                // skip the image-layout transition on the next bind (the new
                // handle may reuse the old index). Touch the backbuffer first
                // so the next view always binds a different framebuffer.
                bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
                bgfx::setViewRect(0, 0, 0, 1, 1);
                bgfx::setViewClear(0, BGFX_CLEAR_NONE);
                bgfx::touch(0);

                if (!m_framebuffer) m_framebuffer = std::make_unique<Galeshi::Framebuffer>();
                m_framebuffer->Resize(renderW, renderH);
            }
        }

        m_viewportW = w;
        m_viewportH = h;

        float postFxAccumMs = 0.0f;
        ProfileClock::time_point postFxSegmentStart{};
        bool postFxProfiling = false;

        if (renderThisFrame && m_framebuffer && m_framebuffer->Valid()) {
            
            const auto pssmStart = ProfileClock::now();
            // --- PSSM SHADOW PASS ---
            auto lview = m_scene.Registry().view<Galeshi::DirectionalLightComponent>();
            glm::mat4 lightMtx0, lightMtx1, lightMtx2;
            float cascadeSplits[4] = {15.0f, 40.0f, 100.0f, 0.0f}; // Fallback
            float cascadeNormalOffsets[4] = {0.0f, 0.0f, 0.0f, 0.0f};
            float cascadeDepthBias[4] = {0.0f, 0.0f, 0.0f, 0.0f};

            if (!lview.empty()) {
                auto lh = *lview.begin();
                auto& dl = lview.get<Galeshi::DirectionalLightComponent>(lh);
                glm::vec3 lightDir = glm::normalize(dl.direction);

                // 1. Calculate Cascade Splits (Practical Split Scheme).
                // Follow the adaptive camera clip range so shadows do not
                // disappear or become unstable at large orbit distances.
                float nearPlane = std::max(m_camera.NearPlane(), 0.05f);
                float farPlane = std::max(m_camera.FarPlane(), 1000.0f);
                float lambda = 0.5f;
                float splits[3];
                for (int i = 0; i < 3; ++i) {
                    float p = (i + 1) / 3.0f;
                    float logSplit = nearPlane * std::pow(farPlane / nearPlane, p);
                    float uniformSplit = nearPlane + (farPlane - nearPlane) * p;
                    splits[i] = lambda * logSplit + (1.0f - lambda) * uniformSplit;
                }
                cascadeSplits[0] = splits[0];
                cascadeSplits[1] = splits[1];
                cascadeSplits[2] = splits[2];

                // 2. Extract Frustum Corners
                glm::vec3 camPos = m_camera.Position();
                glm::mat4 invViewProj = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec3 rays[4];
                for (int i = 0; i < 4; ++i) {
                    float x = (i == 0 || i == 3) ? -1.0f : 1.0f;
                    float y = (i < 2) ? -1.0f : 1.0f;
                    glm::vec4 pt = invViewProj * glm::vec4(x, y, 1.0f, 1.0f);
                    glm::vec3 farWorld = glm::vec3(pt) / pt.w;
                    rays[i] = glm::normalize(farWorld - camPos);
                }

                // 3. Render Cascades
                for (int i = 0; i < 3; ++i) {
                    float prevSplit = (i == 0) ? nearPlane : splits[i-1];
                    float currSplit = splits[i];

                    glm::vec3 corners[8];
                    for (int j = 0; j < 4; ++j) {
                        corners[j] = camPos + rays[j] * prevSplit;
                        corners[j+4] = camPos + rays[j] * currSplit;
                    }

                    glm::vec3 center = glm::vec3(0.0f);
                    for (int j = 0; j < 8; ++j) center += corners[j];
                    center /= 8.0f;

                    glm::vec3 eye = center - lightDir * (farPlane * 0.5f);
                    glm::vec3 up = (std::abs(glm::dot(lightDir, glm::vec3(0,1,0))) > 0.99f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                    glm::mat4 lightView = glm::lookAt(eye, center, up);

                    float minX = std::numeric_limits<float>::max();
                    float maxX = std::numeric_limits<float>::lowest();
                    float minY = std::numeric_limits<float>::max();
                    float maxY = std::numeric_limits<float>::lowest();
                    float minZ = std::numeric_limits<float>::max();
                    float maxZ = std::numeric_limits<float>::lowest();

                    for (int j = 0; j < 8; ++j) {
                        glm::vec4 trf = lightView * glm::vec4(corners[j], 1.0f);
                        minX = std::min(minX, trf.x); maxX = std::max(maxX, trf.x);
                        minY = std::min(minY, trf.y); maxY = std::max(maxY, trf.y);
                        minZ = std::min(minZ, trf.z); maxZ = std::max(maxZ, trf.z);
                    }

                    // Snap the light-space bounds to the shadow-map texel
                    // grid. Without this, small camera movements shift every
                    // cascade by a sub-texel amount and distant shadows appear
                    // to flicker/shimmer.
                    const float extentX = std::max(maxX - minX, 1e-3f);
                    const float extentY = std::max(maxY - minY, 1e-3f);
                    const float extent = std::max(extentX, extentY);
                    const float texelWorld = extent / 2048.0f;
                    const float centerX = std::floor(
                        ((minX + maxX) * 0.5f) / texelWorld) * texelWorld;
                    const float centerY = std::floor(
                        ((minY + maxY) * 0.5f) / texelWorld) * texelWorld;
                    minX = centerX - extent * 0.5f;
                    maxX = centerX + extent * 0.5f;
                    minY = centerY - extent * 0.5f;
                    maxY = centerY + extent * 0.5f;
                    const float depthRange =
                        std::max((-minZ + 10.0f) - (-maxZ - 10.0f), 1e-3f);
                    const float ndcDepthScale = bgfx::getCaps()->homogeneousDepth
                        ? 2.0f : 1.0f;
                    cascadeNormalOffsets[i] = texelWorld * m_pssmNormalOffsetTexels;
                    cascadeDepthBias[i] =
                        (texelWorld * m_pssmDepthBiasTexels / depthRange) *
                        ndcDepthScale;

                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), minX, maxX, minY, maxY,
                                  -maxZ - 10.0f, -minZ + 10.0f, 0.0f,
                                  bgfx::getCaps()->homogeneousDepth,
                                  bx::Handedness::Right);
                    
                    const glm::mat4 lightMtx = lightProj * lightView;
                    if (i == 0) lightMtx0 = lightMtx;
                    else if (i == 1) lightMtx1 = lightMtx;
                    else lightMtx2 = lightMtx;

                    m_render->RenderShadowMap(m_scene, lightMtx, 2 + i, m_shadowFbos[i]);
                }
            } else {
                // Fallback without a directional light.
                cascadeSplits[0] = 15.0f; cascadeSplits[1] = 40.0f; cascadeSplits[2] = 100.0f;
                for (int i = 0; i < 3; ++i) {
                    glm::mat4 lightView = glm::lookAt(glm::vec3(0,20,0), glm::vec3(0), glm::vec3(0,0,1));
                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), -15.0f, 15.0f, -15.0f, 15.0f,
                                  0.1f, 50.0f, 0.0f,
                                  bgfx::getCaps()->homogeneousDepth,
                                  bx::Handedness::Right);
                    const float texelWorld = 30.0f / 2048.0f;
                    const float depthRange = 49.9f;
                    const float ndcDepthScale = bgfx::getCaps()->homogeneousDepth
                        ? 2.0f : 1.0f;
                    cascadeNormalOffsets[i] = texelWorld * m_pssmNormalOffsetTexels;
                    cascadeDepthBias[i] =
                        (texelWorld * m_pssmDepthBiasTexels / depthRange) *
                        ndcDepthScale;
                    const glm::mat4 lightMtx = lightProj * lightView;
                    if (i == 0) lightMtx0 = lightMtx;
                    else if (i == 1) lightMtx1 = lightMtx;
                    else lightMtx2 = lightMtx;
                    m_render->RenderShadowMap(m_scene, lightMtx, 2 + i, m_shadowFbos[i]);
                }
            }
            m_cpuProfile.pssmMs = ProfileElapsedMs(pssmStart, ProfileClock::now());

            // --- POINT LIGHT SHADOW PASS ---
            const auto pointShadowStart = ProfileClock::now();
            auto plview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
            if (plview.begin() != plview.end()) {
                auto firstLight = *plview.begin();
                auto& tc = plview.get<Galeshi::TransformComponent>(firstLight);

                glm::vec3 lightPos = tc.position;

                glm::mat4 proj;
                bx::mtxProj(glm::value_ptr(proj), 90.0f, 1.0f, 0.1f, 50.0f, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
                glm::mat4 views[6] = {
                    glm::lookAt(lightPos, lightPos + glm::vec3( 1, 0, 0), glm::vec3(0,-1,0)),
                    glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0), glm::vec3(0,-1,0)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0, 1, 0), glm::vec3(0, 0,1)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0,-1, 0), glm::vec3(0, 0,-1)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0, 1), glm::vec3(0,-1,0)),
                    glm::lookAt(lightPos, lightPos + glm::vec3( 0, 0,-1), glm::vec3(0,-1,0))
                };
                
                for (int i = 0; i < 6; ++i) {
                    m_render->RenderPointShadowMap(m_scene, proj * views[i], lightPos, 5 + i, m_postProcess.PointShadowFbo(i));
                }
            }
            m_cpuProfile.pointShadowsMs = ProfileElapsedMs(pointShadowStart, ProfileClock::now());

            // --- SPOT LIGHT SHADOW PASS ---
            const auto spotShadowStart = ProfileClock::now();
            auto slview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::SpotLightComponent>();
            int slIdx = 0;
            glm::mat4 spotLightVPs[4];
            for (auto e : slview) {
                if (slIdx >= 4) break;
                auto& tc = slview.get<Galeshi::TransformComponent>(e);
                auto& sl = slview.get<Galeshi::SpotLightComponent>(e);
                glm::vec3 lightPos = tc.position;
                glm::vec3 dir = glm::normalize(sl.direction);
                
                glm::mat4 lightView = glm::lookAt(lightPos, lightPos + dir, glm::vec3(0,1,0));
                glm::mat4 lightProj;
                float fov = glm::radians(sl.outerConeAngle * 2.0f);
                bx::mtxProj(glm::value_ptr(lightProj), fov, 1.0f, 0.1f, 50.0f, bgfx::getCaps()->homogeneousDepth, bx::Handedness::Right);
                spotLightVPs[slIdx] = lightProj * lightView;
                
                m_render->RenderShadowMap(m_scene, spotLightVPs[slIdx], 11 + slIdx, m_postProcess.SpotShadowFbo(slIdx));
                slIdx++;
            }
            m_cpuProfile.spotShadowsMs = ProfileElapsedMs(spotShadowStart, ProfileClock::now());

            const Galeshi::PostProcessSettings pp =
                ResolvePostProcessSettings();

            // Do not carry stale history across a period where TAA was off.
            if (!pp.taaEnabled) m_postProcess.ResetTaaHistory();

            // Reprojection is done in the unjittered screen space. The
            // scene itself uses the jittered projection, but history samples
            // must land on the same pixel grid; otherwise edges acquire dark
            // one-pixel ghosts from the previous jitter offset.
            const glm::mat4 taaVP = m_camera.Projection() * m_camera.View();
            const glm::mat4 taaInvVP = glm::inverse(taaVP);

            // TAA needs a fresh sub-pixel projection offset every frame. The
            // offset is applied only to the scene camera; editor picking,
            // gizmos and auto-focus keep using the unjittered camera.
            Galeshi::Camera renderCamera = m_camera;
            if (m_render->DeferredEnabled() && pp.taaEnabled) {
                auto halton = [](uint32_t index, uint32_t base) {
                    float f = 1.0f;
                    float r = 0.0f;
                    while (index > 0) {
                        f /= static_cast<float>(base);
                        r += f * static_cast<float>(index % base);
                        index /= base;
                    }
                    return r;
                };
                const uint32_t jitterIndex =
                    static_cast<uint32_t>(m_renderFrameCounter % 8) + 1u;
                const float jx = halton(jitterIndex, 2u) - 0.5f;
                const float jy = halton(jitterIndex, 3u) - 0.5f;
                glm::mat4 jitteredProj = m_camera.Projection();
                jitteredProj[2][0] += (2.0f * jx) / static_cast<float>(w);
                jitteredProj[2][1] += (2.0f * jy) / static_cast<float>(h);
                renderCamera.SetProjection(jitteredProj);
            }

            // View 0 is a small depth-only occluder pass. Hardware queries
            // are read on the next frame, so this must run before the scene
            // view without stalling the GPU.
            const auto occlusionStart = ProfileClock::now();
            m_render->RenderOcclusion(m_scene, renderCamera, 0, renderW, renderH);
            m_cpuProfile.occlusionMs = ProfileElapsedMs(occlusionStart, ProfileClock::now());

            // Object motion vectors must use the same unjittered history
            // matrix that TAA uses, otherwise jitter is baked into velocity.
            const glm::mat4 motionPrevVP = m_postProcess.HasPrevViewProj()
                ? m_postProcess.GetPrevViewProj() : taaVP;
            m_render->SetPreviousViewProj(motionPrevVP);

            bgfx::setViewFrameBuffer(1, m_framebuffer->Handle());
            const auto gbufferStart = ProfileClock::now();
            m_render->Render(m_scene, renderCamera, 1, renderW, renderH);
            m_cpuProfile.gbufferMs = ProfileElapsedMs(gbufferStart, ProfileClock::now());

            // Mirrored opaque scene for the nearest water plane. The water
            // pass samples this HDR target at the same screen UV as the
            // visible surface. Half resolution is sufficient for a rippled
            // reflection and keeps the extra scene pass affordable.
            const auto planarStart = ProfileClock::now();
            {
                const uint16_t reflectionW = static_cast<uint16_t>(
                    std::max(2, static_cast<int>(renderW / 2)));
                const uint16_t reflectionH = static_cast<uint16_t>(
                    std::max(2, static_cast<int>(renderH / 2)));
                m_render->RenderPlanarReflection(m_scene, m_camera, 19,
                                                 reflectionW, reflectionH);
            }
            m_cpuProfile.planarReflectionMs = ProfileElapsedMs(planarStart,
                                                               ProfileClock::now());

            postFxSegmentStart = ProfileClock::now();
            postFxProfiling = true;

            // Water lighting uses the same directional light as the deferred
            // pass. Keep a copy outside the deferred block for the water pass
            // that runs after TAA.
            float waterSunDir[4] = {-0.4f, -0.8f, -0.5f, 0.0f};
            float waterSunColor[4] = {1.0f, 1.0f, 1.0f, 1.2f};

            if (m_render->DeferredEnabled()) {
                glm::vec3 ldir{-0.4f,-0.8f,-0.5f}, lcol{1.0f};
                float lint = 1.2f;
                auto lview = m_scene.Registry().view<Galeshi::DirectionalLightComponent>();
                if (!lview.empty()) {
                    auto h = *lview.begin();
                    ldir = lview.get<Galeshi::DirectionalLightComponent>(h).direction;
                    lcol = lview.get<Galeshi::DirectionalLightComponent>(h).color;
                    lint = lview.get<Galeshi::DirectionalLightComponent>(h).intensity;
                }
                waterSunDir[0] = -ldir.x;
                waterSunDir[1] = -ldir.y;
                waterSunDir[2] = -ldir.z;
                waterSunDir[3] = 0.0f;
                waterSunColor[0] = lcol.r;
                waterSunColor[1] = lcol.g;
                waterSunColor[2] = lcol.b;
                waterSunColor[3] = lint;
                glm::vec3 cp = m_camera.Position();
                float camPos[4] = {cp.x, cp.y, cp.z, 1.0f};
                float lDirA[4]  = {ldir.x, ldir.y, ldir.z, 0.0f};
                float lColA[4]  = {lcol.r, lcol.g, lcol.b, lint};

                // Rebuild the sky IBL when the sun direction changes. Views
                // 190/200+ are sorted after the lighting pass, so the current
                // frame uses the previous (already valid) cubemaps.
                {
                    glm::vec3 sunWorld = glm::normalize(-ldir);
                    if (!m_iblGenerated || glm::distance(sunWorld, m_lastIblSun) > 0.02f) {
                        m_postProcess.GenerateIBL(lDirA);
                        m_lastIblSun = sunWorld;
                        m_iblGenerated = true;
                    }
                }
                
                float plPosA[64][4] = {};
                float plColA[64][4] = {};
                int plCount = 0;
                auto plview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
                for (auto h : plview) {
                    if (plCount >= 64) break;
                    auto& tc = plview.get<Galeshi::TransformComponent>(h);
                    auto& pl = plview.get<Galeshi::PointLightComponent>(h);
                    plPosA[plCount][0] = tc.position.x;
                    plPosA[plCount][1] = tc.position.y;
                    plPosA[plCount][2] = tc.position.z;
                    plPosA[plCount][3] = pl.radius;
                    plColA[plCount][0] = pl.color.r;
                    plColA[plCount][1] = pl.color.g;
                    plColA[plCount][2] = pl.color.b;
                    plColA[plCount][3] = pl.intensity;
                    plCount++;
                }

                float slPos[4][4] = {};
                float slDir[4][4] = {};
                float slCol[4][4] = {};
                int slCount = 0;
                auto slview = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::SpotLightComponent>();
                for (auto h : slview) {
                    if (slCount >= 4) break;
                    auto& tc = slview.get<Galeshi::TransformComponent>(h);
                    auto& sl = slview.get<Galeshi::SpotLightComponent>(h);
                    slPos[slCount][0] = tc.position.x;
                    slPos[slCount][1] = tc.position.y;
                    slPos[slCount][2] = tc.position.z;
                    slPos[slCount][3] = sl.radius;
                    glm::vec3 dir = glm::normalize(sl.direction);
                    slDir[slCount][0] = dir.x;
                    slDir[slCount][1] = dir.y;
                    slDir[slCount][2] = dir.z;
                    slDir[slCount][3] = cos(glm::radians(sl.innerConeAngle));
                    slCol[slCount][0] = sl.color.r;
                    slCol[slCount][1] = sl.color.g;
                    slCol[slCount][2] = sl.color.b;
                    slCol[slCount][3] = sl.intensity;
                    slCount++;
                }

                glm::mat4 proj = renderCamera.Projection();
                glm::mat4 invProj = glm::inverse(proj);
                glm::mat4 invViewProj = glm::inverse(proj * m_camera.View());
                glm::mat4 currVP = proj * m_camera.View(); // True VP matrix for post-processing
                

                // Pass spot shadow matrices
                static float spotVPs[16] = {0};
                for(int i=0; i<4 && i<slCount; ++i) {
                    float* p = glm::value_ptr(spotLightVPs[i]);
                    for(int j=0; j<4; ++j) spotVPs[i*4 + j] = p[j];
                }
                // We will need to pass this to RenderLighting, but for now, we just set it as a uniform directly.
                // The binding happens inside RenderLighting, but since we need the matrices, we'll add a setUniform here (hacky but works).
                // Actually, PostProcessManager doesn't have u_spotLightVP exposed. We'll do it in RenderLighting.
                // Since we pass slVP to it? No, we can just set it in PostProcessManager::RenderLighting using the data from a static array.

                {
                    if (slCount > 0) bgfx::setUniform(m_postProcess.u_spotLightVP0, glm::value_ptr(spotLightVPs[0]));
                    if (slCount > 1) bgfx::setUniform(m_postProcess.u_spotLightVP1, glm::value_ptr(spotLightVPs[1]));
                    if (slCount > 2) bgfx::setUniform(m_postProcess.u_spotLightVP2, glm::value_ptr(spotLightVPs[2]));
                    if (slCount > 3) bgfx::setUniform(m_postProcess.u_spotLightVP3, glm::value_ptr(spotLightVPs[3]));
                }
                // Ambient occlusion must be resolved before lighting so the
                // result can modulate ambient/IBL and not replace final color.
                Galeshi::AmbientOcclusionParams ao;
                ao.enabled = pp.ssaoEnabled;
                ao.mode = static_cast<int>(pp.aoMode);
                if (pp.aoMode == Galeshi::AmbientOcclusionMode::SSAO) {
                    ao.radius      = pp.ssaoRadius;
                    ao.bias        = pp.ssaoBias;
                    ao.maxDistance = pp.ssaoMaxDistance;
                    ao.strength    = pp.ssaoStrength;
                } else {
                    ao.radius      = pp.aoRadius;
                    ao.bias        = pp.aoBias;
                    ao.thickness   = pp.aoThickness;
                    ao.maxDistance = pp.aoMaxDistance;
                    ao.strength    = pp.aoStrength;
                    ao.directions  = pp.aoDirections;
                    ao.steps       = pp.aoSteps;
                }
                ao.resolutionScale = pp.aoResolutionScale;
                m_postProcess.RenderAmbientOcclusion(
                    m_framebuffer->WorldPos(), m_framebuffer->Normal(),
                    m_camera.View(), proj, 15, renderW, renderH, ao);

                m_postProcess.RenderLightCulling(plCount, plPosA, currVP, renderW, renderH);
                const float pssmParams[4] = {
                    m_pssmSlopeBias, PssmPcfRadius(),
                    m_pssmNormalOffsetSlope,
                    bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f
                };
                const float fogDensity = pp.fogEnabled ? std::max(pp.fogDensity, 0.0f) : 0.0f;
                const float fogColorDensity[4] = {
                    pp.fogColor.r, pp.fogColor.g, pp.fogColor.b, fogDensity
                };
                const float fogParams[4] = {
                    pp.fogHeightFalloff, pp.fogBaseHeight,
                    pp.fogStartDistance, pp.fogMaxDistance
                };
                const float fogParams2[4] = {
                    pp.fogSunScattering, pp.fogAnisotropy,
                    pp.fogVolumetric ? 1.0f : 0.0f,
                    static_cast<float>(std::clamp(pp.fogSteps, 1, 64))
                };
                m_postProcess.RenderLighting(
                    m_framebuffer->Color(),
                    m_framebuffer->Normal(),
                    m_framebuffer->WorldPos(),
                    m_framebuffer->Material(),
                    m_framebuffer->Features(),
                    m_shadowMaps[0], m_shadowMaps[1], m_shadowMaps[2],
                    lightMtx0, lightMtx1, lightMtx2,
                    invViewProj,
                    cascadeSplits,
                    cascadeNormalOffsets, cascadeDepthBias, pssmParams,
                    camPos, lDirA, lColA,
                    fogColorDensity, fogParams, fogParams2,
                    plPosA, plColA, plCount,
                    slPos, slDir, slCol, slCount,
                    20, renderW, renderH);

                // Particles are drawn after lighting into the HDR output
                // target (view 21), so bloom/tonemapping pick them up.
                if (m_particles) {
                    m_particles->Render(m_scene, m_camera, 21,
                                        m_postProcess.OutputFbo(), renderW, renderH);
                }

                // The post-processing passes are submitted in increasing view
                // ID order. The ping-pong swaps happen on the CPU, so the call
                // order must match the GPU view order exactly.

                // 1. SSGI trace (View 22) and SSGI/SSR composite (View 24).
                // The trace is half-resolution; the composite performs a
                // depth-aware upsample and adds the diffuse bounce before
                // applying the specular SSR pass.
                Galeshi::SSGIParams ssgi;
                ssgi.enabled = pp.ssgiEnabled;
                ssgi.intensity = pp.ssgiIntensity;
                ssgi.radius = pp.ssgiRadius;
                ssgi.thickness = pp.ssgiThickness;
                ssgi.rays = pp.ssgiRays;
                ssgi.steps = pp.ssgiSteps;

                bgfx::TextureHandle ssgiTex = BGFX_INVALID_HANDLE;
                if (ssgi.enabled) {
                    ssgiTex = m_postProcess.RenderSSGI(
                        m_postProcess.OutputTexture(), m_framebuffer->Normal(),
                        m_framebuffer->WorldPos(), m_framebuffer->Depth(),
                        m_camera.View(), proj, 22, renderW, renderH, ssgi);
                }

                if (pp.ssrEnabled || ssgi.enabled) {
                    const uint16_t ssgiW = static_cast<uint16_t>(
                        std::max<int>(1, renderW / 2));
                    const uint16_t ssgiH = static_cast<uint16_t>(
                        std::max<int>(1, renderH / 2));
                    if (bgfx::isValid(m_postProcess.RenderScreenSpaceLighting(
                            m_postProcess.OutputTexture(), m_framebuffer->Normal(),
                            m_framebuffer->Depth(), m_framebuffer->WorldPos(),
                            m_framebuffer->Color(), m_framebuffer->Material(),
                            m_postProcess.AOTexture(), ssgiTex,
                            m_postProcess.PingFbo(), m_camera.View(), proj,
                            invProj, camPos, 24, renderW, renderH,
                            pp.ssrEnabled, pp.ssrIntensity,
                            ssgi.enabled, pp.ssgiIntensity,
                            ssgiW, ssgiH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 2. Motion Blur (velocity view 23, blur view 25)
                if (pp.motionBlurEnabled && m_postProcess.HasPrevViewProj()) {
                    if (bgfx::isValid(m_postProcess.RenderMotionBlur(
                            m_postProcess.OutputTexture(), m_framebuffer->WorldPos(),
                            m_framebuffer->Velocity(),
                            m_postProcess.GetPrevViewProj(), taaInvVP,
                            m_postProcess.PingFbo(), 23, 25, renderW, renderH, m_frameDelta,
                            pp.motionBlurAmount, pp.motionBlurMaxVelocity))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 3. Atmosphere (View 26)
                if (pp.atmosphereEnabled) {
                    float sunDir[4] = { -lDirA[0], -lDirA[1], -lDirA[2], 20.0f };
                    if (bgfx::isValid(m_postProcess.RenderAtmosphere(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            glm::inverse(currVP), camPos, sunDir, m_postProcess.PingFbo(),
                            26, renderW, renderH, pp.atmosphereIntensity))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 4. Volumetric Light / god rays (View 27)
                if (pp.godRaysEnabled) {
                    float sunDir[4] = { -lDirA[0], -lDirA[1], -lDirA[2], 20.0f };
                    if (bgfx::isValid(m_postProcess.RenderGodRays(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            currVP, camPos, sunDir, m_postProcess.PingFbo(),
                            27, renderW, renderH, pp.godRaysIntensity, pp.godRaysDecay,
                            pp.godRaysDensity, pp.godRaysWeight))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 5. Depth of Field (Views 28..31)
                if (pp.dofEnabled) {
                    const float dofFocusDistance = ResolveDofFocusDistance(pp);
                    if (bgfx::isValid(m_postProcess.RenderDof(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            proj, invProj, m_postProcess.PingFbo(), 28, renderW, renderH,
                            dofFocusDistance, pp.dofBlur))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 6. Bloom (Views 32..35)
                if (pp.bloomEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderBloom(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            32, renderW, renderH, pp.bloomThreshold, pp.bloomIntensity,
                            pp.bloomRadius))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 7. TAA (Views 36..37)
                if (pp.taaEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderTaa(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            m_framebuffer->Velocity(),
                            m_postProcess.GetPrevViewProj(), taaInvVP,
                            m_postProcess.PingFbo(), 36, renderW, renderH, pp.taaBlend))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 8. Water (copy scene color/depth at View 38, water at
                // View 39). Rendering after TAA avoids smearing the animated
                // normals while keeping the water in the HDR tonemap chain.
                postFxAccumMs += ProfileElapsedMs(postFxSegmentStart,
                                                   ProfileClock::now());
                const auto waterStart = ProfileClock::now();
                {
                    m_postProcess.BlitTexture(m_postProcess.OutputTexture(),
                                              m_postProcess.BlitTextureA(), 38);
                    m_postProcess.BlitDepth(m_framebuffer->Depth(), 38);
                    bgfx::setViewFrameBuffer(39, m_postProcess.OutputFbo());
                    bgfx::setViewRect(39, 0, 0, renderW, renderH);
                    const bgfx::TextureHandle env =
                        bgfx::isValid(m_postProcess.EnvCubeMap())
                            ? m_postProcess.EnvCubeMap()
                            : m_postProcess.BlackCube();
                    m_render->RenderWater(
                        m_scene, m_camera, m_postProcess.BlitTextureA(),
                        m_postProcess.BlitDepth(), env,
                        m_render->PlanarReflectionTexture(),
                        m_render->PlanarReflectionPlaneY(),
                        waterSunDir, waterSunColor, 39, renderW, renderH,
                        static_cast<float>(m_time), m_camera.NearPlane(),
                        m_camera.FarPlane());
                }
                m_cpuProfile.waterMs = ProfileElapsedMs(waterStart, ProfileClock::now());

                // 8b. Groom hair: an independent forward transparent pass.
                // It reuses the HDR target and the copied opaque depth, and
                // is submitted before tonemapping/AA like other transparent
                // surfaces.
                const auto hairStart = ProfileClock::now();
                {
                    const bgfx::TextureHandle env =
                        bgfx::isValid(m_postProcess.EnvCubeMap())
                            ? m_postProcess.EnvCubeMap()
                            : m_postProcess.BlackCube();
                    bgfx::setViewFrameBuffer(39, m_postProcess.OutputFbo());
                    bgfx::setViewRect(39, 0, 0, renderW, renderH);
                    m_render->RenderHair(
                        m_scene, m_camera, m_postProcess.BlitDepth(), env,
                        waterSunDir, waterSunColor, 39, renderW, renderH,
                        m_camera.NearPlane(), m_camera.FarPlane());
                }
                m_cpuProfile.hairMs = ProfileElapsedMs(hairStart, ProfileClock::now());

                // 8c. Lit translucency: alpha-blended forward pass over the
                // HDR target, using the copied opaque depth for testing and
                // the PSSM shadow maps for shadow/transmission depth.
                {
                    const bgfx::TextureHandle env =
                        bgfx::isValid(m_postProcess.EnvCubeMap())
                            ? m_postProcess.EnvCubeMap()
                            : m_postProcess.BlackCube();
                    bgfx::setViewFrameBuffer(39, m_postProcess.OutputFbo());
                    bgfx::setViewRect(39, 0, 0, renderW, renderH);
                    const float transLightDir[4] = {
                        -waterSunDir[0], -waterSunDir[1], -waterSunDir[2], 0.0f
                    };
                    const float translucencyPssm[4] = {
                        m_pssmSlopeBias, PssmPcfRadius(),
                        m_pssmNormalOffsetSlope,
                        bgfx::getCaps()->originBottomLeft ? 0.0f : 1.0f
                    };
                    m_render->RenderTranslucency(
                        m_scene, m_camera, m_postProcess.BlitDepth(), env,
                        m_shadowMaps[0], m_shadowMaps[1], m_shadowMaps[2],
                        lightMtx0, lightMtx1, lightMtx2,
                        cascadeSplits, translucencyPssm,
                        transLightDir, waterSunColor, 39, renderW, renderH);
                }
                postFxSegmentStart = ProfileClock::now();

                // 9. Scene-referred grading + ACES tonemap (View 40)
                m_postProcess.RenderTonemap(
                    m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                    40, renderW, renderH,
                    pp.exposure * pp.colorExposure,
                    pp.contrast, pp.saturation,
                    pp.colorContrast, pp.colorSaturation);
                m_postProcess.SwapFbos();

                // 10. LUT-style environment palette (View 41), after tonemap
                m_postProcess.RenderColorCorrection(
                    m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                    41, renderW, renderH, 1.0f, 1.0f, 1.0f,
                    pp.environmentPaletteEnabled
                        ? pp.environmentPaletteAmount
                        : 0.0f,
                    pp.environmentPaletteShadow, pp.environmentPaletteMid,
                    pp.environmentPaletteHighlight);
                m_postProcess.SwapFbos();

                // 11. Vignette (View 42)
                m_postProcess.RenderVignette(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 42, renderW, renderH, pp.vignetteIntensity, pp.vignetteSmoothness);
                m_postProcess.SwapFbos();

                // 12. Chromatic Aberration (View 43)
                m_postProcess.RenderChromatic(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 43, renderW, renderH, pp.chromaticAberration);
                m_postProcess.SwapFbos();

                // 13. SMAA (Views 44..45)
                if (pp.smaaEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderSmaa(
                            m_postProcess.OutputTexture(), 44, renderW, renderH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 14. FXAA (View 46)
                if (pp.fxaaEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderFxaa(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            46, renderW, renderH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 15. Invert (View 47)
                if (pp.invertEnabled) {
                    if (bgfx::isValid(m_postProcess.RenderInvert(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            47, renderW, renderH))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // Store the unjittered camera matrix for the next frame's
                // motion blur and TAA reprojection.
                m_postProcess.SetPrevViewProj(taaVP);
            } else {
                // View 2 is reserved for the first directional shadow cascade.
                m_postProcess.Render(46, m_framebuffer->Color(), renderW, renderH);
            }
        }

        // Stop PostFX before editor debug gizmos; the final scene copy and
        // debug overlay are presentation/editor work and land in Other.
        if (postFxProfiling) {
            postFxAccumMs += ProfileElapsedMs(postFxSegmentStart,
                                               ProfileClock::now());
            m_cpuProfile.postFxMs = postFxAccumMs;
        }

        // Debug rendering (View 50). Editor-only overlays (grid, axes,
        // nav/AI debug, lights and post-process-volume bounds) are hidden
        // while Play is active, but collision-shape wireframes remain visible.
        if (m_debug && !m_isGameMode && m_framebuffer) {
            m_debug->Begin();
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (!m_editor.isPlaying) {
            m_debug->AddGrid(50.0f, 2.0f, {0.3f, 0.3f, 0.3f, 0.4f});
            m_debug->AddAxes({0, 0.05f, 0}, 2.0f);
            if (m_navSystem) {
                const auto& navTris = m_navSystem->GetDebugTriangles();
                if (!navTris.empty()) {
                    for (size_t i = 0; i + 2 < navTris.size(); i += 3) {
                        const glm::vec4 c = {0.2f, 0.8f, 0.4f, 0.5f};
                        m_debug->AddLine(navTris[i], navTris[i+1], c);
                        m_debug->AddLine(navTris[i+1], navTris[i+2], c);
                        m_debug->AddLine(navTris[i+2], navTris[i], c);
                    }
                }
            }
            auto navView = m_scene.Registry().view<Galeshi::NavAgentComponent, Galeshi::TransformComponent>();
            for (auto e : navView) {
                auto& tc = navView.get<Galeshi::TransformComponent>(e);
                auto& nav = navView.get<Galeshi::NavAgentComponent>(e);
                glm::vec3 worldPos = tc.position;
                entt::entity curr = e;
                int depth = 0;
                while (depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                    curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr))
                        worldPos += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                }
                m_debug->AddBox(worldPos, glm::vec3(0.5f), {0.2f, 1.0f, 0.2f, 1.0f});
                glm::vec3 prev = worldPos;
                for (const auto& pt : nav.path) {
                    m_debug->AddLine(prev, pt, {1.0f, 1.0f, 0.0f, 1.0f});
                    prev = pt;
                }
            }
            if (m_showAIDebug) {
                auto perceptionView = m_scene.Registry().view<Galeshi::PerceptionComponent,
                                                               Galeshi::TransformComponent>();
                for (auto e : perceptionView) {
                    const auto& perception =
                        perceptionView.get<Galeshi::PerceptionComponent>(e);
                    const auto& tc =
                        perceptionView.get<Galeshi::TransformComponent>(e);
                    glm::vec3 origin = tc.position;
                    entt::entity curr = e;
                    int depth = 0;
                    while (depth++ < 128 &&
                           m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr)) {
                            origin += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                        }
                    }

                    const glm::vec4 ringColor = perception.canSeeTarget
                        ? glm::vec4(0.3f, 1.0f, 0.3f, 0.9f)
                        : glm::vec4(0.9f, 0.7f, 0.2f, 0.55f);
                    const float y = origin.y + 0.04f;
                    for (int i = 0; i < 40; ++i) {
                        const float a1 = static_cast<float>(i) / 40.0f * 6.2831853f;
                        const float a2 = static_cast<float>(i + 1) / 40.0f * 6.2831853f;
                        m_debug->AddLine(
                            {origin.x + std::cos(a1) * perception.sightRadius, y,
                             origin.z + std::sin(a1) * perception.sightRadius},
                            {origin.x + std::cos(a2) * perception.sightRadius, y,
                             origin.z + std::sin(a2) * perception.sightRadius},
                            ringColor);
                    }

                    if (perception.targetEntity != entt::null &&
                        m_scene.Registry().valid(perception.targetEntity) &&
                        m_scene.Registry().all_of<Galeshi::TransformComponent>(
                            perception.targetEntity)) {
                        glm::vec3 target =
                            m_scene.Registry().get<Galeshi::TransformComponent>(
                                perception.targetEntity).position;
                        target.y += 1.0f;
                        const glm::vec3 eye = origin + glm::vec3(0.0f, perception.eyeHeight, 0.0f);
                        m_debug->AddLine(eye, target,
                            perception.canSeeTarget ? glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)
                                                    : glm::vec4(0.5f, 0.5f, 0.5f, 0.7f));
                        m_debug->AddBox(perception.lastKnownPosition + glm::vec3(0.0f, 0.2f, 0.0f),
                                        glm::vec3(0.2f),
                                        glm::vec4(0.2f, 0.8f, 1.0f, 0.8f));
                    }
                }
            }
            if (!m_editor.isPlaying) {
                auto plviewViz = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::PointLightComponent>();
                for (auto e : plviewViz) {
                    auto& tc = plviewViz.get<Galeshi::TransformComponent>(e);
                    auto& pl = plviewViz.get<Galeshi::PointLightComponent>(e);
                    float r = pl.radius;
                    glm::vec4 c = {1.0f, 1.0f, 0.0f, 1.0f};
                    for (int i = 0; i < 32; ++i) {
                        float a1 = i * 2.0f * 3.14159f / 32.0f;
                        float a2 = (i + 1) * 2.0f * 3.14159f / 32.0f;
                        m_debug->AddLine(tc.position + glm::vec3(cos(a1), 0, sin(a1))*r, tc.position + glm::vec3(cos(a2), 0, sin(a2))*r, c);
                        m_debug->AddLine(tc.position + glm::vec3(0, cos(a1), sin(a1))*r, tc.position + glm::vec3(0, cos(a2), sin(a2))*r, c);
                        m_debug->AddLine(tc.position + glm::vec3(cos(a1), sin(a1), 0)*r, tc.position + glm::vec3(cos(a2), sin(a2), 0)*r, c);
                    }
                }
                auto slviewViz = m_scene.Registry().view<Galeshi::TransformComponent, Galeshi::SpotLightComponent>();
                for (auto e : slviewViz) {
                    auto& tc = slviewViz.get<Galeshi::TransformComponent>(e);
                    auto& sl = slviewViz.get<Galeshi::SpotLightComponent>(e);
                    glm::vec3 dir = glm::normalize(sl.direction);
                    float r = sl.radius;
                    float outer = glm::radians(sl.outerConeAngle);
                    float coneR = tan(outer) * r;
                    glm::vec3 endPos = tc.position + dir * r;
                    glm::vec3 up = glm::vec3(0, 1, 0);
                    if (abs(dot(dir, up)) > 0.99f) up = glm::vec3(1, 0, 0);
                    glm::vec3 right = glm::normalize(glm::cross(dir, up));
                    glm::vec3 forward = glm::normalize(glm::cross(right, dir));
                    glm::vec4 c = {1.0f, 0.5f, 0.0f, 1.0f};
                    for (int i = 0; i < 16; ++i) {
                        float a1 = i * 2.0f * 3.14159f / 16.0f;
                        float a2 = (i + 1) * 2.0f * 3.14159f / 16.0f;
                        glm::vec3 p1 = endPos + (cos(a1)*right + sin(a1)*forward) * coneR;
                        glm::vec3 p2 = endPos + (cos(a2)*right + sin(a2)*forward) * coneR;
                        m_debug->AddLine(tc.position, p1, c);
                        m_debug->AddLine(p1, p2, c);
                    }
                }
            }
            if (!m_editor.isPlaying && sel && sel.HasComponent<Galeshi::TransformComponent>() && !sel.HasComponent<Galeshi::SkeletalComponent>()) {
                auto& tc = sel.GetComponent<Galeshi::TransformComponent>();
                m_debug->AddBox(tc.position, tc.scale + glm::vec3(0.1f), {1, 1, 0, 1});
            }

            // Yellow outline of the selected particle emitter's spawn volume.
            // Matches the ParticleSystem: the spawn area is centred on the
            // emitter's world position (parent chain applied) and is either an
            // axis-aligned box or a sphere.
            if (!m_editor.isPlaying && sel &&
                sel.HasComponent<Galeshi::ParticleEmitterComponent>() &&
                sel.HasComponent<Galeshi::TransformComponent>()) {
                const auto& pe = sel.GetComponent<Galeshi::ParticleEmitterComponent>();

                glm::mat4 world(1.0f);
                std::vector<entt::entity> chain;
                entt::entity cur = sel.Handle();
                int depth = 0;
                while (cur != entt::null && depth++ < 128) {
                    chain.push_back(cur);
                    if (m_scene.Registry().all_of<Galeshi::ParentComponent>(cur)) {
                        cur = m_scene.Registry().get<Galeshi::ParentComponent>(cur).parent;
                    } else {
                        break;
                    }
                }
                for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                    if (m_scene.Registry().all_of<Galeshi::TransformComponent>(*it)) {
                        world = world * m_scene.Registry().get<Galeshi::TransformComponent>(*it).LocalMatrix();
                    }
                }
                const glm::vec3 origin(world[3]);
                const glm::vec4 yellow = {1.0f, 1.0f, 0.0f, 1.0f};

                const bool useBox = pe.spawnBoxSize.x > 0.0f ||
                                    pe.spawnBoxSize.y > 0.0f ||
                                    pe.spawnBoxSize.z > 0.0f;
                if (useBox) {
                    // AddBox() expects the full size, spawnBoxSize holds half extents.
                    m_debug->AddBox(origin, pe.spawnBoxSize * 2.0f, yellow);
                } else if (pe.spawnRadius > 0.0f) {
                    const int segments = 48;
                    for (int i = 0; i < segments; ++i) {
                        const float a1 = static_cast<float>(i) / segments * 6.2831853f;
                        const float a2 = static_cast<float>(i + 1) / segments * 6.2831853f;
                        const float c1 = std::cos(a1) * pe.spawnRadius;
                        const float s1 = std::sin(a1) * pe.spawnRadius;
                        const float c2 = std::cos(a2) * pe.spawnRadius;
                        const float s2 = std::sin(a2) * pe.spawnRadius;
                        m_debug->AddLine(origin + glm::vec3(c1, s1, 0.0f),
                                         origin + glm::vec3(c2, s2, 0.0f), yellow);
                        m_debug->AddLine(origin + glm::vec3(c1, 0.0f, s1),
                                         origin + glm::vec3(c2, 0.0f, s2), yellow);
                        m_debug->AddLine(origin + glm::vec3(0.0f, c1, s1),
                                         origin + glm::vec3(0.0f, c2, s2), yellow);
                    }
                }
            }

            } // !isPlaying: editor-only overlays

            // Brush outline: draw the actual brush footprint on the height
            // field so radius is visible before painting/sculpting.
            if (!m_editor.isPlaying && m_lastBrushHitValid && sel &&
                sel.HasComponent<Galeshi::LandscapeComponent>() &&
                m_editor.toolMode != Galeshi::EditorState::ToolMode::Select) {
                const auto& landscape =
                    sel.GetComponent<Galeshi::LandscapeComponent>();
                const glm::mat4 world = EntityWorldMatrix(sel.Handle());
                const float radius = std::max(0.05f, m_lastBrushRadius);
                const glm::vec4 color =
                    m_editor.toolMode == Galeshi::EditorState::ToolMode::PaintTrees
                        ? glm::vec4(0.30f, 1.00f, 0.30f, 1.0f)
                        : m_editor.toolMode == Galeshi::EditorState::ToolMode::PaintGrass
                            ? glm::vec4(0.35f, 0.85f, 0.25f, 1.0f)
                            : glm::vec4(1.00f, 0.80f, 0.20f, 1.0f);
                constexpr int segments = 64;
                glm::vec3 previous(0.0f);
                for (int i = 0; i <= segments; ++i) {
                    const float a = static_cast<float>(i) / segments *
                                    6.2831853f;
                    const float x = m_lastBrushLocal.x + std::cos(a) * radius;
                    const float z = m_lastBrushLocal.z + std::sin(a) * radius;
                    const float y = Galeshi::LandscapeMesh::SampleHeight(
                        landscape, x, z) + 0.035f;
                    const glm::vec3 point = glm::vec3(
                        world * glm::vec4(x, y, z, 1.0f));
                    if (i > 0) m_debug->AddLine(previous, point, color);
                    previous = point;
                }
            }

            // Camera gizmos (editor only): a small frustum makes the camera
            // object visible and clearly shows its viewing direction.
            if (!m_editor.isPlaying) {
                auto cameraView = m_scene.Registry()
                    .view<Galeshi::CameraComponent,
                          Galeshi::TransformComponent>();
                const float aspect = m_viewportH
                    ? static_cast<float>(m_viewportW) / m_viewportH
                    : 16.0f / 9.0f;
                for (auto cameraEntity : cameraView) {
                    const auto& cameraComponent =
                        cameraView.get<Galeshi::CameraComponent>(cameraEntity);
                    const glm::mat4 world = EntityWorldMatrix(cameraEntity);
                    const glm::vec3 eye = glm::vec3(world[3]);
                    glm::vec3 forward = glm::vec3(
                        world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f));
                    glm::vec3 up = glm::vec3(
                        world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f));
                    forward = glm::dot(forward, forward) < 1e-6f
                        ? glm::vec3(0,0,-1) : glm::normalize(forward);
                    up = glm::dot(up, up) < 1e-6f
                        ? glm::vec3(0,1,0) : glm::normalize(up);
                    glm::vec3 right = glm::normalize(glm::cross(forward, up));
                    up = glm::normalize(glm::cross(right, forward));
                    const bool selectedCamera = sel && sel.Handle() == cameraEntity;
                    const glm::vec4 color = selectedCamera
                        ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
                        : glm::vec4(0.25f, 0.8f, 1.0f, 0.85f);
                    const float nearPlane = 0.45f;
                    const float halfH = std::tan(glm::radians(
                        glm::clamp(cameraComponent.fovY, 1.0f, 179.0f)) * 0.5f) *
                        nearPlane;
                    const float halfW = halfH * aspect;
                    const glm::vec3 center = eye + forward * nearPlane;
                    const glm::vec3 corners[4] = {
                        center - right * halfW - up * halfH,
                        center + right * halfW - up * halfH,
                        center + right * halfW + up * halfH,
                        center - right * halfW + up * halfH,
                    };
                    for (int i = 0; i < 4; ++i) {
                        m_debug->AddLine(eye, corners[i], color);
                        m_debug->AddLine(corners[i], corners[(i + 1) % 4], color);
                    }
                    m_debug->AddLine(eye, eye + forward * 0.9f, color);
                }
            }

            // Collision shape gizmos stay visible across Play/Stop. Only the
            // per-collider Visible in Editor flag controls the wireframe.
            DrawCollisionShapeGizmos();

            // Post-process volume bounds.
            if (!m_editor.isPlaying) {
                auto volumeView = m_scene.Registry()
                    .view<Galeshi::PostProcessVolumeComponent>();
                for (auto e : volumeView) {
                    const auto& volume =
                        volumeView.get<Galeshi::PostProcessVolumeComponent>(e);
                    if (!volume.visibleInEditor) continue;

                    const bool isSelected = sel && sel.Handle() == e;
                    const glm::vec4 color = isSelected
                        ? glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)
                        : glm::vec4(0.65f, 0.30f, 1.00f, 0.85f);

                    const glm::mat4 world = EntityWorldMatrix(e);
                    glm::vec3 worldScale, translation, worldSkew;
                    glm::vec4 worldPerspective;
                    glm::quat rotation;
                    glm::decompose(world, worldScale, rotation, translation,
                                   worldSkew, worldPerspective);

                    const glm::vec3 h = volume.size * 0.5f;
                    auto toWorld = [&](const glm::vec3& p) {
                        return translation + rotation * p;
                    };
                    glm::vec3 p[8] = {
                        toWorld({-h.x, -h.y, -h.z}),
                        toWorld({ h.x, -h.y, -h.z}),
                        toWorld({ h.x, -h.y,  h.z}),
                        toWorld({-h.x, -h.y,  h.z}),
                        toWorld({-h.x,  h.y, -h.z}),
                        toWorld({ h.x,  h.y, -h.z}),
                        toWorld({ h.x,  h.y,  h.z}),
                        toWorld({-h.x,  h.y,  h.z}),
                    };
                    auto edge = [&](int a, int b) {
                        m_debug->AddLine(p[a], p[b], color);
                    };
                    edge(0, 1); edge(1, 2); edge(2, 3); edge(3, 0);
                    edge(4, 5); edge(5, 6); edge(6, 7); edge(7, 4);
                    edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7);
                }
            }
            if (m_framebuffer && bgfx::isValid(m_framebuffer->Handle())) {
                // Draw directly into the G-buffer FBO: it already owns the
                // scene depth, and its color attachment has been filled with
                // the post-processed image at view 48. The G-buffer color is
                // no longer sampled after the lighting pass.
                bgfx::setViewFrameBuffer(50, m_framebuffer->Handle());
                m_debug->Render(m_camera, 50, renderW, renderH);
            }
        }

        // Copy the final HDR color into the G-buffer color attachment, then
        // blend the transparent layer over it (view 48, before glass at 49,
        // debug at 50 and the ImGui blit at 51). Reusing the persistent
        // G-buffer FBO avoids temporary FBOs whose layout can get out of sync.
        if (renderThisFrame && m_framebuffer &&
            bgfx::isValid(m_framebuffer->Color()) &&
            m_postProcess.OutputFbo().idx != bgfx::kInvalidHandle) {
            m_postProcess.BlitTexture(m_postProcess.OutputTexture(), m_framebuffer->Color(), 48);
            bgfx::setViewFrameBuffer(49, m_framebuffer->Handle());
            bgfx::setViewRect(49, 0, 0, renderW, renderH);
            m_render->RenderGlass(m_scene, m_camera, 49, renderW, renderH);
        }

        // Set when the 2D UI canvas overlay handles the click this frame.
        bool uiOverlayConsumed = false;

        // Blit for ImGui (View 51). While Realtime Rendering is paused this
        // pass is skipped and the last completed viewport image is sampled
        // directly, so the UI stays responsive without submitting scene work.
        bgfx::TextureHandle __disp = BGFX_INVALID_HANDLE;
        if (renderThisFrame && m_framebuffer &&
            bgfx::isValid(m_framebuffer->Color())) {
            bgfx::TextureHandle __ppTex = m_framebuffer->Color();
            __disp = __ppTex;
            m_postProcess.BlitTexture(__ppTex, m_postProcess.BlitTextureA(), 51);
            __disp = m_postProcess.BlitTextureA();
            m_lastViewportTexture = __disp;
        } else if (bgfx::isValid(m_lastViewportTexture)) {
            __disp = m_lastViewportTexture;
        }

        if (bgfx::isValid(__disp)) {
            ImTextureID texId = static_cast<ImTextureID>(__disp.idx);
            ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0, 0), ImVec2(1, 1));
            m_viewportImagePos = ImGui::GetItemRectMin();
            m_viewportImageSize = ImGui::GetItemRectSize();

            // Keep the lightweight viewport stats visible instead of burying
            // them in a menu. Draw them after the image so they overlay the
            // rendered scene without becoming interactive viewport widgets.
            {
                char fpsText[64];
                char entitiesText[64];
                std::snprintf(fpsText, sizeof(fpsText), "FPS: %.1f",
                              ImGui::GetIO().Framerate);
                std::snprintf(entitiesText, sizeof(entitiesText), "Entities: %d",
                              static_cast<int>(m_scene.AllEntities().size()));

                const bool renderPassesPaused =
                    !m_realtimeRendering && !m_isGameMode;
                const char* realtimeText = renderPassesPaused
                    ? "Render passes: PAUSED (scene running)"
                    : "Render passes: Realtime";

                const float lineHeight = ImGui::GetTextLineHeight();
                const ImVec2 padding(8.0f, 5.0f);
                const ImVec2 textPos(m_viewportImagePos.x + padding.x,
                                     m_viewportImagePos.y + padding.y);
                const float textWidth = std::max(
                    std::max(ImGui::CalcTextSize(fpsText).x,
                             ImGui::CalcTextSize(entitiesText).x),
                    ImGui::CalcTextSize(realtimeText).x);
                const ImVec2 panelMin = m_viewportImagePos;
                const ImVec2 panelMax(
                    m_viewportImagePos.x + textWidth + padding.x * 2.0f,
                    m_viewportImagePos.y + lineHeight * 3.0f +
                        padding.y * 2.0f);

                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(panelMin, panelMax,
                                        IM_COL32(10, 12, 16, 150), 5.0f);
                drawList->AddText(ImVec2(textPos.x + 1.0f, textPos.y + 1.0f),
                                  IM_COL32(0, 0, 0, 180), fpsText);
                drawList->AddText(textPos, IM_COL32(235, 240, 248, 255), fpsText);
                drawList->AddText(ImVec2(textPos.x + 1.0f,
                                         textPos.y + lineHeight + 1.0f),
                                  IM_COL32(0, 0, 0, 180), entitiesText);
                drawList->AddText(ImVec2(textPos.x, textPos.y + lineHeight),
                                  IM_COL32(235, 240, 248, 255), entitiesText);
                drawList->AddText(
                    ImVec2(textPos.x + 1.0f, textPos.y + lineHeight * 2.0f + 1.0f),
                    IM_COL32(0, 0, 0, 180), realtimeText);
                drawList->AddText(
                    ImVec2(textPos.x, textPos.y + lineHeight * 2.0f),
                    renderPassesPaused
                        ? IM_COL32(255, 190, 90, 255)
                        : IM_COL32(140, 220, 150, 255),
                    realtimeText);
            }

            // Accept assets dragged from the Content Browser. Models are
            // spawned under the cursor; textures are still applied to the
            // selected material by OnAssetDropped().
            const ImVec2 dropImageMin = ImGui::GetItemRectMin();
            const ImVec2 dropImageMax = ImGui::GetItemRectMax();
            if (!m_isGameMode) {
                Galeshi::Entity dropSelection = m_editor.Selected(&m_scene);
                if (const ImGuiPayload* active = ImGui::GetDragDropPayload();
                    active && active->IsDataType("GALESHI_ASSET_PATH") &&
                    IsDragDropModelAssetPath(static_cast<const char*>(active->Data)) &&
                    dropSelection &&
                    ImGui::IsItemHovered(
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                    if (dropSelection.HasComponent<Galeshi::LandscapeComponent>()) {
                        ImGui::SetTooltip(
                            "Drop to assign this model as the Landscape tree mesh.");
                    } else if (IsGltfModelAssetPath(
                                   static_cast<const char*>(active->Data)) &&
                               FindSkeletalRootForReplacement(
                                   dropSelection.Handle())) {
                        ImGui::SetTooltip(
                            "Drop to replace the selected character's model "
                            "with this glTF/GLB.\nMaterial values are reloaded "
                            "from the file.");
                    }
                }
            }
            if (!m_isGameMode && ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path = static_cast<const char*>(payload->Data);
                    if (path && *path) {
                        const glm::vec3 dropPos = ViewportDropWorldPosition(
                            ImGui::GetMousePos(), dropImageMin, w, h);
                        OnAssetDropped(path, dropPos);
                    }
                }
                ImGui::EndDragDropTarget();
            }

            // 2D UI Studio canvas overlay.  It draws the authored canvas on
            // top of the viewport and handles selection/move/resize before
            // the 3D picking below gets a chance to run.
            if (!m_isGameMode && !m_editor.isPlaying && m_uiCanvasOverlay) {
                if (Galeshi::UICanvas* overlayCanvas = ActiveUICanvas()) {
                    uiOverlayConsumed = DrawUICanvasOverlay(
                        *overlayCanvas, dropImageMin, dropImageMax);
                }
            }
        } else {
            // Keep the viewport item geometry valid for gizmos, picking and
            // layout while there is no completed frame to display yet.
            ImGui::Dummy(ImVec2(static_cast<float>(w),
                                static_cast<float>(h)));
            m_viewportImagePos = ImGui::GetItemRectMin();
            m_viewportImageSize = ImGui::GetItemRectSize();
        }

        if (bgfx::isValid(__disp)) {
            if (m_isGameMode) {
                ImGui::End();
                return; // Skip gizmo and mouse picking in game mode.
            }

            ImVec2 imgMin = ImGui::GetItemRectMin();
            ImVec2 imgMax = ImGui::GetItemRectMax();

            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            const bool selHasTf = sel && sel.HasComponent<Galeshi::TransformComponent>();
            const bool selectedIsUICanvas =
                sel && sel.HasComponent<Galeshi::UICanvasComponent>();
            const bool landscapeSculptActive =
                !m_isGameMode && selHasTf &&
                sel.HasComponent<Galeshi::LandscapeComponent>() &&
                m_editor.toolMode != Galeshi::EditorState::ToolMode::Select &&
                !uiOverlayConsumed &&
                (!m_editor.isPlaying || !m_uiRuntime.IsPointerOverUI());

            // Landscape sculpting uses the same camera ray as picking, then
            // raycasts the actual height field instead of entity AABBs.
            if (landscapeSculptActive) {
                const ImVec2 sculptMouse = ImGui::GetMousePos();
                const bool overImage =
                    ImGui::IsWindowHovered() &&
                    sculptMouse.x >= imgMin.x && sculptMouse.x <= imgMax.x &&
                    sculptMouse.y >= imgMin.y && sculptMouse.y <= imgMax.y;
                const float ndcX = ((sculptMouse.x - imgMin.x) / w) * 2.0f - 1.0f;
                const float ndcY = 1.0f - ((sculptMouse.y - imgMin.y) / h) * 2.0f;
                const glm::mat4 invVP = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec4 rayStart4 = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 rayEnd4   = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
                rayStart4 /= rayStart4.w;
                rayEnd4 /= rayEnd4.w;
                const glm::vec3 rayOrigin = glm::vec3(rayStart4);
                const glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd4 - rayStart4));

                glm::mat4 landscapeWorld(1.0f);
                {
                    entt::entity curr = sel.Handle();
                    std::vector<entt::entity> chain;
                    int depth = 0;
                    while (curr != entt::null && depth++ < 128) {
                        chain.push_back(curr);
                        if (m_scene.Registry().all_of<Galeshi::ParentComponent>(curr))
                            curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        else
                            break;
                    }
                    for (size_t ci = chain.size(); ci-- > 0;) {
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(chain[ci]))
                            landscapeWorld = landscapeWorld *
                                m_scene.Registry().get<Galeshi::TransformComponent>(chain[ci]).LocalMatrix();
                    }
                }

                auto& landscape = sel.GetComponent<Galeshi::LandscapeComponent>();
                Galeshi::LandscapeRaycastHit hit;
                m_lastBrushHitValid = false;
                if (overImage &&
                    Galeshi::LandscapeMesh::Raycast(landscape, landscapeWorld,
                                                    rayOrigin, rayDir, hit)) {
                    m_lastSculptHeight = hit.height;
                    m_lastBrushLocal = hit.localPosition;
                    m_lastBrushRadius = m_editor.brushRadius;
                    m_lastBrushHitValid = true;

                    const bool paintTrees =
                        m_editor.toolMode ==
                        Galeshi::EditorState::ToolMode::PaintTrees;
                    const bool paintGrass =
                        m_editor.toolMode ==
                        Galeshi::EditorState::ToolMode::PaintGrass;
                    const bool paintLandscape =
                        m_editor.toolMode ==
                        Galeshi::EditorState::ToolMode::PaintLandscape;
                    const bool paintMode = paintTrees || paintGrass;
                    const bool leftClicked =
                        ImGui::IsMouseClicked(ImGuiMouseButton_Left);

                    if (leftClicked) {
                        PushHistory();
                        m_wasSculpting = true;
                        m_paintStampValid = false;
                    }
                    if (paintLandscape &&
                        (leftClicked ||
                         (m_wasSculpting &&
                          ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
                        const bool erase = ImGui::GetIO().KeyAlt;
                        if (Galeshi::LandscapeMesh::ApplyPaintBrush(
                                landscape, landscape.activeLayer,
                                hit.localPosition.x,
                                hit.localPosition.z, m_editor.brushRadius,
                                m_editor.brushStrength,
                                m_editor.brushFalloff,
                                ImGui::GetIO().DeltaTime, erase) > 0) {
                            // Bake after the stroke so sources are decoded at
                            // most once per mouse release, even if the cursor
                            // leaves the viewport.
                            m_pendingLandscapeBake = sel.Handle();
                        }
                    } else if (paintMode &&
                        (leftClicked ||
                         (m_wasSculpting &&
                          ImGui::IsMouseDown(ImGuiMouseButton_Left)))) {
                        const float minStep =
                            std::max(0.05f, m_editor.brushRadius * 0.2f);
                        const float dx =
                            hit.localPosition.x - m_lastPaintLocal.x;
                        const float dz =
                            hit.localPosition.z - m_lastPaintLocal.z;
                        if (!m_paintStampValid ||
                            dx * dx + dz * dz >= minStep * minStep) {
                            PaintVegetationAt(sel, landscape,
                                              hit.localPosition,
                                              paintGrass);
                            m_lastPaintLocal = hit.localPosition;
                            m_paintStampValid = true;
                        }
                    } else if (m_wasSculpting &&
                               ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        Galeshi::LandscapeBrushSettings settings;
                        switch (m_editor.toolMode) {
                            case Galeshi::EditorState::ToolMode::Raise:
                                settings.tool = Galeshi::LandscapeBrush::Raise; break;
                            case Galeshi::EditorState::ToolMode::Lower:
                                settings.tool = Galeshi::LandscapeBrush::Lower; break;
                            case Galeshi::EditorState::ToolMode::Smooth:
                                settings.tool = Galeshi::LandscapeBrush::Smooth; break;
                            case Galeshi::EditorState::ToolMode::Flatten:
                                settings.tool = Galeshi::LandscapeBrush::Flatten; break;
                            case Galeshi::EditorState::ToolMode::Noise:
                                settings.tool = Galeshi::LandscapeBrush::Noise; break;
                            case Galeshi::EditorState::ToolMode::Terrace:
                                settings.tool = Galeshi::LandscapeBrush::Terrace; break;
                            case Galeshi::EditorState::ToolMode::Sharpen:
                                settings.tool = Galeshi::LandscapeBrush::Sharpen; break;
                            default: break;
                        }
                        settings.radius = m_editor.brushRadius;
                        settings.strength = m_editor.brushStrength;
                        settings.falloff = m_editor.brushFalloff;
                        settings.flattenHeight = m_editor.flattenHeight;
                        settings.noiseFrequency = m_editor.noiseFrequency;
                        settings.terraceStep = m_editor.terraceStep;
                        if (Galeshi::LandscapeMesh::ApplyBrush(
                                landscape, settings,
                                hit.localPosition.x,
                                hit.localPosition.z,
                                ImGui::GetIO().DeltaTime) > 0) {
                            if (sel.HasComponent<Galeshi::MeshComponent>()) {
                                auto& meshComponent =
                                    sel.GetComponent<Galeshi::MeshComponent>();
                                if (meshComponent.mesh) {
                                    Galeshi::LandscapeMesh::UpdateMesh(
                                        landscape, *meshComponent.mesh);
                                }
                            }
                        }
                    }

                    if (m_wasSculpting &&
                        !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                        m_wasSculpting = false;
                        m_paintStampValid = false;
                        if (!paintMode && !paintLandscape &&
                            sel.HasComponent<Galeshi::VegetationComponent>()) {
                            sel.GetComponent<Galeshi::VegetationComponent>().dirty = true;
                        }
                    }
                }

                if (m_wasSculpting &&
                    !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_wasSculpting = false;
                    m_paintStampValid = false;
                }
            } else {
                m_wasSculpting = false;
            }

            // World position of the selected object (walk up parents)
            glm::vec3 selWorldPos{0.0f};
            if (selHasTf) {
                selWorldPos = sel.GetComponent<Galeshi::TransformComponent>().position;
                entt::entity curr = sel.Handle();
                int __depth = 0; while (__depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                    curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr))
                        selWorldPos += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                }
            }

            // Project selected object to screen (center hit-test)
            glm::vec2 selScreen{0.0f, 0.0f};
            bool selOnScreen = false;
            if (selHasTf) {
                glm::vec4 clip = m_camera.Projection() * m_camera.View() * glm::vec4(selWorldPos, 1.0f);
                if (clip.w > 0.0001f) {
                    glm::vec3 ndc = glm::vec3(clip) / clip.w;
                    selScreen.x = imgMin.x + (ndc.x * 0.5f + 0.5f) * static_cast<float>(w);
                    selScreen.y = imgMin.y + (1.0f - (ndc.y * 0.5f + 0.5f)) * static_cast<float>(h);
                    selOnScreen = true;
                }
            }

            // --- Gizmo: apply translation/rotation/scale to the ECS. ---
            if (selHasTf && !landscapeSculptActive && !selectedIsUICanvas &&
                !uiOverlayConsumed && !m_editor.isPlaying) {
                ImGuizmo::SetDrawlist();
                ImGuizmo::SetRect(imgMin.x, imgMin.y, w, h);
                ImGuizmo::SetGizmoSizeClipSpace(0.12f);
                glm::mat4 visView  = m_camera.View();
                glm::mat4 visProj  = m_camera.Projection();

                // Compute the world matrix and the parent world matrix
                // separately so the manipulated world transform can be
                // converted back into the entity's local transform.
                glm::mat4 worldMatrix(1.0f);
                glm::mat4 parentWorld(1.0f);
                entt::entity curr = sel.Handle();
                std::vector<entt::entity> chain;
                int depth = 0;
                while (curr != entt::null && depth++ < 128) {
                    chain.push_back(curr);
                    if (m_scene.Registry().all_of<Galeshi::ParentComponent>(curr))
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    else
                        break;
                }
                // chain[0] is the selected entity, the rest are ancestors.
                for (size_t ci = chain.size(); ci-- > 0; ) {
                    if (!m_scene.Registry().all_of<Galeshi::TransformComponent>(chain[ci]))
                        continue;
                    const glm::mat4 local =
                        m_scene.Registry().get<Galeshi::TransformComponent>(chain[ci]).LocalMatrix();
                    worldMatrix = worldMatrix * local;
                    if (ci > 0) parentWorld = parentWorld * local;
                }

                ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                switch (m_editor.Mode()) {
                    case Galeshi::GizmoMode::Rotate: operation = ImGuizmo::ROTATE; break;
                    case Galeshi::GizmoMode::Scale:  operation = ImGuizmo::SCALE;  break;
                    case Galeshi::GizmoMode::Translate:
                    default:                         operation = ImGuizmo::TRANSLATE; break;
                }

                glm::mat4 visModel = worldMatrix;
                const bool manipulated = ImGuizmo::Manipulate(
                    glm::value_ptr(visView), glm::value_ptr(visProj),
                    operation, ImGuizmo::WORLD, glm::value_ptr(visModel));
                const bool usingGizmo = manipulated || ImGuizmo::IsUsing();

                if (usingGizmo) {
                    // Snapshot the pre-drag state on the first frame that
                    // actually changes the matrix. A plain click on a handle
                    // must not create a redundant undo entry.
                    if (manipulated && !m_wasUsingGizmo) {
                        PushHistory();
                        m_wasUsingGizmo = true;
                    }

                    if (manipulated) {
                        glm::vec3 localTranslation, localScale, localSkew;
                        glm::vec4 localPerspective;
                        glm::quat localRotation;
                        const glm::mat4 localMatrix = glm::inverse(parentWorld) * visModel;
                        if (glm::decompose(localMatrix, localScale, localRotation,
                                           localTranslation, localSkew, localPerspective)) {
                            auto& tcGizmo = sel.GetComponent<Galeshi::TransformComponent>();
                            tcGizmo.position = localTranslation;
                            tcGizmo.rotation = glm::normalize(localRotation);
                            tcGizmo.scale = localScale;

                            if (sel.HasComponent<Galeshi::RigidBodyComponent>()) {
                                auto& rb = sel.GetComponent<Galeshi::RigidBodyComponent>();
                                glm::vec3 worldTranslation, worldScale, worldSkew;
                                glm::vec4 worldPerspective;
                                glm::quat worldRotation;
                                if (glm::decompose(visModel, worldScale, worldRotation,
                                                   worldTranslation, worldSkew, worldPerspective)) {
                                    if (rb.bodyId != 0xFFFFFFFF) {
                                        m_physics->SetBodyTransform(rb.bodyId, worldTranslation,
                                                                    glm::normalize(worldRotation));
                                    }
                                }
                            }
                        }
                    }
                } else if (m_wasUsingGizmo) {
                    // Store the final transform so Redo can restore the drag.
                    PushHistory();
                    m_wasUsingGizmo = false;
                }
            } else {
                m_wasUsingGizmo = false;
            }

            const float centerRadiusPx = 24.0f;
            ImVec2 mouse = ImGui::GetMousePos();
            const bool overCenter = selOnScreen &&
                std::hypot(mouse.x - selScreen.x, mouse.y - selScreen.y) < centerRadiusPx;

            // Begin drag on LMB down over center
            if (!landscapeSculptActive && !selectedIsUICanvas &&
                !uiOverlayConsumed && !m_editor.isPlaying &&
                ImGui::IsItemHovered() && !m_isDragging &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overCenter &&
                !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                PushHistory();
                m_isDragging = true;
                m_dragStartMouse = { mouse.x, mouse.y };
                m_dragStartObjWorld = selWorldPos;
            }

            // Perform drag while LMB held
            if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && selHasTf &&
                !m_editor.isPlaying && !ImGuizmo::IsUsing()) {
                const float dx = mouse.x - m_dragStartMouse.x;
                const float dy = mouse.y - m_dragStartMouse.y;

                glm::vec3 camPos = m_camera.Position();
                float dist = glm::length(selWorldPos - camPos);
                dist = std::max(dist, 0.5f);
                const float fovY = glm::radians(m_preferences.cameraFovDegrees);  // matches SetPerspective
                const float worldPerPx = (2.0f * std::tan(fovY * 0.5f) * dist) /
                                         static_cast<float>(h);

                glm::mat4 v = m_camera.View();
                glm::vec3 right = glm::vec3(v[0]);  // view column 0 = camera right (world)
                glm::vec3 up    = glm::vec3(v[1]);  // view column 1 = camera up    (world)

                glm::vec3 deltaWorld = right * ( dx * worldPerPx)
                                     + up    * (-dy * worldPerPx);
                glm::vec3 newWorld = m_dragStartObjWorld + deltaWorld;

                // Convert to local if the object has a parent
                glm::vec3 parentOffset{0.0f};
                {
                    entt::entity curr = sel.Handle();
                    int __depth = 0; while (__depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr))
                            parentOffset += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                    }
                }

                auto& tcSel = sel.GetComponent<Galeshi::TransformComponent>();
                tcSel.position = newWorld - parentOffset;

                if (sel.HasComponent<Galeshi::RigidBodyComponent>()) {
                    auto& rb = sel.GetComponent<Galeshi::RigidBodyComponent>();
                    if (rb.bodyId != 0xFFFFFFFF)
                        m_physics->SetBodyTransform(rb.bodyId, tcSel.position, tcSel.rotation);
                }
            }

            if (m_isDragging && ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
                m_isDragging = false;
                PushHistory();
            }

            // Center hint circle (drag affordance)
            if (selOnScreen && !selectedIsUICanvas && !m_editor.isPlaying) {
                ImU32 hintColor = overCenter ? IM_COL32(255, 220, 40, 220)
                                             : IM_COL32(255, 255, 255, 110);
                ImGui::GetWindowDrawList()->AddCircle(
                    ImVec2(selScreen.x, selScreen.y),
                    centerRadiusPx * 0.5f, hintColor, 24, 2.0f);
            }

            if (!landscapeSculptActive && !uiOverlayConsumed &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && !m_isDragging &&
                !overCenter && !ImGuizmo::IsOver()) {
                ImVec2 mouse = ImGui::GetMousePos();
                float mx = mouse.x - imgMin.x;
                float my = mouse.y - imgMin.y;
                float ndcX = (mx / w) * 2.0f - 1.0f;
                float ndcY = 1.0f - (my / h) * 2.0f;

                glm::mat4 invVP = glm::inverse(m_camera.Projection() * m_camera.View());
                glm::vec4 rayStart = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
                glm::vec4 rayEnd   = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
                rayStart /= rayStart.w; rayEnd /= rayEnd.w;
                
                glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd - rayStart));
                glm::vec3 rayOrigin = glm::vec3(rayStart);

                Galeshi::Entity bestHit;
                float bestDist = 99999.0f;

                auto view = m_scene.Registry().view<Galeshi::TransformComponent>();
                for (auto e : view) {
                    // UI canvases are viewport overlays, not 3D scene geometry.
                    if (m_scene.Registry().all_of<Galeshi::UICanvasComponent>(e))
                        continue;
                    // Invisible standalone collision shapes must not be
                    // selectable during Play.
                    if (m_editor.isPlaying &&
                        m_scene.Registry().all_of<Galeshi::ColliderComponent>(e) &&
                        !m_scene.Registry().all_of<Galeshi::MeshComponent>(e)) {
                        continue;
                    }
                    auto& tc = view.get<Galeshi::TransformComponent>(e);
                    entt::entity curr = e;
                    int __depth = 0;
                    while (__depth++ < 128 &&
                           m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                    }

                    glm::vec3 boundsCenter, boundsExtents;
                    if (!MeshEntityWorldBounds(e, boundsCenter, boundsExtents)) {
                        // Standalone colliders have their own precise test below.
                        if (m_scene.Registry().all_of<Galeshi::ColliderComponent>(e))
                            continue;
                        boundsCenter = glm::vec3(EntityWorldMatrix(e)[3]);
                        boundsExtents = glm::vec3(0.5f);
                    }

                    const glm::vec3 minB = boundsCenter - boundsExtents;
                    const glm::vec3 maxB = boundsCenter + boundsExtents;
                    float tmin = -std::numeric_limits<float>::infinity();
                    float tmax = std::numeric_limits<float>::infinity();
                    bool miss = false;
                    for (int axis = 0; axis < 3 && !miss; ++axis) {
                        if (std::abs(rayDir[axis]) < 1e-8f) {
                            if (rayOrigin[axis] < minB[axis] ||
                                rayOrigin[axis] > maxB[axis]) {
                                miss = true;
                            }
                        } else {
                            float t1 = (minB[axis] - rayOrigin[axis]) /
                                       rayDir[axis];
                            float t2 = (maxB[axis] - rayOrigin[axis]) /
                                       rayDir[axis];
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) miss = true;
                        }
                    }
                    if (miss || tmax < 0.0f) continue;

                    const float hitDistance = tmin > 0.0f ? tmin : tmax;
                    if (hitDistance < bestDist) {
                        bestDist = hitDistance;
                        bestHit = Galeshi::Entity(curr, &m_scene); // Select the root, not the bone.
                    }
                }

                // Collision shapes have no mesh, so pick their authored volume
                // directly. This also makes standalone shapes selectable.
                auto colliderPickView = m_scene.Registry()
                    .view<Galeshi::ColliderComponent, Galeshi::TransformComponent>();
                for (auto e : colliderPickView) {
                    if (m_editor.isPlaying) continue;
                    const auto& collider =
                        colliderPickView.get<Galeshi::ColliderComponent>(e);
                    if (!collider.visibleInEditor) continue;

                    const glm::mat4 world = EntityWorldMatrix(e);
                    const glm::mat4 invWorld = glm::inverse(world);
                    const glm::vec3 localOrigin =
                        glm::vec3(invWorld * glm::vec4(rayOrigin, 1.0f));
                    const glm::vec3 localDir =
                        glm::vec3(invWorld * glm::vec4(rayDir, 0.0f));

                    glm::vec3 half;
                    switch (collider.shapeType) {
                        case Galeshi::ColliderShapeType::Sphere:
                            half = glm::vec3(collider.radius);
                            break;
                        case Galeshi::ColliderShapeType::Capsule:
                            half = glm::vec3(collider.radius,
                                             collider.halfHeight + collider.radius,
                                             collider.radius);
                            break;
                        case Galeshi::ColliderShapeType::Box:
                        default:
                            half = collider.halfExtents;
                            break;
                    }
                    const glm::vec3 minB = collider.offset - half;
                    const glm::vec3 maxB = collider.offset + half;

                    float tmin = -std::numeric_limits<float>::infinity();
                    float tmax =  std::numeric_limits<float>::infinity();
                    bool miss = false;
                    for (int axis = 0; axis < 3 && !miss; ++axis) {
                        if (std::abs(localDir[axis]) < 1e-8f) {
                            if (localOrigin[axis] < minB[axis] ||
                                localOrigin[axis] > maxB[axis]) {
                                miss = true;
                            }
                        } else {
                            float t1 = (minB[axis] - localOrigin[axis]) / localDir[axis];
                            float t2 = (maxB[axis] - localOrigin[axis]) / localDir[axis];
                            if (t1 > t2) std::swap(t1, t2);
                            tmin = std::max(tmin, t1);
                            tmax = std::min(tmax, t2);
                            if (tmin > tmax) miss = true;
                        }
                    }
                    if (miss || tmax < 0.0f) continue;

                    const float tHit = tmin > 0.0f ? tmin : tmax;
                    const glm::vec3 localHit = localOrigin + localDir * tHit;
                    const glm::vec3 worldHit =
                        glm::vec3(world * glm::vec4(localHit, 1.0f));
                    const float worldDist = glm::length(worldHit - rayOrigin);
                    if (worldDist < bestDist) {
                        bestDist = worldDist;
                        bestHit = Galeshi::Entity(e, &m_scene);
                    }
                }

                if (bestHit) m_editor.Select(bestHit);
                else m_editor.ClearSelection();


            }

                    }
        ImGui::End();
    }

    struct LandscapeImage {
        int width = 0;
        int height = 0;
        std::vector<uint8_t> rgba;
        bool Valid() const { return width > 0 && height > 0 && !rgba.empty(); }
    };

    // shortcut: layer sources are decoded with stb (PNG/JPEG/BMP/TGA).
    // DDS/KTX layer maps fall back to the base texture. Upgrade this to the
    // engine's bimg container decoder if compressed layer sources are needed.
    bool LoadLandscapeImage(const std::string& path, LandscapeImage& out) {
        out = {};
        if (path.empty()) return false;
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_set_flip_vertically_on_load(false);
        unsigned char* pixels = stbi_load(
            path.c_str(), &width, &height, &channels, 4);
        if (!pixels) return false;
        out.width = width;
        out.height = height;
        out.rgba.assign(pixels,
                        pixels + static_cast<size_t>(width) * height * 4);
        stbi_image_free(pixels);
        return out.Valid();
    }

    static glm::vec4 SampleLandscapeImage(
        const LandscapeImage& image, float u, float v,
        const glm::vec4& fallback) {
        if (!image.Valid()) return fallback;
        u = glm::clamp(u, 0.0f, 1.0f);
        v = glm::clamp(v, 0.0f, 1.0f);
        const float x = u * static_cast<float>(image.width - 1);
        const float y = v * static_cast<float>(image.height - 1);
        const int x0 = static_cast<int>(std::floor(x));
        const int y0 = static_cast<int>(std::floor(y));
        const int x1 = std::min(x0 + 1, image.width - 1);
        const int y1 = std::min(y0 + 1, image.height - 1);
        const float fx = x - static_cast<float>(x0);
        const float fy = y - static_cast<float>(y0);
        const auto sample = [&](int px, int py) {
            const size_t index =
                (static_cast<size_t>(py) * image.width + px) * 4;
            return glm::vec4(
                static_cast<float>(image.rgba[index + 0]) / 255.0f,
                static_cast<float>(image.rgba[index + 1]) / 255.0f,
                static_cast<float>(image.rgba[index + 2]) / 255.0f,
                static_cast<float>(image.rgba[index + 3]) / 255.0f);
        };
        return glm::mix(
            glm::mix(sample(x0, y0), sample(x1, y0), fx),
            glm::mix(sample(x0, y1), sample(x1, y1), fx), fy);
    }

    static float SampleLandscapeWeight(
        const std::vector<float>& weights, int sizeX, int sizeZ,
        float x, float z) {
        if (weights.empty() || sizeX < 2 || sizeZ < 2) return 0.0f;
        x = glm::clamp(x, 0.0f, static_cast<float>(sizeX - 1));
        z = glm::clamp(z, 0.0f, static_cast<float>(sizeZ - 1));
        const int x0 = static_cast<int>(std::floor(x));
        const int z0 = static_cast<int>(std::floor(z));
        const int x1 = std::min(x0 + 1, sizeX - 1);
        const int z1 = std::min(z0 + 1, sizeZ - 1);
        const float fx = x - static_cast<float>(x0);
        const float fz = z - static_cast<float>(z0);
        const auto sample = [&](int px, int pz) {
            return weights[static_cast<size_t>(pz) * sizeX + px];
        };
        return glm::mix(
            glm::mix(sample(x0, z0), sample(x1, z0), fx),
            glm::mix(sample(x0, z1), sample(x1, z1), fx), fz);
    }

    static bool SaveLandscapePng(const std::filesystem::path& path,
                                 const LandscapeImage& image) {
        if (!image.Valid()) return false;
        png_image png{};
        png.version = PNG_IMAGE_VERSION;
        png.width = static_cast<png_uint_32>(image.width);
        png.height = static_cast<png_uint_32>(image.height);
        png.format = PNG_FORMAT_RGBA;
        return png_image_write_to_file(
                   &png, path.string().c_str(), 0, image.rgba.data(), 0,
                   nullptr) != 0;
    }

    static std::string SafeLandscapeTextureName(const std::string& name) {
        std::string safe;
        for (char c : name) {
            safe.push_back(
                std::isalnum(static_cast<unsigned char>(c)) ? c : '_');
        }
        if (safe.empty()) safe = "Landscape";
        return safe;
    }

    bool BakeLandscapeTextures(Galeshi::Entity entity) {
        if (!entity || !entity.HasComponent<Galeshi::LandscapeComponent>()) {
            return false;
        }
        auto& landscape =
            entity.GetComponent<Galeshi::LandscapeComponent>();
        auto& registry = m_scene.Registry();
        auto* materialComponent =
            registry.try_get<Galeshi::MaterialComponent>(entity.Handle());
        if (!materialComponent) {
            materialComponent =
                &entity.AddComponent<Galeshi::MaterialComponent>();
        }
        if (!materialComponent->material) {
            materialComponent->material = std::make_shared<Galeshi::Material>();
        }
        auto& material = materialComponent->material;

        // Capture the untouched base maps once, before the generated maps
        // become the material's visible texture slots.
        if (!landscape.baseCaptured &&
            !landscape.bakedAlbedoPath.empty()) {
            // An older save may already point the material at baked maps.
            // Do not feed those back as the base.
            landscape.baseCaptured = true;
        }
        if (!landscape.baseCaptured) {
            const auto usable = [](const std::shared_ptr<Galeshi::Texture>& t) {
                return t && t->Path() != "<white>";
            };
            if (usable(material->albedoMap)) {
                landscape.baseAlbedoPath = material->albedoMap->Path();
            }
            if (usable(material->metallicRoughnessMap)) {
                landscape.baseOrmPath =
                    material->metallicRoughnessMap->Path();
            }
            if (usable(material->metallicMap)) {
                landscape.baseMetallicPath =
                    material->metallicMap->Path();
            }
            if (usable(material->roughnessMap)) {
                landscape.baseRoughnessPath =
                    material->roughnessMap->Path();
            }
            if (usable(material->occlusionMap) &&
                (!material->metallicRoughnessMap ||
                 material->occlusionMap !=
                     material->metallicRoughnessMap)) {
                landscape.baseOcclusionPath =
                    material->occlusionMap->Path();
            }
            if (usable(material->normalMap)) {
                landscape.baseNormalPath = material->normalMap->Path();
            }
            landscape.baseCaptured = true;
        }

        LandscapeImage baseAlbedo;
        LandscapeImage baseOrm;
        LandscapeImage baseMetallic;
        LandscapeImage baseRoughness;
        LandscapeImage baseOcclusion;
        LandscapeImage baseNormal;
        LoadLandscapeImage(landscape.baseAlbedoPath, baseAlbedo);
        LoadLandscapeImage(landscape.baseOrmPath, baseOrm);
        LoadLandscapeImage(landscape.baseMetallicPath, baseMetallic);
        LoadLandscapeImage(landscape.baseRoughnessPath, baseRoughness);
        LoadLandscapeImage(landscape.baseOcclusionPath, baseOcclusion);
        LoadLandscapeImage(landscape.baseNormalPath, baseNormal);

        std::vector<LandscapeImage> layerAlbedo(landscape.layers.size());
        std::vector<LandscapeImage> layerOrm(landscape.layers.size());
        std::vector<LandscapeImage> layerNormal(landscape.layers.size());
        for (size_t i = 0; i < landscape.layers.size(); ++i) {
            const auto& layer = landscape.layers[i];
            if (layer.albedoMap) {
                LoadLandscapeImage(layer.albedoMap->Path(), layerAlbedo[i]);
            }
            if (layer.ormMap) {
                LoadLandscapeImage(layer.ormMap->Path(), layerOrm[i]);
            }
            if (layer.normalMap) {
                LoadLandscapeImage(layer.normalMap->Path(), layerNormal[i]);
            }
        }
        if (landscape.layerWeights.size() != landscape.layers.size()) {
            landscape.layerWeights.resize(landscape.layers.size());
        }
        for (auto& weights : landscape.layerWeights) {
            if (weights.size() !=
                static_cast<size_t>(landscape.sizeX) *
                    static_cast<size_t>(landscape.sizeZ)) {
                weights.resize(
                    static_cast<size_t>(landscape.sizeX) *
                        static_cast<size_t>(landscape.sizeZ), 0.0f);
            }
        }

        const int outputSize = std::clamp(
            std::max(landscape.sizeX, landscape.sizeZ) * 4, 256, 2048);
        LandscapeImage outputAlbedo;
        LandscapeImage outputOrm;
        LandscapeImage outputNormal;
        outputAlbedo.width = outputOrm.width = outputNormal.width =
            outputSize;
        outputAlbedo.height = outputOrm.height = outputNormal.height =
            outputSize;
        outputAlbedo.rgba.resize(
            static_cast<size_t>(outputSize) * outputSize * 4);
        outputOrm.rgba.resize(outputAlbedo.rgba.size());
        outputNormal.rgba.resize(outputAlbedo.rgba.size());

        const glm::vec4 white(1.0f);
        const glm::vec4 flatNormal(0.5f, 0.5f, 1.0f, 1.0f);
        const size_t expectedWeights =
            static_cast<size_t>(landscape.sizeX) * landscape.sizeZ;
        std::vector<float> weights(landscape.layers.size(), 0.0f);
        for (int y = 0; y < outputSize; ++y) {
            // The engine uploads source textures flipped (bottom-left V), so
            // write the baked rows in the same orientation.
            const float v = 1.0f -
                            (static_cast<float>(y) + 0.5f) /
                                static_cast<float>(outputSize);
            const float gz = v * static_cast<float>(landscape.sizeZ);
            for (int x = 0; x < outputSize; ++x) {
                const float u = (static_cast<float>(x) + 0.5f) /
                                static_cast<float>(outputSize);
                const float gx = u * static_cast<float>(landscape.sizeX);
                float layerWeightSum = 0.0f;
                std::fill(weights.begin(), weights.end(), 0.0f);
                for (size_t i = 0; i < weights.size(); ++i) {
                    if (i >= landscape.layerWeights.size() ||
                        landscape.layerWeights[i].size() !=
                            expectedWeights) {
                        continue;
                    }
                    weights[i] = SampleLandscapeWeight(
                        landscape.layerWeights[i], landscape.sizeX,
                        landscape.sizeZ, gx, gz);
                    layerWeightSum += weights[i];
                }
                float baseWeight =
                    std::max(0.0f, 1.0f - layerWeightSum);
                float totalWeight = baseWeight + layerWeightSum;
                if (totalWeight < 1e-5f) {
                    baseWeight = 1.0f;
                    totalWeight = 1.0f;
                }
                const float invTotal = 1.0f / totalWeight;

                const glm::vec4 baseAlbedoColor =
                    SampleLandscapeImage(baseAlbedo, u, v, white);
                glm::vec4 baseOrmColor =
                    SampleLandscapeImage(baseOrm, u, v, white);
                if (baseMetallic.Valid()) {
                    baseOrmColor.b = SampleLandscapeImage(
                        baseMetallic, u, v, white).r;
                }
                if (baseRoughness.Valid()) {
                    baseOrmColor.g = SampleLandscapeImage(
                        baseRoughness, u, v, white).r;
                }
                if (baseOcclusion.Valid()) {
                    baseOrmColor.r = SampleLandscapeImage(
                        baseOcclusion, u, v, white).r;
                }
                const glm::vec4 baseNormalColor =
                    SampleLandscapeImage(baseNormal, u, v, flatNormal);
                glm::vec4 albedo = baseAlbedoColor * baseWeight;
                glm::vec4 orm = baseOrmColor * baseWeight;
                glm::vec3 normal =
                    glm::vec3(baseNormalColor * 2.0f - 1.0f) *
                    baseWeight;
                for (size_t i = 0; i < weights.size(); ++i) {
                    if (weights[i] <= 1e-6f) continue;
                    const glm::vec4 layerAlbedoColor =
                        SampleLandscapeImage(
                            layerAlbedo[i], u, v, baseAlbedoColor);
                    const glm::vec4 layerOrmColor =
                        SampleLandscapeImage(
                            layerOrm[i], u, v, baseOrmColor);
                    const glm::vec4 layerNormalColor =
                        SampleLandscapeImage(
                            layerNormal[i], u, v, baseNormalColor);
                    albedo += layerAlbedoColor * weights[i];
                    orm += layerOrmColor * weights[i];
                    normal += glm::vec3(layerNormalColor * 2.0f - 1.0f) *
                              weights[i];
                }
                albedo *= invTotal;
                orm *= invTotal;
                normal *= invTotal;
                const float normalLength = glm::length(normal);
                normal = normalLength > 1e-5f
                    ? normal / normalLength
                    : glm::vec3(0.0f, 0.0f, 1.0f);
                const glm::vec4 encodedNormal =
                    glm::vec4(normal * 0.5f + 0.5f, 1.0f);
                const size_t index =
                    (static_cast<size_t>(y) * outputSize + x) * 4;
                const auto writeColor =
                    [&](LandscapeImage& target, const glm::vec4& color) {
                        target.rgba[index + 0] = static_cast<uint8_t>(
                            glm::clamp(color.r, 0.0f, 1.0f) * 255.0f);
                        target.rgba[index + 1] = static_cast<uint8_t>(
                            glm::clamp(color.g, 0.0f, 1.0f) * 255.0f);
                        target.rgba[index + 2] = static_cast<uint8_t>(
                            glm::clamp(color.b, 0.0f, 1.0f) * 255.0f);
                        target.rgba[index + 3] = static_cast<uint8_t>(
                            glm::clamp(color.a, 0.0f, 1.0f) * 255.0f);
                    };
                writeColor(outputAlbedo,
                           glm::vec4(albedo.r, albedo.g, albedo.b, 1.0f));
                writeColor(outputOrm,
                           glm::vec4(orm.r, orm.g, orm.b, 1.0f));
                writeColor(outputNormal, encodedNormal);
            }
        }

        std::filesystem::path texturesDir =
            std::filesystem::path(Galeshi::ProjectManager::GetProjectPath()) /
            "assets/textures";
        if (Galeshi::ProjectManager::GetProjectPath().empty()) {
            texturesDir =
                std::filesystem::current_path() / "assets/textures";
        }
        std::error_code ec;
        std::filesystem::create_directories(texturesDir, ec);
        const std::string entityName =
            entity.HasComponent<Galeshi::NameComponent>()
                ? entity.GetComponent<Galeshi::NameComponent>().name
                : std::string("Landscape");
        std::string baseName;
        std::string existingBaked =
            !landscape.bakedAlbedoPath.empty()
                ? landscape.bakedAlbedoPath
                : landscape.bakedOrmPath;
        // A duplicated landscape copies its baked paths. Do not let both
        // entities write the same files; give the copy its own suffix.
        bool bakedPathsShared = false;
        if (!existingBaked.empty()) {
            for (auto other :
                 registry.view<Galeshi::LandscapeComponent>()) {
                if (other == entity.Handle()) continue;
                const auto& otherLandscape =
                    registry.get<Galeshi::LandscapeComponent>(other);
                if ((!otherLandscape.bakedAlbedoPath.empty() &&
                     otherLandscape.bakedAlbedoPath ==
                         landscape.bakedAlbedoPath) ||
                    (!otherLandscape.bakedOrmPath.empty() &&
                     otherLandscape.bakedOrmPath ==
                         landscape.bakedOrmPath) ||
                    (!otherLandscape.bakedNormalPath.empty() &&
                     otherLandscape.bakedNormalPath ==
                         landscape.bakedNormalPath)) {
                    bakedPathsShared = true;
                    break;
                }
            }
        }
        if (!existingBaked.empty() && !bakedPathsShared) {
            std::string stem =
                std::filesystem::path(existingBaked).stem().string();
            for (const std::string& suffix :
                 {std::string("_albedo"), std::string("_orm"),
                  std::string("_normal")}) {
                if (stem.size() > suffix.size() &&
                    stem.compare(stem.size() - suffix.size(),
                                 suffix.size(), suffix) == 0) {
                    stem.resize(stem.size() - suffix.size());
                    break;
                }
            }
            baseName = SafeLandscapeTextureName(stem);
        }
        if (baseName.empty()) {
            std::string scenePrefix;
            if (!m_currentScenePath.empty()) {
                scenePrefix = SafeLandscapeTextureName(
                    std::filesystem::path(m_currentScenePath)
                        .stem()
                        .string()) +
                    "_";
            }
            baseName =
                SafeLandscapeTextureName(scenePrefix + entityName);
            std::error_code existsEc;
            bool nameTaken =
                std::filesystem::exists(
                    texturesDir / (baseName + "_albedo.png"), existsEc) ||
                std::filesystem::exists(
                    texturesDir / (baseName + "_orm.png"), existsEc) ||
                std::filesystem::exists(
                    texturesDir / (baseName + "_normal.png"), existsEc);
            if (bakedPathsShared || nameTaken) {
                baseName += "_" +
                    std::to_string(entt::to_integral(entity.Handle()));
            }
        }
        const std::filesystem::path albedoPath =
            texturesDir / (baseName + "_albedo.png");
        const std::filesystem::path ormPath =
            texturesDir / (baseName + "_orm.png");
        const std::filesystem::path normalPath =
            texturesDir / (baseName + "_normal.png");
        if (!SaveLandscapePng(albedoPath, outputAlbedo) ||
            !SaveLandscapePng(ormPath, outputOrm) ||
            !SaveLandscapePng(normalPath, outputNormal)) {
            spdlog::error("[Landscape] Failed to write baked textures to {}",
                          texturesDir.string());
            return false;
        }
        auto loadGenerated = [](const std::filesystem::path& path,
                                bool srgb) {
            return std::shared_ptr<Galeshi::Texture>(
                Galeshi::Texture::LoadFromFile(path.string(), srgb, true));
        };
        auto generatedAlbedo = loadGenerated(albedoPath, true);
        auto generatedOrm = loadGenerated(ormPath, false);
        auto generatedNormal = loadGenerated(normalPath, false);
        if (!generatedAlbedo || !generatedOrm || !generatedNormal) {
            spdlog::error(
                "[Landscape] Baked files were written but could not be "
                "loaded from {}",
                texturesDir.string());
            return false;
        }
        material->albedoMap = std::move(generatedAlbedo);
        material->metallicRoughnessMap = std::move(generatedOrm);
        material->normalMap = std::move(generatedNormal);
        material->occlusionMap = material->metallicRoughnessMap;
        // The generated ORM map owns metallic/roughness now; leaving the
        // original split maps bound would make the shader ignore it.
        material->metallicMap.reset();
        material->roughnessMap.reset();
        material->albedoSrgb = true;
        material->metallicRoughnessSrgb = false;
        material->normalSrgb = false;
        material->occlusionSrgb = false;
        material->landscapeBlend = false;
        material->landscapeAlbedoMap.reset();
        material->landscapeOrmMap.reset();
        material->landscapeNormalMap.reset();
        material->sceneOverride = true;
        landscape.bakedAlbedoPath = albedoPath.string();
        landscape.bakedOrmPath = ormPath.string();
        landscape.bakedNormalPath = normalPath.string();
        spdlog::info("[Landscape] Baked layer textures to {}",
                     texturesDir.string());
        return true;
    }

    // -------------------------------------------------------------------
    // Details
    // -------------------------------------------------------------------
    void DrawTextureSlot(const char* label, std::shared_ptr<Galeshi::Texture>& slot,
                         bool srgb, bool* srgbFlag = nullptr,
                         bool* changed = nullptr) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);

        const auto acceptTextureDrop = [&]() {
            if (!ImGui::BeginDragDropTarget()) return;
            if (const ImGuiPayload* payload =
                    ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                const char* path = static_cast<const char*>(payload->Data);
                std::string p = path;
                auto dotPos = p.find_last_of('.');
                std::string ext =
                    (dotPos != std::string::npos) ? p.substr(dotPos) : "";
                for (auto& c : ext) {
                    c = static_cast<char>(
                        std::tolower(static_cast<unsigned char>(c)));
                }
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".bmp" || ext == ".tga" || ext == ".dds" ||
                    ext == ".ktx") {
                    if (changed) *changed = true;
                    PushHistory();
                    slot = m_assets->LoadTexture(
                        p, srgbFlag ? *srgbFlag : srgb);
                }
            }
            ImGui::EndDragDropTarget();
        };

        if (slot && bgfx::isValid(slot->Handle())) {
            ImTextureID texId = static_cast<ImTextureID>(slot->Handle().idx);
            // Engine textures are uploaded with a bottom-left V origin (the
            // Blender/UV convention); ImGui displays top-left, so flip only
            // the preview quad.
            ImGui::Image(texId, ImVec2(64, 64),
                         ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));
            acceptTextureDrop();
            ImGui::SameLine();
            ImGui::BeginGroup();
            std::string path = slot->Path();
            auto slash = path.find_last_of('/');
            std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            ImGui::TextWrapped("%s", name.c_str());
            if (ImGui::Button("Clear")) {
                if (changed) *changed = true;
                PushHistory();
                slot.reset();
            }
            ImGui::EndGroup();
        } else {
            ImGui::Button("Empty (drop texture here)", ImVec2(200, 64));
            acceptTextureDrop();
        }

        // Every material map exposes its color space. Albedo starts enabled
        // and data maps start linear; the user can override any of them.
        bool value = srgbFlag ? *srgbFlag : srgb;
        if (!srgbFlag) ImGui::BeginDisabled();
        if (ImGui::Checkbox("sRGB", &value) && srgbFlag) {
            if (changed) *changed = true;
            PushHistory();
            *srgbFlag = value;
            if (slot) {
                const std::string texturePath = slot->Path();
                if (!texturePath.empty()) {
                    auto reloaded = m_assets->LoadTexture(
                        texturePath, value, nullptr,
                        slot->FlippedVertically());
                    // Keep the current upload if the requested variant cannot
                    // be produced (for example a missing external file).
                    if (reloaded && reloaded != m_assets->WhiteTexture()) {
                        slot = reloaded;
                    }
                }
            }
        }
        if (!srgbFlag) ImGui::EndDisabled();
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
            ImGui::SetTooltip(
                "Decode this texture as sRGB. Disable for linear data maps.");
        }

        ImGui::PopID();
    }

    bool HasSkeletalParent(entt::entity handle) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle) ||
            !reg.all_of<Galeshi::ParentComponent>(handle)) {
            return false;
        }
        const entt::entity parent =
            reg.get<Galeshi::ParentComponent>(handle).parent;
        return reg.valid(parent) &&
               reg.all_of<Galeshi::SkeletalComponent>(parent);
    }

    bool HasCollisionShapeParent(entt::entity handle) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle) ||
            !reg.all_of<Galeshi::ParentComponent>(handle)) {
            return false;
        }
        const entt::entity parent =
            reg.get<Galeshi::ParentComponent>(handle).parent;
        return reg.valid(parent) &&
               reg.all_of<Galeshi::ColliderComponent>(parent) &&
               !reg.all_of<Galeshi::SkeletalComponent>(parent);
    }

    void SetLocalTransformFromWorld(entt::entity handle,
                                    const glm::mat4& world) {
        auto& reg = m_scene.Registry();
        if (!reg.all_of<Galeshi::TransformComponent>(handle)) return;
        auto& tf = reg.get<Galeshi::TransformComponent>(handle);
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        if (glm::decompose(world, scale, rotation, translation, skew,
                           perspective)) {
            tf.position = translation;
            tf.rotation = glm::normalize(rotation);
            tf.scale = scale;
        } else {
            tf.position = glm::vec3(world[3]);
        }
    }

    // Detach is a collision-shape feature: a child can leave a collision
    // shape parent, but never a skeletal parent. The world transform is kept.
    void DetachChildFromCollisionShape(entt::entity child) {
        auto& reg = m_scene.Registry();
        if (!HasCollisionShapeParent(child)) return;

        PushHistory();
        const glm::mat4 world = EntityWorldMatrix(child);
        reg.remove<Galeshi::ParentComponent>(child);
        SetLocalTransformFromWorld(child, world);
        spdlog::info("[Outliner] Detached child {} from collision shape",
                     static_cast<uint32_t>(child));
    }

    void DetachCollisionShapeChildren(entt::entity parent) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(parent) ||
            !reg.all_of<Galeshi::ColliderComponent>(parent) ||
            reg.all_of<Galeshi::SkeletalComponent>(parent)) {
            return;
        }

        std::vector<std::pair<entt::entity, glm::mat4>> children;
        auto childView = reg.view<Galeshi::ParentComponent>();
        for (auto child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent != parent)
                continue;
            glm::mat4 world(1.0f);
            if (reg.all_of<Galeshi::TransformComponent>(child))
                world = EntityWorldMatrix(child);
            children.emplace_back(child, world);
        }
        if (children.empty()) return;

        PushHistory();
        for (const auto& [child, world] : children) {
            if (!reg.valid(child)) continue;
            reg.remove<Galeshi::ParentComponent>(child);
            SetLocalTransformFromWorld(child, world);
        }
        spdlog::info("[Outliner] Detached {} children from collision shape {}",
                     children.size(), static_cast<uint32_t>(parent));
    }

    bool IsDescendantOf(entt::entity node, entt::entity ancestor) {
        auto& reg = m_scene.Registry();
        for (int depth = 0; reg.valid(node) && depth < 128; ++depth) {
            if (node == ancestor) return true;
            if (!reg.all_of<Galeshi::ParentComponent>(node)) break;
            node = reg.get<Galeshi::ParentComponent>(node).parent;
        }
        return false;
    }

    void DetachFromParent(entt::entity child) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(child) ||
            !reg.all_of<Galeshi::ParentComponent>(child) ||
            HasSkeletalParent(child)) {
            return;
        }
        PushHistory();
        const glm::mat4 world = EntityWorldMatrix(child);
        reg.remove<Galeshi::ParentComponent>(child);
        SetLocalTransformFromWorld(child, world);
        spdlog::info("[Outliner] Detached entity {} from its parent",
                     static_cast<uint32_t>(child));
    }

    void DetachChildren(entt::entity parent) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(parent) ||
            reg.all_of<Galeshi::SkeletalComponent>(parent)) {
            return;
        }
        std::vector<std::pair<entt::entity, glm::mat4>> children;
        auto childView = reg.view<Galeshi::ParentComponent>();
        for (auto child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent != parent)
                continue;
            if (reg.all_of<Galeshi::NameComponent>(child)) {
                const auto& name = reg.get<Galeshi::NameComponent>(child).name;
                if (name.rfind("__material_slot__", 0) == 0) continue;
            }
            glm::mat4 world(1.0f);
            if (reg.all_of<Galeshi::TransformComponent>(child))
                world = EntityWorldMatrix(child);
            children.emplace_back(child, world);
        }
        if (children.empty()) return;

        PushHistory();
        for (const auto& [child, world] : children) {
            if (!reg.valid(child) || HasSkeletalParent(child)) continue;
            reg.remove<Galeshi::ParentComponent>(child);
            SetLocalTransformFromWorld(child, world);
        }
        spdlog::info("[Outliner] Detached {} children from entity {}",
                     children.size(), static_cast<uint32_t>(parent));
    }

    void AttachSelectedTo(entt::entity parent) {
        auto& reg = m_scene.Registry();
        auto selected = m_editor.SelectedAll(&m_scene);
        if (!reg.valid(parent) || selected.size() != 1) return;
        const entt::entity child = selected.front().Handle();
        if (!reg.valid(child) || child == parent ||
            HasSkeletalParent(child) || IsDescendantOf(parent, child)) {
            return;
        }

        PushHistory();
        const glm::mat4 world = EntityWorldMatrix(child);
        reg.emplace_or_replace<Galeshi::ParentComponent>(
            child, Galeshi::ParentComponent{parent});
        SetLocalTransformFromWorld(
            child, glm::inverse(EntityWorldMatrix(parent)) * world);
        spdlog::info("[Outliner] Attached entity {} to {}",
                     static_cast<uint32_t>(child),
                     static_cast<uint32_t>(parent));
    }

    void AttachPrimitiveMesh(entt::entity parent, bool sphere) {
        if (!m_scene.Registry().valid(parent)) return;
        PushHistory();
        auto child = sphere
            ? MakeSphere("MeshChild", glm::vec3(0.0f))
            : MakeCube("MeshChild", glm::vec3(0.0f));
        auto& reg = m_scene.Registry();
        reg.emplace_or_replace<Galeshi::ParentComponent>(
            child.Handle(), Galeshi::ParentComponent{parent});
        if (reg.all_of<Galeshi::TransformComponent>(child.Handle())) {
            auto& tf = reg.get<Galeshi::TransformComponent>(child.Handle());
            tf.position = glm::vec3(0.0f);
            tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            tf.scale = glm::vec3(1.0f);
        }
        m_editor.Select(child);
    }

    bool MeshEntityWorldBounds(entt::entity handle, glm::vec3& outCenter,
                               glm::vec3& outExtents) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle)) return false;
        const auto* meshComponent =
            reg.try_get<Galeshi::MeshComponent>(handle);
        if (!meshComponent || !meshComponent->mesh) return false;

        glm::vec3 localMin, localMax;
        meshComponent->mesh->LocalAABB(localMin, localMax);
        const glm::mat4 world = EntityWorldMatrix(handle);
        glm::vec3 worldMin(std::numeric_limits<float>::max());
        glm::vec3 worldMax(std::numeric_limits<float>::lowest());
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local{
                (corner & 1) ? localMax.x : localMin.x,
                (corner & 2) ? localMax.y : localMin.y,
                (corner & 4) ? localMax.z : localMin.z};
            const glm::vec3 point = glm::vec3(world * glm::vec4(local, 1.0f));
            worldMin = glm::min(worldMin, point);
            worldMax = glm::max(worldMax, point);
        }
        outCenter = (worldMin + worldMax) * 0.5f;
        outExtents = (worldMax - worldMin) * 0.5f;
        return true;
    }

    void AccumulateEntityWorldBounds(entt::entity handle, glm::vec3& outMin,
                                     glm::vec3& outMax, int depth) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(handle) || depth > 128) return;

        glm::vec3 center, extents;
        if (MeshEntityWorldBounds(handle, center, extents)) {
            outMin = glm::min(outMin, center - extents);
            outMax = glm::max(outMax, center + extents);
        } else if (const auto* collider =
                       reg.try_get<Galeshi::ColliderComponent>(handle)) {
            glm::vec3 half;
            switch (collider->shapeType) {
                case Galeshi::ColliderShapeType::Sphere:
                    half = glm::vec3(collider->radius);
                    break;
                case Galeshi::ColliderShapeType::Capsule:
                    half = glm::vec3(collider->radius,
                                     collider->halfHeight + collider->radius,
                                     collider->radius);
                    break;
                case Galeshi::ColliderShapeType::Box:
                default:
                    half = collider->halfExtents;
                    break;
            }
            const glm::mat4 world = EntityWorldMatrix(handle);
            const glm::vec3 centerWorld =
                glm::vec3(world * glm::vec4(collider->offset, 1.0f));
            const glm::vec3 scale{
                glm::length(glm::vec3(world[0])),
                glm::length(glm::vec3(world[1])),
                glm::length(glm::vec3(world[2]))};
            const glm::vec3 worldHalf = half * scale;
            outMin = glm::min(outMin, centerWorld - worldHalf);
            outMax = glm::max(outMax, centerWorld + worldHalf);
        }

        auto childView = reg.view<Galeshi::ParentComponent>();
        for (entt::entity child : childView) {
            if (childView.get<Galeshi::ParentComponent>(child).parent == handle) {
                AccumulateEntityWorldBounds(child, outMin, outMax, depth + 1);
            }
        }
    }

    bool EntityWorldBounds(entt::entity handle, glm::vec3& outCenter,
                           glm::vec3& outExtents) {
        glm::vec3 min(std::numeric_limits<float>::max());
        glm::vec3 max(std::numeric_limits<float>::lowest());
        AccumulateEntityWorldBounds(handle, min, max, 0);
        if (min.x > max.x || min.y > max.y || min.z > max.z) {
            auto& reg = m_scene.Registry();
            if (!reg.valid(handle) ||
                !reg.all_of<Galeshi::TransformComponent>(handle)) {
                return false;
            }
            outCenter = glm::vec3(EntityWorldMatrix(handle)[3]);
            outExtents = glm::vec3(0.5f);
            return true;
        }
        outCenter = (min + max) * 0.5f;
        outExtents = (max - min) * 0.5f;
        return true;
    }

    void FocusCameraOnEntity(entt::entity handle) {
        glm::vec3 center, extents;
        if (!EntityWorldBounds(handle, center, extents)) return;
        m_editor.camTarget = center;
        m_editor.camRadius =
            glm::clamp(glm::length(extents) * 2.5f, 1.0f, 200.0f);
        m_editor.Select(Galeshi::Entity(handle, &m_scene));
    }

    void AttachCollisionShape(entt::entity parent,
                              Galeshi::ColliderShapeType type) {
        if (!m_scene.Registry().valid(parent)) return;
        auto child = SpawnCollisionShape("CollisionShape", type);
        auto& reg = m_scene.Registry();
        reg.emplace_or_replace<Galeshi::ParentComponent>(
            child.Handle(), Galeshi::ParentComponent{parent});
        if (reg.all_of<Galeshi::TransformComponent>(child.Handle())) {
            auto& tf = reg.get<Galeshi::TransformComponent>(child.Handle());
            tf.position = glm::vec3(0.0f);
            tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            tf.scale = glm::vec3(1.0f);
        }
        m_editor.Select(child);
    }

        void DrawOutliner() {
        ImGui::Begin("World Outliner");
        auto& reg = m_scene.Registry();

        // ── Helper: compute world matrix by walking up the parent chain ──
        auto GetWorldMatrix = [&](entt::entity ent) -> glm::mat4 {
            glm::mat4 world(1.0f);
            std::vector<entt::entity> chain;
            entt::entity curr = ent;
            int depth = 0;
            while (curr != entt::null && depth++ < 128) {
                chain.push_back(curr);
                if (reg.all_of<Galeshi::ParentComponent>(curr))
                    curr = reg.get<Galeshi::ParentComponent>(curr).parent;
                else
                    break;
            }
            for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
                if (reg.all_of<Galeshi::TransformComponent>(*it))
                    world = world * reg.get<Galeshi::TransformComponent>(*it).LocalMatrix();
            }
            return world;
        };

        // ── Helper: decompose matrix into TRS and write to TransformComponent ──
        auto SetLocalFromMatrix = [&](entt::entity ent, const glm::mat4& m) {
            if (!reg.all_of<Galeshi::TransformComponent>(ent)) return;
            auto& tf = reg.get<Galeshi::TransformComponent>(ent);

            glm::vec3 pos(m[3]);
            glm::vec3 c0(m[0]), c1(m[1]), c2(m[2]);
            glm::vec3 scl(glm::length(c0), glm::length(c1), glm::length(c2));

            // Avoid divide-by-zero
            float sx = (scl.x > 1e-6f) ? scl.x : 1.0f;
            float sy = (scl.y > 1e-6f) ? scl.y : 1.0f;
            float sz = (scl.z > 1e-6f) ? scl.z : 1.0f;

            glm::mat3 rotMat(c0 / sx, c1 / sy, c2 / sz);
            glm::quat rot = glm::quat_cast(rotMat);

            tf.position = pos;
            tf.rotation = rot;
            tf.scale    = scl;
        };

        // Imported models are stored as an empty root with one mesh child per
        // material submesh. Those children only add noise to the Outliner, so
        // show the model root by itself. Skeletal roots can own non-mesh
        // children as well, so recognise them explicitly.
        auto IsModelRoot = [&](entt::entity root,
                               const std::vector<entt::entity>& children) {
            if (reg.all_of<Galeshi::SkeletalComponent>(root)) return true;
            // Collision and physics shapes are real hierarchy nodes, not
            // imported-model wrappers. Keep their attached meshes visible so
            // each child can be selected and detached independently.
            if (reg.all_of<Galeshi::ColliderComponent>(root) ||
                reg.all_of<Galeshi::RigidBodyComponent>(root)) {
                return false;
            }
            if (children.empty() || reg.all_of<Galeshi::MeshComponent>(root))
                return false;
            for (entt::entity child : children) {
                if (!reg.all_of<Galeshi::MeshComponent>(child)) return false;
                if (reg.get<Galeshi::MeshComponent>(child).sourceAssetPath.empty())
                    return false;
            }
            return true;
        };

        std::function<void(entt::entity, int)> DrawNode = [&](entt::entity e, int depth) {
            if (!reg.valid(e)) return;

            std::string name = "Entity " + std::to_string((uint32_t)e);
            if (reg.all_of<Galeshi::NameComponent>(e))
                name = reg.get<Galeshi::NameComponent>(e).name;
            if (reg.all_of<Galeshi::SkeletalComponent>(e))
                name = "[Skeletal] " + name;

            ImGui::PushID((uint32_t)e);

            // Snapshot children before drawing (avoids iterator invalidation during drag&drop)
            std::vector<entt::entity> children;
            reg.view<Galeshi::ParentComponent>().each([&](auto child, auto& pc) {
                if (pc.parent != e) return;
                // Material-only placeholders are Details slots, not model
                // hierarchy nodes; keep them out of the Outliner.
                if (reg.all_of<Galeshi::NameComponent>(child)) {
                    const auto& childName =
                        reg.get<Galeshi::NameComponent>(child).name;
                    if (childName.rfind("__material_slot__", 0) == 0) return;
                }
                children.push_back(child);
            });
            if (IsModelRoot(e, children)) {
                // Imported model roots collapse their own mesh submeshes, but
                // must keep non-mesh attachments visible. In particular, a
                // skeletal character's collision shape must remain a separate
                // Outliner node that can be selected and detached.
                children.erase(
                    std::remove_if(
                        children.begin(), children.end(),
                        [&](entt::entity child) {
                            if (!reg.all_of<Galeshi::MeshComponent>(child))
                                return false;
                            return !reg.get<Galeshi::MeshComponent>(child)
                                        .sourceAssetPath.empty();
                        }),
                    children.end());
            }
            bool hasChildren = !children.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
            if (m_editor.IsSelected(Galeshi::Entity(e, &m_scene))) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;
            // Landscape rows are asset drop targets for static tree models;
            // spanning the row makes that target much easier to hit.
            if (reg.all_of<Galeshi::LandscapeComponent>(e))
                flags |= ImGuiTreeNodeFlags_SpanAvailWidth;

            bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.c_str());

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_editor.Select(Galeshi::Entity(e, &m_scene));
            if (ImGui::IsItemHovered() &&
                ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                FocusCameraOnEntity(e);
            }

            // Hierarchy actions for meshes, collision shapes and the other
            // attachment nodes artists use to assemble a character.
            if (ImGui::BeginPopupContextItem("##node_context")) {
                auto selected = m_editor.SelectedAll(&m_scene);
                const bool canAttachSelected =
                    selected.size() == 1 &&
                    selected.front().Handle() != e &&
                    !HasSkeletalParent(selected.front().Handle()) &&
                    !IsDescendantOf(e, selected.front().Handle());

                if (ImGui::MenuItem("Attach Selected Here", nullptr, false,
                                    canAttachSelected)) {
                    AttachSelectedTo(e);
                }
                if (ImGui::BeginMenu("Attach Mesh")) {
                    if (ImGui::MenuItem("Cube")) AttachPrimitiveMesh(e, false);
                    if (ImGui::MenuItem("Sphere")) AttachPrimitiveMesh(e, true);
                    ImGui::EndMenu();
                }
                if (ImGui::BeginMenu("Attach Collision Shape")) {
                    if (ImGui::MenuItem("Box")) {
                        AttachCollisionShape(
                            e, Galeshi::ColliderShapeType::Box);
                    }
                    if (ImGui::MenuItem("Sphere")) {
                        AttachCollisionShape(
                            e, Galeshi::ColliderShapeType::Sphere);
                    }
                    if (ImGui::MenuItem("Capsule")) {
                        AttachCollisionShape(
                            e, Galeshi::ColliderShapeType::Capsule);
                    }
                    ImGui::EndMenu();
                }

                ImGui::Separator();
                const bool canDetachParent =
                    reg.all_of<Galeshi::ParentComponent>(e) &&
                    !HasSkeletalParent(e);
                const bool canDetachChildren =
                    !children.empty() &&
                    !reg.all_of<Galeshi::SkeletalComponent>(e);
                if (ImGui::MenuItem("Detach from Parent", nullptr, false,
                                    canDetachParent)) {
                    DetachFromParent(e);
                }
                if (ImGui::MenuItem("Detach Children", nullptr, false,
                                    canDetachChildren)) {
                    DetachChildren(e);
                }
                ImGui::EndPopup();
            }

            if (!HasSkeletalParent(e) && ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ENTITY", &e, sizeof(entt::entity));
                ImGui::Text("Move %s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                // A model dropped on a Landscape node becomes its instanced
                // static tree mesh. A glTF/GLB dropped on a character or on
                // its capsule swaps the model while preserving the hierarchy;
                // material values come from the new file. Other entities keep
                // accepting only Outliner reparenting drops.
                if (const ImGuiPayload* active = ImGui::GetDragDropPayload();
                    active && active->IsDataType("GALESHI_ASSET_PATH") &&
                    ImGui::IsItemHovered(
                        ImGuiHoveredFlags_AllowWhenBlockedByActiveItem)) {
                    const char* activePath =
                        static_cast<const char*>(active->Data);
                    if (reg.all_of<Galeshi::LandscapeComponent>(e)) {
                        ImGui::SetTooltip(
                            "Drop a model to use it as this Landscape's "
                            "instanced tree mesh.");
                    } else if (activePath &&
                               IsGltfModelAssetPath(activePath) &&
                               FindSkeletalRootForReplacement(e)) {
                        ImGui::SetTooltip(
                            "Drop to replace this character's model with the "
                            "glTF/GLB.\nMaterial values are reloaded from the "
                            "file.");
                    } else if (activePath &&
                               AssetExtensionLower(activePath) == ".fbx") {
                        ImGui::SetTooltip(
                            "FBX drag & drop is disabled. Use glTF/GLB.");
                    }
                }
                if (const ImGuiPayload* payload =
                        ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path =
                        static_cast<const char*>(payload->Data);
                    if (path && reg.all_of<Galeshi::LandscapeComponent>(e)) {
                        if (IsDragDropModelAssetPath(path)) {
                            AssignLandscapeTreeModel(
                                Galeshi::Entity(e, &m_scene), path);
                        }
                    } else if (path && IsGltfModelAssetPath(path) &&
                               FindSkeletalRootForReplacement(e)) {
                        ReplaceCharacterModel(e, path);
                    }
                }
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                    entt::entity dragged = *(const entt::entity*)payload->Data;
                    if (HasSkeletalParent(dragged)) {
                        spdlog::warn("[Outliner] Cannot move a child of a "
                                     "skeletal mesh");
                    } else if (dragged != e) {
                        // Cycle detection: check if 'e' is a descendant of 'dragged'
                        bool isCycle = false;
                        entt::entity curr = e;
                        int depth = 0;
                        while (curr != entt::null && depth++ < 128) {
                            if (curr == dragged) { isCycle = true; break; }
                            if (reg.all_of<Galeshi::ParentComponent>(curr))
                                curr = reg.get<Galeshi::ParentComponent>(curr).parent;
                            else
                                break;
                        }

                        if (!isCycle) {
                            PushHistory();

                            // 1) Detach from old parent
                            if (reg.all_of<Galeshi::ParentComponent>(dragged))
                                reg.remove<Galeshi::ParentComponent>(dragged);

                            // 2) Attach to new parent
                            reg.emplace_or_replace<Galeshi::ParentComponent>(
                                dragged, Galeshi::ParentComponent{ e });

                            // 3) Snap to Parent's origin (Local Transform = Identity)
                            // The agent will now visually be at the same position as the parent.
                            if (reg.all_of<Galeshi::TransformComponent>(dragged)) {
                                auto& tf = reg.get<Galeshi::TransformComponent>(dragged);
                                tf.position = glm::vec3(0.0f);
                                tf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                                tf.scale = glm::vec3(1.0f);
                            }
                        } else {
                            spdlog::warn("[Outliner] Cannot reparent: would create a cycle.");
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            if (open) {
                if (hasChildren) {
                    for (auto child : children) {
                        DrawNode(child, depth + 1);
                    }
                }
                ImGui::TreePop(); // MUST be called if TreeNodeEx returned true
            }
            ImGui::PopID();
        };

        // Snapshot roots
        std::vector<entt::entity> roots;
        for (auto e : m_scene.AllEntities()) {
            if (!reg.all_of<Galeshi::ParentComponent>(e))
                roots.push_back(e);
        }
        for (auto e : roots) {
            DrawNode(e, 0);
        }

        // Background drop zone: unparent
        ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.y < 10.0f) avail.y = 10.0f;
        ImGui::InvisibleButton("##outliner_bg", avail);
        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                entt::entity dragged = *(const entt::entity*)payload->Data;
                if (HasSkeletalParent(dragged)) {
                    spdlog::warn("[Outliner] Cannot detach a child of a "
                                 "skeletal mesh");
                } else if (reg.all_of<Galeshi::ParentComponent>(dragged)) {
                    PushHistory();
                    // 1) Remember world matrix BEFORE unparent
                    glm::mat4 worldA_old = GetWorldMatrix(dragged);
                    // 2) Detach
                    reg.remove<Galeshi::ParentComponent>(dragged);
                    // 3) World matrix becomes local matrix
                    SetLocalFromMatrix(dragged, worldA_old);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::End();
    }

    std::string FindCharacterModelPath(entt::entity entity) {
        if (!m_assets) return {};
        auto& registry = m_scene.Registry();
        if (!registry.valid(entity)) return {};

        if (registry.all_of<Galeshi::SkeletalComponent>(entity)) {
            const auto& source =
                registry.get<Galeshi::SkeletalComponent>(entity).sourcePath;
            if (!source.empty()) return source;
        }

        // Find the first descendant mesh belonging to a model with bones or
        // animation clips. The editor does not need Player/NPC tags for this;
        // the imported model type is the source of truth.
        std::vector<entt::entity> pending{entity};
        std::unordered_set<entt::entity> visited;
        while (!pending.empty()) {
            const entt::entity current = pending.back();
            pending.pop_back();
            if (!registry.valid(current) || !visited.insert(current).second) {
                continue;
            }
            if (registry.all_of<Galeshi::MeshComponent>(current)) {
                const auto& mesh =
                    registry.get<Galeshi::MeshComponent>(current);
                if (!mesh.sourceAssetPath.empty()) {
                    auto model = m_assets->LoadModel(mesh.sourceAssetPath);
                    if (model.hasAnimation || model.totalBones > 0) {
                        return mesh.sourceAssetPath;
                    }
                }
            }
            auto childView = registry.view<Galeshi::ParentComponent>();
            for (entt::entity child : childView) {
                if (childView.get<Galeshi::ParentComponent>(child).parent ==
                    current) {
                    pending.push_back(child);
                }
            }
        }
        return {};
    }

    bool AssignStateAnimationAsset(Galeshi::Entity entity, int stateIndex,
                                   const std::string& originalPath) {
        if (!entity || !m_assets || stateIndex < 0) return false;
        if (!IsModelAssetPath(originalPath)) return false;
        auto& registry = m_scene.Registry();
        auto* machine =
            registry.try_get<Galeshi::AnimationStateMachineComponent>(
                entity.Handle());
        if (!machine || stateIndex >= static_cast<int>(machine->states.size())) {
            return false;
        }

        PushHistory();
        auto& state = machine->states[static_cast<size_t>(stateIndex)];
        state.animationPath = m_assets->MakeRelativePath(originalPath);
        state.clip = 0;
        state.animationName.clear();
        auto animationModel = m_assets->LoadModel(state.animationPath);
        if (!animationModel.animationNames.empty()) {
            state.animationName = animationModel.animationNames.front();
        }

        // Dropping a clip is an explicit request to preview it.
        machine->enabled = true;
        machine->currentState = stateIndex;
        machine->previousState = -1;
        machine->stateTime = 0.0f;
        machine->previousTime = 0.0f;
        machine->blendElapsed = machine->blendDuration;
        machine->initialized = true;
        if (auto* skeleton =
                registry.try_get<Galeshi::SkeletalComponent>(entity.Handle())) {
            skeleton->isAnimated = true;
        }
        return true;
    }

    void DrawAnimationStateMachineContents(
        Galeshi::Entity entity,
        const std::string& fallbackModelPath = {}) {
        if (!entity) return;
        auto& registry = m_scene.Registry();
        const entt::entity handle = entity.Handle();
        if (!registry.valid(handle)) return;

        auto* skeleton = registry.try_get<Galeshi::SkeletalComponent>(handle);
        std::string modelPath = fallbackModelPath;
        if (skeleton && !skeleton->sourcePath.empty()) {
            modelPath = skeleton->sourcePath;
        }
        if (modelPath.empty()) modelPath = FindCharacterModelPath(handle);

        std::vector<std::string> clipNames;
        bool modelHasSkeleton = false;
        if (!modelPath.empty() && m_assets) {
            auto model = m_assets->LoadModel(modelPath);
            clipNames = model.animationNames;
            modelHasSkeleton = model.totalBones > 0;
        }

        std::vector<std::string> clipLabels;
        clipLabels.reserve(clipNames.size());
        for (size_t i = 0; i < clipNames.size(); ++i) {
            clipLabels.push_back(clipNames[i].empty()
                                     ? "Clip " + std::to_string(i)
                                     : clipNames[i]);
        }
        if (clipLabels.empty()) clipLabels.push_back("No animation clips");

        if (!modelPath.empty()) {
            if (clipNames.empty()) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                    "No embedded animation clips in the character model.");
                ImGui::TextWrapped(
                    "FBX export needs \"All Actions\" (or an active Action) "
                    "to embed clips. glTF/GLB can embed animation clips or "
                    "be used as a separate animation asset.");
            } else {
                ImGui::TextDisabled("Embedded clips: %zu", clipNames.size());
            }
            if (!modelHasSkeleton) {
                ImGui::TextColored(
                    ImVec4(1.0f, 0.65f, 0.25f, 1.0f),
                    "This model has no skin/armature. Separate animation clips "
                    "cannot deform it; export the glTF with the character skin.");
            }
        }
        std::vector<const char*> clipItems;
        clipItems.reserve(clipLabels.size());
        for (const std::string& label : clipLabels) {
            clipItems.push_back(label.c_str());
        }

        auto* machine =
            registry.try_get<Galeshi::AnimationStateMachineComponent>(handle);
        if (!machine) {
            if (!skeleton) {
                if (modelPath.empty()) {
                    ImGui::TextDisabled(
                        "This entity has no skeletal mesh.");
                    return;
                }
                ImGui::TextDisabled("Skeletal model: %s", modelPath.c_str());
                if (ImGui::Button("+ Set Up Skeletal Character")) {
                    PushHistory();
                    auto model = m_assets->LoadModel(modelPath);
                    auto& created =
                        registry.emplace<Galeshi::SkeletalComponent>(handle);
                    created.sourcePath = m_assets->MakeRelativePath(modelPath);
                    created.isAnimated = model.hasAnimation;
                    created.boneMatrices.assign(256, glm::mat4(1.0f));
                    auto& createdMachine =
                        registry.emplace<
                            Galeshi::AnimationStateMachineComponent>(handle);
                    createdMachine =
                        Galeshi::AnimationSystem::CreateDefaultStateMachine(
                            model.animationNames);
                    createdMachine.parameters = {
                        {"Speed", 0.0f},
                        {"PlayerVisible", 0.0f},
                        {"TargetDistance", 100.0f}
                    };
                }
                return;
            }
            if (skeleton->sourcePath.empty() && !modelPath.empty()) {
                skeleton->sourcePath = m_assets->MakeRelativePath(modelPath);
            }
            if (ImGui::Button("+ Add Animation State Machine")) {
                PushHistory();
                auto& created =
                    registry.emplace<Galeshi::AnimationStateMachineComponent>(
                        handle);
                created = Galeshi::AnimationSystem::CreateDefaultStateMachine(
                    clipNames);
                created.parameters = {
                    {"Speed", 0.0f},
                    {"PlayerVisible", 0.0f},
                    {"TargetDistance", 100.0f}
                };
            }
            return;
        }

        ImGui::PushID(static_cast<int>(handle));
        ImGui::Checkbox("Enabled", &machine->enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Playback Speed", &machine->playbackSpeed,
                         0.01f, 0.0f, 4.0f);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Animation Scale", &machine->animationScale,
                         0.001f, 0.001f, 100.0f, "%.4f");
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip(
                "Uniform scale for retargeted animation translation.\n"
                "Use 0.01 for centimeter animations on a meter character.");
        }
        ImGui::Text("Current State: %d / %zu", machine->currentState,
                    machine->states.size());
        ImGui::TextDisabled("Clip duration: %.2fs",
                            machine->currentClipDuration);

        if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeState = -1;
            for (int i = 0; i < static_cast<int>(machine->states.size()); ++i) {
                auto& state = machine->states[static_cast<size_t>(i)];
                ImGui::PushID(i);
                char stateName[128];
                std::snprintf(stateName, sizeof(stateName), "%s",
                              state.name.c_str());
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText("Name", stateName, sizeof(stateName))) {
                    state.name = stateName;
                }
                ImGui::SameLine();

                char animationPath[512];
                std::snprintf(animationPath, sizeof(animationPath), "%s",
                              state.animationPath.c_str());
                ImGui::SetNextItemWidth(260.0f);
                if (ImGui::InputText("Animation Asset", animationPath,
                                     sizeof(animationPath))) {
                    state.animationPath = animationPath;
                    state.clip = 0;
                    state.animationName.clear();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Empty = clips embedded in the character model.\n"
                        "Drag a separate FBX, glTF or GLB animation here to "
                        "retarget its clips by bone name.");
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(
                                "GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        const std::string candidate = path ? path : "";
                        AssignStateAnimationAsset(entity, i, candidate);
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse##animation_asset")) {
                    m_animationClipTarget = handle;
                    m_animationClipState = i;
                    std::string start = "assets";
                    const std::string projectPath =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (!projectPath.empty()) {
                        start = (std::filesystem::path(projectPath) /
                                 "assets").string();
                    }
                    m_animationClipPicker.Open(
                        start, "Select Animation Asset",
                        {".fbx", ".gltf", ".glb", ".obj", ".dae"});
                }
                if (!state.animationPath.empty()) {
                    ImGui::SameLine();
                    if (clipNames.empty()) {
                        ImGui::BeginDisabled();
                        ImGui::SmallButton("Use Embedded");
                        ImGui::EndDisabled();
                        if (ImGui::IsItemHovered(
                                ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip(
                                "The character model contains no embedded "
                                "animation clips.");
                        }
                    } else if (ImGui::SmallButton("Use Embedded")) {
                        state.animationPath.clear();
                        state.clip = 0;
                        state.animationName.clear();
                    }
                }

                std::vector<std::string> stateClipNames = clipNames;
                if (!state.animationPath.empty() && m_assets) {
                    auto animationModel =
                        m_assets->LoadModel(state.animationPath);
                    stateClipNames = animationModel.animationNames;
                }
                if (state.animationName.empty() &&
                    state.clip >= 0 &&
                    state.clip < static_cast<int>(stateClipNames.size())) {
                    state.animationName = stateClipNames[state.clip];
                }
                std::vector<std::string> stateClipLabels;
                stateClipLabels.reserve(stateClipNames.size());
                for (size_t clipIndex = 0;
                     clipIndex < stateClipNames.size(); ++clipIndex) {
                    stateClipLabels.push_back(
                        stateClipNames[clipIndex].empty()
                            ? "Clip " + std::to_string(clipIndex)
                            : stateClipNames[clipIndex]);
                }
                if (stateClipLabels.empty()) {
                    stateClipLabels.push_back("No animation clips");
                }
                std::vector<const char*> stateClipItems;
                stateClipItems.reserve(stateClipLabels.size());
                for (const std::string& label : stateClipLabels) {
                    stateClipItems.push_back(label.c_str());
                }
                int clip = std::clamp(
                    state.clip, 0,
                    static_cast<int>(stateClipItems.size()) - 1);
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::Combo("Clip", &clip, stateClipItems.data(),
                                 static_cast<int>(stateClipItems.size()))) {
                    state.clip = clip;
                    if (clip >= 0 &&
                        clip < static_cast<int>(stateClipNames.size())) {
                        state.animationName = stateClipNames[clip];
                    } else {
                        state.animationName.clear();
                    }
                    // Preview the selected state immediately. Previously a
                    // clip change only edited the state data; the animation
                    // kept sampling machine->currentState, so selecting a
                    // different embedded clip appeared to do nothing until
                    // the separate "Set" button was pressed.
                    machine->enabled = true;
                    machine->currentState = i;
                    machine->previousState = -1;
                    machine->stateTime = 0.0f;
                    machine->blendElapsed = machine->blendDuration;
                    if (skeleton) skeleton->isAnimated = true;
                }
                ImGui::SameLine();
                ImGui::Checkbox("Loop", &state.loop);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80.0f);
                ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 4.0f);
                ImGui::SameLine();
                if (ImGui::SmallButton("Set")) {
                    machine->enabled = true;
                    machine->currentState = i;
                    machine->previousState = -1;
                    machine->stateTime = 0.0f;
                    machine->blendElapsed = machine->blendDuration;
                    if (skeleton) skeleton->isAnimated = true;
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeState = i;
                ImGui::PopID();
            }
            if (removeState >= 0 && machine->states.size() > 1) {
                machine->states.erase(machine->states.begin() + removeState);
                for (auto& transition : machine->transitions) {
                    if (transition.fromState == removeState) {
                        transition.fromState = -1;
                    } else if (transition.fromState > removeState) {
                        --transition.fromState;
                    }
                    if (transition.toState == removeState) {
                        transition.toState = 0;
                    } else if (transition.toState > removeState) {
                        --transition.toState;
                    }
                }
                if (machine->currentState >=
                    static_cast<int>(machine->states.size())) {
                    machine->currentState = 0;
                }
            }
            if (ImGui::Button("+ Add State")) {
                Galeshi::AnimationState state;
                state.name = "State " +
                             std::to_string(machine->states.size());
                state.clip = std::clamp(
                    machine->currentState, 0,
                    std::max(0, static_cast<int>(clipNames.size()) - 1));
                if (state.clip >= 0 &&
                    state.clip < static_cast<int>(clipNames.size())) {
                    state.animationName = clipNames[state.clip];
                }
                machine->states.push_back(state);
            }
            if (!clipNames.empty()) {
                ImGui::SameLine();
                if (ImGui::Button("+ Add All Clips")) {
                    for (size_t clipIndex = 0; clipIndex < clipNames.size();
                         ++clipIndex) {
                        bool alreadyAdded = false;
                        for (const auto& state : machine->states) {
                            if (state.clip == static_cast<int>(clipIndex) &&
                                state.name == clipNames[clipIndex]) {
                                alreadyAdded = true;
                                break;
                            }
                        }
                        if (alreadyAdded) continue;
                        Galeshi::AnimationState state;
                        state.name = clipNames[clipIndex].empty()
                            ? "Clip " + std::to_string(clipIndex)
                            : clipNames[clipIndex];
                        state.clip = static_cast<int>(clipIndex);
                        state.animationName = clipNames[clipIndex];
                        machine->states.push_back(state);
                    }
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Create one state for every animation clip in the model.");
                }
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Transitions", ImGuiTreeNodeFlags_DefaultOpen)) {
            const char* conditionNames[] = {
                "Bool True", "Bool False", "Greater", "Less", "Trigger",
                "Always"
            };
            int removeTransition = -1;
            for (int i = 0; i < static_cast<int>(machine->transitions.size());
                 ++i) {
                auto& transition =
                    machine->transitions[static_cast<size_t>(i)];
                ImGui::PushID(1000 + i);
                int from = transition.fromState + 1;
                std::vector<const char*> fromNames;
                fromNames.push_back("Any State");
                for (const auto& state : machine->states) {
                    fromNames.push_back(state.name.c_str());
                }
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("From", &from, fromNames.data(),
                                 static_cast<int>(fromNames.size()))) {
                    transition.fromState = from - 1;
                }
                ImGui::SameLine();
                int to = std::clamp(
                    transition.toState, 0,
                    std::max(0, static_cast<int>(machine->states.size()) - 1));
                std::vector<const char*> toNames;
                for (const auto& state : machine->states) {
                    toNames.push_back(state.name.c_str());
                }
                ImGui::SetNextItemWidth(150.0f);
                if (ImGui::Combo("To", &to, toNames.data(),
                                 static_cast<int>(toNames.size()))) {
                    transition.toState = to;
                }
                int condition = static_cast<int>(transition.condition);
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::Combo("Condition", &condition, conditionNames,
                                 IM_ARRAYSIZE(conditionNames))) {
                    transition.condition =
                        static_cast<Galeshi::AnimationCondition>(condition);
                }
                ImGui::SameLine();
                char parameter[128];
                std::snprintf(parameter, sizeof(parameter), "%s",
                              transition.parameter.c_str());
                ImGui::SetNextItemWidth(120.0f);
                if (ImGui::InputText("Parameter", parameter,
                                     sizeof(parameter))) {
                    transition.parameter = parameter;
                }
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("Threshold", &transition.threshold, 0.01f);
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90.0f);
                ImGui::DragFloat("Blend", &transition.duration, 0.01f,
                                 0.0f, 4.0f);
                ImGui::SameLine();
                ImGui::Checkbox("On", &transition.enabled);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeTransition = i;
                ImGui::Separator();
                ImGui::PopID();
            }
            if (removeTransition >= 0) {
                machine->transitions.erase(
                    machine->transitions.begin() + removeTransition);
            }
            if (ImGui::Button("+ Add Transition")) {
                Galeshi::AnimationTransition transition;
                transition.fromState = machine->currentState;
                transition.toState =
                    (machine->currentState + 1) %
                    std::max<size_t>(1, machine->states.size());
                machine->transitions.push_back(transition);
            }
            ImGui::TreePop();
        }

        if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
            int removeParameter = -1;
            for (int i = 0; i < static_cast<int>(machine->parameters.size());
                 ++i) {
                auto& parameter = machine->parameters[static_cast<size_t>(i)];
                ImGui::PushID(2000 + i);
                char parameterName[128];
                std::snprintf(parameterName, sizeof(parameterName), "%s",
                              parameter.name.c_str());
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::InputText("Name", parameterName,
                                     sizeof(parameterName))) {
                    parameter.name = parameterName;
                }
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120.0f);
                ImGui::DragFloat("Value", &parameter.value, 0.01f);
                ImGui::SameLine();
                if (ImGui::SmallButton("Trigger")) parameter.value = 1.0f;
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) removeParameter = i;
                ImGui::PopID();
            }
            if (removeParameter >= 0) {
                machine->parameters.erase(machine->parameters.begin() +
                                          removeParameter);
            }
            if (ImGui::Button("+ Add Parameter")) {
                machine->parameters.push_back({"Parameter", 0.0f});
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }

    void DrawStateMachineEditor() {
        if (!m_showStateMachineEditor) return;

        // Keep this editor in the bottom dock group even when an old
        // imgui.ini remembers it as a floating window.
        if (ImGuiWindow* host = ImGui::FindWindowByName("UI Designer");
            host && host->DockId != 0) {
            ImGui::SetNextWindowDockID(host->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* output = ImGui::FindWindowByName("Output Log");
                   output && output->DockId != 0) {
            ImGui::SetNextWindowDockID(output->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* browser =
                       ImGui::FindWindowByName("Content Browser");
                   browser && browser->DockId != 0) {
            ImGui::SetNextWindowDockID(browser->DockId, ImGuiCond_Always);
        }

        if (!ImGui::Begin("Animation State Machine Editor",
                          &m_showStateMachineEditor)) {
            ImGui::End();
            return;
        }

        struct CharacterEntry {
            entt::entity entity = entt::null;
            std::string label;
            std::string sourcePath;
        };
        std::vector<CharacterEntry> characters;
        std::unordered_map<entt::entity, size_t> characterIndices;
        auto& registry = m_scene.Registry();

        auto addCharacter = [&](entt::entity entity,
                                const std::string& sourcePath) {
            if (!registry.valid(entity)) return;
            const auto existing = characterIndices.find(entity);
            if (existing != characterIndices.end()) {
                CharacterEntry& entry =
                    characters[existing->second];
                if (entry.sourcePath.empty() && !sourcePath.empty()) {
                    entry.sourcePath = sourcePath;
                }
                return;
            }
            std::string name = "Entity " + std::to_string((uint32_t)entity);
            if (registry.all_of<Galeshi::NameComponent>(entity)) {
                name = registry.get<Galeshi::NameComponent>(entity).name;
            }
            characterIndices.emplace(entity, characters.size());
            characters.push_back({entity, name, sourcePath});
        };

        // Direct skeletal-character roots.
        auto skeletonView = registry.view<Galeshi::SkeletalComponent>();
        for (entt::entity handle : skeletonView) {
            const auto& skeleton = skeletonView.get<Galeshi::SkeletalComponent>(handle);
            addCharacter(handle, skeleton.sourcePath);
        }

        // Also detect imported skeletal mesh hierarchies that do not yet have
        // a SkeletalComponent on their root. The model's bone/animation data
        // is the type check; Player/NPC tags are intentionally not consulted.
        std::unordered_map<std::string, bool> skeletalModelCache;
        auto isSkeletalModel = [&](const std::string& path) {
            if (path.empty()) return false;
            const auto cached = skeletalModelCache.find(path);
            if (cached != skeletalModelCache.end()) return cached->second;
            auto model = m_assets ? m_assets->LoadModel(path)
                                  : Galeshi::AssetManager::LoadedModel{};
            const bool skeletal = model.hasAnimation || model.totalBones > 0;
            skeletalModelCache.emplace(path, skeletal);
            return skeletal;
        };

        auto modelRootForMesh = [&](entt::entity handle) {
            // Prefer the nearest ancestor that owns a skeleton. Walking all
            // the way to the scene root can select a shared parent: editing
            // its state machine would then animate every character below it.
            entt::entity root = handle;
            for (int depth = 0; depth < 128 &&
                                registry.valid(root);
                 ++depth) {
                if (registry.all_of<Galeshi::SkeletalComponent>(root) ||
                    registry.all_of<Galeshi::AnimationStateMachineComponent>(
                        root)) {
                    return root;
                }
                if (!registry.all_of<Galeshi::ParentComponent>(root)) break;
                root = registry.get<Galeshi::ParentComponent>(root).parent;
            }
            if (registry.all_of<Galeshi::ParentComponent>(handle)) {
                // Imported model meshes normally live under their own root;
                // keep that root instead of the common scene parent.
                return registry.get<Galeshi::ParentComponent>(handle).parent;
            }
            return handle;
        };

        auto meshView = registry.view<Galeshi::MeshComponent>();
        for (entt::entity handle : meshView) {
            const auto& mesh = meshView.get<Galeshi::MeshComponent>(handle);
            if (mesh.sourceAssetPath.empty()) continue;
            // glTF characters can be authored without embedded animation
            // clips (or even without skin data in the file). They must still
            // be selectable here so an external clip or a skeleton can be
            // assigned later. FBX props stay filtered by the skeletal check.
            const bool gltfModel =
                IsGltfModelAssetPath(mesh.sourceAssetPath);
            if (!gltfModel && !isSkeletalModel(mesh.sourceAssetPath)) {
                continue;
            }
            addCharacter(modelRootForMesh(handle), mesh.sourceAssetPath);
        }

        // A model just added to the scene is usually selected. Include the
        // selection explicitly so the state machine editor can target it even
        // if the import does not expose bones or animation metadata.
        for (Galeshi::Entity selected : m_editor.SelectedAll(&m_scene)) {
            const entt::entity handle = selected.Handle();
            if (!registry.valid(handle)) continue;
            if (const auto* skeleton =
                    registry.try_get<Galeshi::SkeletalComponent>(handle)) {
                addCharacter(handle, skeleton->sourcePath);
                continue;
            }
            if (const auto* mesh =
                    registry.try_get<Galeshi::MeshComponent>(handle);
                mesh && !mesh->sourceAssetPath.empty()) {
                addCharacter(modelRootForMesh(handle), mesh->sourceAssetPath);
                continue;
            }

            // Selected model root/container: find the first descendant mesh
            // that belongs to an imported model.
            std::vector<entt::entity> pending{handle};
            std::unordered_set<entt::entity> visited;
            while (!pending.empty()) {
                const entt::entity current = pending.back();
                pending.pop_back();
                if (!registry.valid(current) ||
                    !visited.insert(current).second) {
                    continue;
                }
                if (const auto* mesh =
                        registry.try_get<Galeshi::MeshComponent>(current);
                    mesh && !mesh->sourceAssetPath.empty()) {
                    addCharacter(handle, mesh->sourceAssetPath);
                    break;
                }
                auto childView = registry.view<Galeshi::ParentComponent>();
                for (entt::entity child : childView) {
                    if (childView.get<Galeshi::ParentComponent>(child).parent ==
                        current) {
                        pending.push_back(child);
                    }
                }
            }
        }

        std::sort(characters.begin(), characters.end(),
                  [](const CharacterEntry& a, const CharacterEntry& b) {
                      return a.label < b.label;
                  });

        if (characters.empty()) {
            ImGui::TextDisabled("No skeletal characters in the scene.");
            ImGui::End();
            return;
        }

        bool found = false;
        for (const CharacterEntry& entry : characters) {
            if (entry.entity == m_stateMachineEditorEntity) {
                found = true;
                break;
            }
        }
        if (!found) {
            m_stateMachineEditorEntity = characters.front().entity;
        }

        int current = 0;
        for (int i = 0; i < static_cast<int>(characters.size()); ++i) {
            if (characters[static_cast<size_t>(i)].entity ==
                m_stateMachineEditorEntity) {
                current = i;
                break;
            }
        }
        std::vector<const char*> labels;
        labels.reserve(characters.size());
        for (const CharacterEntry& entry : characters) {
            labels.push_back(entry.label.c_str());
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##character", &current, labels.data(),
                         static_cast<int>(labels.size()))) {
            m_stateMachineEditorEntity =
                characters[static_cast<size_t>(current)].entity;
        }
        if (ImGui::Button("Select in Scene")) {
            Galeshi::Entity entity(m_stateMachineEditorEntity, &m_scene);
            if (entity) m_editor.Select(entity);
        }
        ImGui::SameLine();
        std::string selectedSourcePath;
        for (const CharacterEntry& entry : characters) {
            if (entry.entity == m_stateMachineEditorEntity) {
                selectedSourcePath = entry.sourcePath;
                break;
            }
        }
        if (registry.all_of<Galeshi::SkeletalComponent>(
                m_stateMachineEditorEntity)) {
            const auto& skeleton = registry.get<Galeshi::SkeletalComponent>(
                m_stateMachineEditorEntity);
            if (!skeleton.sourcePath.empty()) {
                selectedSourcePath = skeleton.sourcePath;
            }
        }
        ImGui::TextDisabled("Source: %s",
                            selectedSourcePath.empty()
                                ? "(skeletal mesh not assigned)"
                                : selectedSourcePath.c_str());
        ImGui::TextDisabled(
            "Drop an animation file on the Animation Asset field below.");
        ImGui::Separator();

        ImGui::BeginChild("##state_machine_scroll");
        DrawAnimationStateMachineContents(
            Galeshi::Entity(m_stateMachineEditorEntity, &m_scene),
            selectedSourcePath);
        ImGui::EndChild();
        ImGui::End();
    }

void DrawDetails() {
        ImGui::Begin("Details");

        auto selected = m_editor.SelectedAll(&m_scene);
        if (selected.empty()) { ImGui::TextDisabled("No selection"); ImGui::End(); return; }

        auto lowerAscii = [](std::string value) {
            for (char& c : value) {
                c = static_cast<char>(
                    std::tolower(static_cast<unsigned char>(c)));
            }
            return value;
        };
        const bool searchActive = !m_detailsSearch.empty();
        const std::string searchLower = lowerAscii(m_detailsSearch);
        bool detailsAnyMatch = false;

        // Floating sticky search bar inside Details. It has its own rounded
        // background, is right-aligned, and stays visible while the component
        // list scrolls in the child below. This is not a separate window.
        {
            const float availableWidth = ImGui::GetContentRegionAvail().x;
            const float searchWidth = std::min(360.0f, availableWidth);
            const float clearWidth = 52.0f;
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + availableWidth -
                                 searchWidth);
            ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 7.0f);
            ImGui::PushStyleColor(ImGuiCol_ChildBg,
                                  ImVec4(0.075f, 0.095f, 0.12f, 0.94f));
            ImGui::BeginChild("##DetailsSearchFloat",
                              ImVec2(searchWidth,
                                     ImGui::GetFrameHeight() + 10.0f),
                              true, ImGuiWindowFlags_NoScrollbar |
                                        ImGuiWindowFlags_NoScrollWithMouse);
            ImGui::SetCursorPos(ImVec2(6.0f, 5.0f));
            ImGui::SetNextItemWidth(searchWidth - clearWidth - 16.0f);
            if (ImGui::InputTextWithHint(
                    "##DetailsSearch", "Search properties or materials...",
                    m_detailsSearchBuffer, sizeof(m_detailsSearchBuffer))) {
                m_detailsSearch = m_detailsSearchBuffer;
                m_detailsSearchScrollPending = true;
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Clear##DetailsSearch")) {
                m_detailsSearch.clear();
                m_detailsSearchBuffer[0] = '\0';
                m_detailsSearchScrollPending = true;
            }
            ImGui::EndChild();
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();
        }
        ImGui::BeginChild("##DetailsScroll", ImVec2(0.0f, 0.0f), false);

        auto searchContains = [&](const char* text) {
            if (!searchActive) return true;
            const std::string value = text ? text : "";
            return lowerAscii(value).find(searchLower) != std::string::npos;
        };
        auto beginDetailsSection = [&](const char* title,
                                       ImGuiTreeNodeFlags flags,
                                       const char* keywords) -> bool {
            if (searchActive) {
                const std::string haystack =
                    lowerAscii(std::string(title ? title : "") + " " +
                               (keywords ? keywords : ""));
                if (haystack.find(searchLower) == std::string::npos)
                    return false;
                detailsAnyMatch = true;
                ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            }
            const bool open = ImGui::CollapsingHeader(title, flags);
            if (open && searchActive && m_detailsSearchScrollPending) {
                ImGui::SetScrollHereY(0.0f);
                m_detailsSearchScrollPending = false;
            }
            return open;
        };

        // Collision-shape entities are physics-only: their details page should
        // not expose rendering materials or audio source controls.
        const bool selectedIsCollisionShape =
            selected[0].HasComponent<Galeshi::ColliderComponent>();

        // Texture Settings is a global texture-loading option, but it is only
        // relevant while inspecting a 3D model that actually uses textures.
        auto materialHasTextures = [](const Galeshi::MaterialComponent& matc) {
            if (!matc.material) return false;
            const auto& m = *matc.material;
            return m.albedoMap || m.normalMap || m.metallicRoughnessMap ||
                   m.metallicMap || m.roughnessMap || m.emissiveMap ||
                   m.occlusionMap || m.opacityMap || m.specularMap;
        };

        auto& reg = m_scene.Registry();
        const entt::entity selectedHandle = selected[0].Handle();
        bool selectionHasMesh = false;
        bool selectionHasTextures = false;
        bool selectionHasMaterials = false;
        for (auto entity : m_scene.AllEntities()) {
            entt::entity cur = entity.Handle();
            bool belongsToSelection = false;
            int depth = 0;
            while (cur != entt::null && depth++ < 128) {
                if (cur == selectedHandle) {
                    belongsToSelection = true;
                    break;
                }
                if (reg.all_of<Galeshi::ParentComponent>(cur)) {
                    cur = reg.get<Galeshi::ParentComponent>(cur).parent;
                } else {
                    break;
                }
            }
            if (!belongsToSelection) continue;

            const entt::entity handle = entity.Handle();
            if (reg.all_of<Galeshi::MeshComponent>(handle) ||
                reg.all_of<Galeshi::SkeletalComponent>(handle)) {
                selectionHasMesh = true;
            }
            if (reg.all_of<Galeshi::MaterialComponent>(handle)) {
                selectionHasMaterials = true;
                if (materialHasTextures(
                        reg.get<Galeshi::MaterialComponent>(handle))) {
                    selectionHasTextures = true;
                }
            }
        }

        if (!selected.empty() && selected[0].HasComponent<Galeshi::NameComponent>()) {
            auto& nc = selected[0].GetComponent<Galeshi::NameComponent>();
            if (searchContains(nc.name.c_str())) detailsAnyMatch = true;
            char buf[256]; std::snprintf(buf, sizeof(buf), "%s", nc.name.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) nc.name = buf;
        }

        // Character asset field. Existing FBX scenes can migrate to glTF by
        // dropping a GLB/glTF here (or on the character/capsule in the
        // viewport or Outliner). The hierarchy is preserved and all material
        // values are reloaded from the replacement file.
        if (Galeshi::Entity replacementRoot =
                FindSkeletalRootForReplacement(selectedHandle)) {
            auto& replacementSkeleton =
                reg.get<Galeshi::SkeletalComponent>(replacementRoot.Handle());
            if (beginDetailsSection(
                    "Character Model", ImGuiTreeNodeFlags_DefaultOpen,
                    "model asset gltf glb fbx swap replace character capsule "
                    "materials preserve migrate")) {
                ImGui::TextDisabled(
                    "Current: %s",
                    replacementSkeleton.sourcePath.empty()
                        ? "(none)"
                        : replacementSkeleton.sourcePath.c_str());
                if (replacementSkeleton.sourcePath.size() >= 4 &&
                    AssetExtensionLower(replacementSkeleton.sourcePath) == ".fbx") {
                    ImGui::TextColored(
                        ImVec4(1.0f, 0.72f, 0.30f, 1.0f),
                        "FBX source (legacy). Drop a glTF/GLB to migrate it.");
                }
                auto openCharacterModelPicker = [&]() {
                    m_characterModelTarget = replacementRoot.Handle();
                    std::string start = "assets";
                    const std::string projectPath =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (!projectPath.empty()) {
                        start = (std::filesystem::path(projectPath) /
                                 "assets").string();
                    }
                    m_characterModelPicker.Open(
                        start, "Replace Character Model",
                        {".gltf", ".glb"});
                };
                const float browseWidth = 78.0f;
                if (ImGui::Button(
                        "Drop glTF/GLB model here",
                        ImVec2(std::max(120.0f,
                                        ImGui::GetContentRegionAvail().x -
                                            browseWidth -
                                            ImGui::GetStyle().ItemSpacing.x),
                               34.0f))) {
                    openCharacterModelPicker();
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Replaces the model attached to this character/capsule.\n"
                        "The hierarchy and skeleton are preserved.\n"
                        "Material values are reloaded from the glTF.\n"
                        "FBX drag & drop is disabled.");
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        if (path && IsGltfModelAssetPath(path)) {
                            ReplaceCharacterModel(replacementRoot.Handle(), path);
                        } else if (path) {
                            spdlog::warn(
                                "[ModelSwap] Only glTF/GLB can replace a "
                                "character model: {}",
                                path);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }
                ImGui::SameLine();
                if (ImGui::SmallButton("Browse...")) {
                    openCharacterModelPicker();
                }
            }
        }

        // Cast Shadows is shared by the whole imported model. Find the
        // nearest model root above the current selection.
        entt::entity modelRoot = entt::null;
        for (entt::entity cursor = selectedHandle;
             reg.valid(cursor);) {
            if (reg.all_of<Galeshi::ModelComponent>(cursor)) {
                modelRoot = cursor;
                break;
            }
            if (!reg.all_of<Galeshi::ParentComponent>(cursor)) break;
            cursor = reg.get<Galeshi::ParentComponent>(cursor).parent;
        }
        if (modelRoot != entt::null) {
            auto& model = reg.get<Galeshi::ModelComponent>(modelRoot);
            if (beginDetailsSection(
                    "Model", ImGuiTreeNodeFlags_DefaultOpen,
                    "cast shadows shadow model renderer")) {
                bool castsShadow = model.castsShadow;
                if (ImGui::Checkbox("Cast Shadows", &castsShadow)) {
                    PushHistory();
                    model.castsShadow = castsShadow;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Controls shadow casting for every mesh in this model.");
                }
            }
        }

        // Mesh LOD controls apply to the selected mesh and all mesh children.
        // This is especially useful for imported static models, which default
        // to LOD0; users can opt into generated LODs per model/sub-mesh.
        {
            std::vector<Galeshi::Entity> lodMeshes;
            for (auto entity : m_scene.AllEntities()) {
                if (!entity.HasComponent<Galeshi::MeshComponent>()) continue;
                bool belongsToSelection =
                    entity.Handle() == selectedHandle;
                entt::entity cursor = entity.Handle();
                for (int depth = 0;
                     !belongsToSelection && depth < 128; ++depth) {
                    if (!reg.all_of<Galeshi::ParentComponent>(cursor)) break;
                    cursor = reg.get<Galeshi::ParentComponent>(cursor).parent;
                    if (!reg.valid(cursor)) break;
                    if (cursor == selectedHandle) belongsToSelection = true;
                }
                if (belongsToSelection) lodMeshes.push_back(entity);
            }
            if (!lodMeshes.empty()) {
                if (beginDetailsSection(
                        "Mesh LOD", ImGuiTreeNodeFlags_DefaultOpen,
                        "lod level level of detail mesh detail generated "
                        "bias force auto")) {
                    auto& first =
                        lodMeshes.front().GetComponent<Galeshi::MeshComponent>();
                    bool enabled = first.lodEnabled;
                    bool mixedEnabled = false;
                    int forced = first.forcedLod;
                    bool mixedForced = false;
                    float bias = first.lodBias;
                    bool mixedBias = false;
                    for (size_t i = 1; i < lodMeshes.size(); ++i) {
                        const auto& mc =
                            lodMeshes[i].GetComponent<Galeshi::MeshComponent>();
                        mixedEnabled |= mc.lodEnabled != enabled;
                        mixedForced |= mc.forcedLod != forced;
                        mixedBias |= std::abs(mc.lodBias - bias) > 1e-4f;
                    }
                    if (mixedEnabled) ImGui::TextDisabled("Mixed values");
                    if (ImGui::Checkbox("Enable Generated LOD", &enabled)) {
                        for (auto& entity : lodMeshes)
                            entity.GetComponent<Galeshi::MeshComponent>()
                                .lodEnabled = enabled;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Imported models default to authored LOD 0.\n"
                            "Enable to let the renderer generate cluster LODs.");
                    }

                    const char* lodItems[] = {
                        "Auto", "Force LOD 0", "Force LOD 1",
                        "Force LOD 2", "Force LOD 3"
                    };
                    int lodSelection = forced + 1;
                    if (lodSelection < 0) lodSelection = 0;
                    if (lodSelection > 4) lodSelection = 4;
                    if (mixedForced) {
                        ImGui::TextDisabled("Force LOD: mixed");
                    }
                    if (ImGui::Combo("LOD Override", &lodSelection,
                                     lodItems, IM_ARRAYSIZE(lodItems))) {
                        const int newForced = lodSelection - 1;
                        for (auto& entity : lodMeshes)
                            entity.GetComponent<Galeshi::MeshComponent>()
                                .forcedLod = newForced;
                    }

                    if (mixedBias) ImGui::TextDisabled("LOD Bias: mixed");
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::SliderFloat("LOD Bias", &bias, 0.25f,
                                           4.0f, "%.2f")) {
                        for (auto& entity : lodMeshes)
                            entity.GetComponent<Galeshi::MeshComponent>()
                                .lodBias = bias;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Multiplier for automatic LOD selection.\n"
                            "Lower = more detail, higher = more aggressive LOD.");
                    }
                    ImGui::TextDisabled(
                        "%zu mesh(es), generated levels: %u",
                        lodMeshes.size(),
                        first.mesh ? first.mesh->LodCount() : 1u);
                }
            }
        }

        if (selectionHasMesh && selectionHasTextures) {
            if (beginDetailsSection(
                    "Texture Settings", 0,
                    "texture compression compress bc7 loading vram")) {
                bool compress = Galeshi::Texture::CompressionEnabled();
                if (ImGui::Checkbox("Compress textures on load (BC7)", &compress)) {
                    Galeshi::Texture::SetCompressionEnabled(compress);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("BC7 compression lowers VRAM usage but makes\n"
                                      "loading significantly slower. Disabled by\n"
                                      "default for fast scene loading.");
                }
                ImGui::TextDisabled("Current: %s",
                                    Galeshi::Texture::CompressionEnabled() ? "compressed" : "uncompressed");
            }
            ImGui::Separator();
        }

        if (beginDetailsSection(
                "Transform", ImGuiTreeNodeFlags_DefaultOpen,
                "position rotation scale uniform transform location orientation "
                "detach collision shape")) {
            glm::vec3 pos = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().position : glm::vec3(0.0f);
            glm::vec3 scl = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().scale : glm::vec3(1.0f);
            glm::vec3 rot = selected[0].HasComponent<Galeshi::TransformComponent>() ? glm::degrees(glm::eulerAngles(selected[0].GetComponent<Galeshi::TransformComponent>().rotation)) : glm::vec3(0.0f);

            if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().position = pos; }


        
            if (ImGui::DragFloat3("Scale", glm::value_ptr(scl), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().scale = scl; }
            float uniformScale = scl.x;
            if (ImGui::DragFloat("Uniform Scale", &uniformScale, 0.05f)) {
                scl = glm::vec3(uniformScale);
                for (auto e : selected) {
                    if (e.HasComponent<Galeshi::TransformComponent>()) {
                        e.GetComponent<Galeshi::TransformComponent>().scale = scl;
                    }
                }
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Sets X, Y and Z scale together.");
            }
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rot), 1.0f)) { glm::quat q = glm::quat(glm::radians(rot)); for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().rotation = q; }

            if (HasCollisionShapeParent(selectedHandle)) {
                if (ImGui::Button("Detach from Collision Shape")) {
                    DetachChildFromCollisionShape(selectedHandle);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Detach this child from its collision shape while "
                        "preserving the current world transform.");
                }
            }
            if (selected[0].HasComponent<Galeshi::ColliderComponent>() &&
                !selected[0].HasComponent<Galeshi::SkeletalComponent>()) {
                int directChildren = 0;
                auto childView = reg.view<Galeshi::ParentComponent>();
                for (auto child : childView) {
                    if (childView.get<Galeshi::ParentComponent>(child).parent ==
                        selectedHandle) {
                        ++directChildren;
                    }
                }
                if (directChildren > 0) {
                    if (ImGui::Button("Detach Children")) {
                        DetachCollisionShapeChildren(selectedHandle);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Detach all direct children of this collision "
                            "shape. Skeletal parents keep their children locked.");
                    }
                }
            }
        }

        // --- Gameplay camera ----------------------------------------------
        if (selected[0].HasComponent<Galeshi::CameraComponent>()) {
            auto& camera =
                selected[0].GetComponent<Galeshi::CameraComponent>();
            if (beginDetailsSection(
                    "Camera", ImGuiTreeNodeFlags_DefaultOpen,
                    "primary game camera priority fov field of view near far "
                    "clip local offset attached parent distance view mode third "
                    "first person strafing movement")) {
                ImGui::Checkbox("Primary (Game Camera)", &camera.primary);
                ImGui::DragInt("Priority", &camera.priority, 1.0f, -100, 100);
                ImGui::DragFloat("FOV (vertical)", &camera.fovY,
                                 0.1f, 1.0f, 179.0f, "%.1f deg");
                ImGui::DragFloat("Near Clip", &camera.nearZ,
                                 0.001f, 0.0001f, 100.0f, "%.4f");
                ImGui::DragFloat("Far Clip", &camera.farZ,
                                 1.0f, 0.1f, 100000.0f, "%.1f");
                if (selected[0].HasComponent<Galeshi::TransformComponent>()) {
                    auto& cameraTransform =
                        selected[0].GetComponent<Galeshi::TransformComponent>();
                    ImGui::DragFloat3("Local Offset",
                                      glm::value_ptr(cameraTransform.position),
                                      0.01f);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Local translation from the parent (character). "
                            "Use Z for first/third-person distance.");
                    }
                }
                if (IsGameplayCameraAttachedToPlayer(selectedHandle)) {
                    int mode = std::clamp(GetGameplayCameraMode(), 0, 1);
                    const char* modeNames[] = {"Third Person", "First Person"};
                    if (ImGui::Combo("View Mode", &mode, modeNames,
                                     IM_ARRAYSIZE(modeNames))) {
                        SetGameplayCameraMode(mode);
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Switch the player camera between third-person "
                            "orbit and first-person view.");
                    }
                    ImGui::Checkbox("Strafing (A/D)", &m_playerStrafing);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "When disabled, A/D turn the player.\n"
                            "When enabled, A/D move the player sideways.");
                    }
                }
                if (selected[0].HasComponent<Galeshi::ParentComponent>()) {
                    const entt::entity parent =
                        selected[0].GetComponent<Galeshi::ParentComponent>().parent;
                    if (m_scene.Registry().valid(parent) &&
                        m_scene.Registry().all_of<Galeshi::NameComponent>(parent)) {
                        ImGui::TextDisabled("Attached to: %s",
                            m_scene.Registry().get<Galeshi::NameComponent>(parent)
                                .name.c_str());
                    } else {
                        ImGui::TextDisabled("Attached to a parent");
                    }
                } else {
                    ImGui::TextDisabled("Not attached (world camera)");
                }
                ImGui::TextDisabled(
                    "Camera pose uses this entity's Transform. When parented, "
                    "Position/Rotation are local; translate it to set "
                    "first-person/third-person distance.");
                ImGui::TextDisabled(
                    "While Play is active, the highest-priority primary camera "
                    "overrides the viewport camera.");
            }
        }

        // --- Post-process volume inspector --------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::PostProcessVolumeComponent>()) {
            auto& volume =
                sel.GetComponent<Galeshi::PostProcessVolumeComponent>();
            if (beginDetailsSection(
                    "Post-Process Volume", ImGuiTreeNodeFlags_DefaultOpen,
                    "post process bloom ssao ssr ssgi fxaa smaa taa motion blur "
                    "atmosphere fog god rays exposure contrast saturation "
                    "vignette chromatic aberration")) {
                DrawPostProcessVolumeSettings(volume);
            }
        }

        // --- Water inspector ---------------------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::WaterComponent>()) {
            auto& water = sel.GetComponent<Galeshi::WaterComponent>();
            if (beginDetailsSection(
                    "Water", ImGuiTreeNodeFlags_DefaultOpen,
                    "water waves color depth foam reflection flow speed")) {
                DrawWaterSettings(water);
            }
        }

        // --- Groom hair inspector ----------------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel) {
            // Groom hair is only meaningful for skeletal characters. In
            // particular, never expose it for landscape entities even if an
            // old scene still contains a stale GroomComponent.
            if (IsSkeletalCharacter(sel)) {
                if (sel.HasComponent<Galeshi::GroomComponent>()) {
                    auto& groom = sel.GetComponent<Galeshi::GroomComponent>();
                    if (beginDetailsSection(
                            "Groom Hair", ImGuiTreeNodeFlags_DefaultOpen,
                            "groom hair strands guides density length width "
                            "gravity stiffness color")) {
                        DrawGroomSettings(groom);
                    }
                } else if (ImGui::Button("+ Add Groom Hair")) {
                    PushHistory();
                    sel.AddComponent<Galeshi::GroomComponent>();
                }
            }
        }

        // --- Collision shape inspector -----------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::ColliderComponent>()) {
            auto& collider = sel.GetComponent<Galeshi::ColliderComponent>();
            if (beginDetailsSection(
                    "Collision Shape", ImGuiTreeNodeFlags_DefaultOpen,
                    "collision shape box sphere capsule half extents radius "
                    "height offset friction restitution trigger visible")) {
                const char* shapeNames[] = {"Box", "Sphere", "Capsule"};
                int shapeType = static_cast<int>(collider.shapeType);
                if (ImGui::Combo("Shape", &shapeType, shapeNames, 3)) {
                    collider.shapeType =
                        static_cast<Galeshi::ColliderShapeType>(shapeType);
                    RebuildPhysicsBody(sel);
                }

                bool changed = false;
                switch (collider.shapeType) {
                    case Galeshi::ColliderShapeType::Box:
                        changed |= ImGui::DragFloat3("Half Extents",
                            glm::value_ptr(collider.halfExtents), 0.01f, 0.001f, 1000.0f);
                        break;
                    case Galeshi::ColliderShapeType::Sphere:
                        changed |= ImGui::DragFloat("Radius", &collider.radius,
                                                    0.01f, 0.001f, 1000.0f);
                        break;
                    case Galeshi::ColliderShapeType::Capsule:
                        changed |= ImGui::DragFloat("Half Height", &collider.halfHeight,
                                                    0.01f, 0.001f, 1000.0f);
                        changed |= ImGui::DragFloat("Radius", &collider.radius,
                                                    0.01f, 0.001f, 1000.0f);
                        break;
                }

                // Collider placement relative to the entity. X/Y are exposed
                // separately for quick top-down placement.
                changed |= ImGui::DragFloat2("Offset XY", glm::value_ptr(collider.offset),
                                             0.01f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Moves the collision shape on local X/Y\n"
                                      "without moving the entity.");
                }
                changed |= ImGui::DragFloat("Offset Z", &collider.offset.z,
                                            0.01f);

                changed |= ImGui::DragFloat("Friction", &collider.friction,
                                            0.01f, 0.0f, 2.0f);
                changed |= ImGui::DragFloat("Restitution", &collider.restitution,
                                            0.01f, 0.0f, 1.0f);
                changed |= ImGui::Checkbox("Is Trigger", &collider.isTrigger);

                // Editor visibility is a debug-draw option only. Do not feed
                // it into the physics rebuild path: toggling the wireframe
                // must not recreate the body or disturb a mesh parented to
                // the collider entity.
                ImGui::Checkbox("Visible in Editor", &collider.visibleInEditor);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Shows or hides only the collider wireframe.\n"
                        "Meshes attached to this entity remain visible.");
                }

                if (changed) RebuildPhysicsBody(sel);

                if (ImGui::Button("Remove Collision Shape")) {
                    sel.RemoveComponent<Galeshi::ColliderComponent>();
                }
            }
        }

        // --- Rigid body inspector ----------------------------------------
        if (auto sel = m_editor.Selected(&m_scene); sel &&
            sel.HasComponent<Galeshi::RigidBodyComponent>()) {
            auto& rb = sel.GetComponent<Galeshi::RigidBodyComponent>();
            if (beginDetailsSection(
                    "Rigid Body", 0,
                    "rigid body motion type static dynamic kinematic mass "
                    "gravity lock rotation rebuild physics")) {
                const char* motionNames[] = {"Static", "Dynamic", "Kinematic"};
                int motionType = static_cast<int>(rb.motionType);
                if (ImGui::Combo("Motion Type", &motionType, motionNames, 3)) {
                    rb.motionType =
                        static_cast<Galeshi::RigidBodyComponent::MotionType>(motionType);
                    RebuildPhysicsBody(sel);
                }

                bool changed = false;
                changed |= ImGui::DragFloat("Mass", &rb.mass, 0.05f, 0.001f, 100000.0f);
                changed |= ImGui::Checkbox("Use Gravity", &rb.useGravity);
                changed |= ImGui::Checkbox("Lock Rotation", &rb.lockRotation);
                if (changed) RebuildPhysicsBody(sel);

                ImGui::TextDisabled("Runtime body: %s",
                    rb.bodyId == 0xFFFFFFFFu ? "not created (created on Play)"
                                             : "active");
                if (ImGui::Button("Rebuild Physics Body")) {
                    RebuildPhysicsBody(sel);
                }
            }
        }

        if (auto sel = m_editor.Selected(&m_scene); sel) {
            bool isLight = sel.HasComponent<Galeshi::DirectionalLightComponent>() ||
                           sel.HasComponent<Galeshi::PointLightComponent>() ||
                           sel.HasComponent<Galeshi::SpotLightComponent>();
            bool isGround = false;
            if (sel.HasComponent<Galeshi::NameComponent>() && sel.GetComponent<Galeshi::NameComponent>().name == "Ground") {
                isGround = true;
            }
            const bool isLandscape =
                sel.HasComponent<Galeshi::LandscapeComponent>();

            if (!isLight && !isGround && !selectedIsCollisionShape) {
                if (!sel.HasComponent<Galeshi::AudioSourceComponent>()) {
                    if (!isLandscape &&
                        ImGui::Button("+ Add Audio Source")) {
                        sel.AddComponent<Galeshi::AudioSourceComponent>();
                    }
                } else {
                    auto& audio = sel.GetComponent<Galeshi::AudioSourceComponent>();
                    if (beginDetailsSection(
                            "Audio Source", 0,
                            "audio sound file drop wav mp3 bus autoplay loop "
                            "spatial volume pitch attenuation distance rolloff "
                            "doppler offset")) {
                        const auto openAudioPicker = [&]() {
                            m_audioPickerTarget = sel.Handle();
                            std::string start = "assets";
                            const std::string projectPath =
                                Galeshi::ProjectManager::GetProjectPath();
                            if (!projectPath.empty()) {
                                start = (std::filesystem::path(projectPath) /
                                         "assets").string();
                            }
                            m_audioPicker.Open(
                                start, "Select Sound", {".wav", ".mp3"});
                        };

                        const std::string soundLabel =
                            audio.soundPath.empty()
                                ? "Drop WAV/MP3 sound here"
                                : std::filesystem::path(audio.soundPath)
                                      .filename()
                                      .string();
                        const float browseWidth = 82.0f;
                        const float soundWidth = std::max(
                            120.0f,
                            ImGui::GetContentRegionAvail().x - browseWidth -
                                ImGui::GetStyle().ItemSpacing.x);
                        if (ImGui::Button(
                                soundLabel.c_str(),
                                ImVec2(soundWidth, 32.0f))) {
                            openAudioPicker();
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "%s\nDrop a WAV/MP3 file from the Content "
                                "Browser, or click to browse.",
                                audio.soundPath.empty()
                                    ? "No sound assigned."
                                    : audio.soundPath.c_str());
                        }
                        if (ImGui::BeginDragDropTarget()) {
                            if (const ImGuiPayload* payload =
                                    ImGui::AcceptDragDropPayload(
                                        "GALESHI_ASSET_PATH")) {
                                const char* path =
                                    static_cast<const char*>(payload->Data);
                                if (path && IsAudioAssetPath(path)) {
                                    PushHistory();
                                    m_audio.StopSource(sel.Handle());
                                    audio.soundPath = m_assets
                                        ? m_assets->MakeRelativePath(path)
                                        : std::string(path);
                                    audio.playing = false;
                                }
                            }
                            ImGui::EndDragDropTarget();
                        }
                        ImGui::SameLine();
                        if (ImGui::SmallButton("Browse...")) {
                            openAudioPicker();
                        }
                        if (!audio.soundPath.empty()) {
                            ImGui::SameLine();
                            if (ImGui::SmallButton("Clear")) {
                                PushHistory();
                                m_audio.StopSource(sel.Handle());
                                audio.soundPath.clear();
                                audio.playing = false;
                            }
                        }

                        const char* busNames[Galeshi::kAudioBusCount] = {
                            "Master", "Music", "SFX", "Voice", "Ambient"};
                        int bus = static_cast<int>(audio.bus);
                        if (ImGui::Combo("Bus", &bus, busNames,
                                         Galeshi::kAudioBusCount)) {
                            audio.bus = static_cast<Galeshi::AudioBus>(bus);
                        }

                        ImGui::Checkbox("Autoplay", &audio.autoplay);
                        ImGui::Checkbox("Loop", &audio.loop);
                        ImGui::Checkbox("3D Spatialization", &audio.spatial);
                        ImGui::DragFloat("Volume", &audio.volume, 0.01f, 0.0f, 2.0f);
                        ImGui::DragFloat("Pitch", &audio.pitch, 0.01f, 0.05f, 4.0f);

                        if (audio.spatial) {
                            const char* attenuationNames[3] = {
                                "Inverse", "Linear", "Exponential"};
                            int attenuation = static_cast<int>(audio.attenuation);
                            if (ImGui::Combo("Attenuation", &attenuation,
                                             attenuationNames, 3)) {
                                audio.attenuation =
                                    static_cast<Galeshi::AudioAttenuationModel>(
                                        attenuation);
                            }
                            ImGui::DragFloat("Min Distance", &audio.minDistance,
                                             0.05f, 0.0f, 1000.0f);
                            ImGui::DragFloat("Max Distance", &audio.maxDistance,
                                             0.5f, 0.0f, 10000.0f);
                            ImGui::DragFloat("Rolloff", &audio.rolloff,
                                             0.01f, 0.01f, 10.0f);
                            ImGui::DragFloat("Doppler Factor", &audio.dopplerFactor,
                                             0.01f, 0.0f, 10.0f);
                            ImGui::DragFloat3("3D Offset",
                                              glm::value_ptr(audio.offset), 0.05f);
                        }

                        // World-space position of the sound for manual play.
                        glm::vec3 soundPosition(0.0f);
                        {
                            const glm::mat4 world = EntityWorldMatrix(sel.Handle());
                            glm::vec3 scale, translation, skew;
                            glm::vec4 perspective;
                            glm::quat rotation;
                            glm::decompose(world, scale, rotation, translation,
                                           skew, perspective);
                            soundPosition = translation + rotation * audio.offset;
                        }

                        if (ImGui::Button(audio.playing ? "Stop" : "Play")) {
                            if (audio.playing) {
                                m_audio.StopSource(sel.Handle());
                                audio.playing = false;
                            } else {
                                m_audio.PlaySource(sel.Handle(), audio,
                                                   soundPosition);
                                audio.playing =
                                    m_audio.IsSourcePlaying(sel.Handle());
                            }
                        }
                        ImGui::SameLine();
                        ImGui::Text("State: %s",
                                    audio.playing ? "playing" : "idle");
                        ImGui::SameLine();
                        ImGui::TextDisabled("(bus: %s)",
                                            Galeshi::AudioBusName(audio.bus));
                        if (ImGui::Button("Remove Audio Source")) {
                            m_audio.StopSource(sel.Handle());
                            sel.RemoveComponent<Galeshi::AudioSourceComponent>();
                        }
                    }
                }

                if (sel.HasComponent<Galeshi::AudioListenerComponent>()) {
                    auto& listener =
                        sel.GetComponent<Galeshi::AudioListenerComponent>();
                    if (beginDetailsSection(
                            "Audio Listener", 0,
                            "audio listener primary position orientation")) {
                        ImGui::Checkbox("Primary Listener", &listener.primary);
                        ImGui::TextDisabled(
                            "Uses this entity's world position and orientation.");
                        if (ImGui::Button("Remove Audio Listener")) {
                            sel.RemoveComponent<Galeshi::AudioListenerComponent>();
                        }
                    }
                } else if (!isLandscape &&
                           ImGui::Button("+ Add Audio Listener")) {
                    sel.AddComponent<Galeshi::AudioListenerComponent>();
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::LandscapeComponent>() &&
            beginDetailsSection(
                "Landscape (Sculpting)", ImGuiTreeNodeFlags_DefaultOpen,
                "landscape sculpt brush raise lower smooth flatten noise terrace "
                "sharpen layer paint albedo orm normal texture blend trees grass "
                "radius strength falloff height terrain")) {
            auto& landscape = selected[0].GetComponent<Galeshi::LandscapeComponent>();
            const char* toolNames[] = {
                "Select", "Raise", "Lower", "Smooth", "Flatten", "Noise",
                "Terrace", "Sharpen", "Paint Landscape", "Paint Trees",
                "Paint Grass"
            };
            int currentTool = static_cast<int>(m_editor.toolMode);
            if (ImGui::Combo("Brush", &currentTool, toolNames, IM_ARRAYSIZE(toolNames))) {
                m_editor.toolMode = static_cast<Galeshi::EditorState::ToolMode>(currentTool);
            }
            ImGui::DragFloat("Brush Radius", &m_editor.brushRadius, 0.1f, 0.25f, 100.0f);
            ImGui::DragFloat("Brush Strength", &m_editor.brushStrength, 0.1f, 0.0f, 50.0f);
            ImGui::SliderFloat("Brush Falloff", &m_editor.brushFalloff, 0.0f, 1.0f);
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Flatten) {
                ImGui::DragFloat("Flatten Height", &m_editor.flattenHeight, 0.1f, -500.0f, 500.0f);
                ImGui::SameLine();
                if (ImGui::Button("Use Cursor Height")) {
                    m_editor.flattenHeight = m_lastSculptHeight;
                }
            }
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Noise) {
                ImGui::DragFloat("Noise Frequency", &m_editor.noiseFrequency, 0.005f, 0.01f, 1.0f);
            }
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Terrace) {
                ImGui::DragFloat("Terrace Step", &m_editor.terraceStep,
                                 0.05f, 0.05f, 100.0f);
            }
            ImGui::Separator();
            ImGui::Text("Landscape Layers");
            ImGui::TextDisabled(
                "Paint Landscape paints the active layer. Baked albedo, ORM "
                "and normal maps are written to assets/textures/.");

            const size_t expectedVertices =
                static_cast<size_t>(landscape.sizeX) *
                static_cast<size_t>(landscape.sizeZ);
            // Migrate the previous single-layer material fields into the new
            // layer list the first time this landscape is inspected.
            if (landscape.layers.empty() &&
                !landscape.legacyLayerMigrated) {
                auto* legacyMaterial =
                    reg.try_get<Galeshi::MaterialComponent>(
                        selectedHandle);
                if (legacyMaterial && legacyMaterial->material) {
                    auto& legacy = *legacyMaterial->material;
                    const bool hasLegacyWeights =
                        landscape.paintWeights.size() ==
                            expectedVertices &&
                        std::any_of(
                            landscape.paintWeights.begin(),
                            landscape.paintWeights.end(),
                            [](float value) { return value > 1e-6f; });
                    if (legacy.landscapeAlbedoMap ||
                        legacy.landscapeOrmMap ||
                        legacy.landscapeNormalMap ||
                        hasLegacyWeights) {
                        Galeshi::LandscapeLayer layer;
                        layer.albedoMap = legacy.landscapeAlbedoMap;
                        layer.ormMap = legacy.landscapeOrmMap;
                        layer.normalMap = legacy.landscapeNormalMap;
                        layer.albedoSrgb = legacy.landscapeAlbedoSrgb;
                        layer.ormSrgb = legacy.landscapeOrmSrgb;
                        layer.normalSrgb = legacy.landscapeNormalSrgb;
                        landscape.layers.push_back(std::move(layer));
                        landscape.legacyLayerMigrated = true;
                        landscape.layerWeights.emplace_back(
                            expectedVertices, 0.0f);
                        if (landscape.paintWeights.size() ==
                            expectedVertices) {
                            landscape.layerWeights[0] =
                                landscape.paintWeights;
                        }
                    }
                }
            }
            if (landscape.layerWeights.size() != landscape.layers.size()) {
                landscape.layerWeights.resize(landscape.layers.size());
            }
            for (auto& weights : landscape.layerWeights) {
                if (weights.size() != expectedVertices) {
                    weights.resize(expectedVertices, 0.0f);
                }
            }
            if (landscape.layers.empty()) {
                landscape.activeLayer = 0;
            } else {
                landscape.activeLayer = std::clamp(
                    landscape.activeLayer, 0,
                    static_cast<int>(landscape.layers.size()) - 1);
            }

            if (ImGui::Button("+ Add Layer")) {
                PushHistory();
                landscape.layers.push_back(Galeshi::LandscapeLayer{});
                landscape.layerWeights.emplace_back(expectedVertices, 0.0f);
                landscape.activeLayer =
                    static_cast<int>(landscape.layers.size()) - 1;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%zu layer(s)", landscape.layers.size());

            int removeLayer = -1;
            for (size_t i = 0; i < landscape.layers.size(); ++i) {
                auto& layer = landscape.layers[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Separator();
                const ImGuiTreeNodeFlags layerFlags =
                    i == 0 ? ImGuiTreeNodeFlags_DefaultOpen
                           : ImGuiTreeNodeFlags_None;
                if (ImGui::CollapsingHeader(
                        ("Layer " + std::to_string(i + 1)).c_str(),
                        layerFlags)) {
                    if (ImGui::RadioButton(
                            "Active paint layer",
                            landscape.activeLayer ==
                                static_cast<int>(i))) {
                        landscape.activeLayer = static_cast<int>(i);
                    }
                    DrawTextureSlot("Albedo", layer.albedoMap, true,
                                    &layer.albedoSrgb);
                    DrawTextureSlot("ORM", layer.ormMap, false,
                                    &layer.ormSrgb);
                    DrawTextureSlot("Normal", layer.normalMap, false,
                                    &layer.normalSrgb);
                    if (ImGui::Button("- Remove Layer")) {
                        removeLayer = static_cast<int>(i);
                    }
                }
                ImGui::PopID();
            }
            if (removeLayer >= 0) {
                PushHistory();
                landscape.layers.erase(
                    landscape.layers.begin() + removeLayer);
                if (landscape.layerWeights.size() ==
                    landscape.layers.size() + 1) {
                    landscape.layerWeights.erase(
                        landscape.layerWeights.begin() + removeLayer);
                }
                if (removeLayer < landscape.activeLayer) {
                    --landscape.activeLayer;
                }
                if (landscape.layers.empty()) {
                    landscape.activeLayer = 0;
                } else {
                    landscape.activeLayer = std::clamp(
                        landscape.activeLayer, 0,
                        static_cast<int>(landscape.layers.size()) - 1);
                }
            }

            if (ImGui::Button("Bake Layer Textures")) {
                PushHistory();
                BakeLandscapeTextures(selected[0]);
            }
            ImGui::SameLine();
            ImGui::TextDisabled(
                "Suggested: paint a stroke to bake automatically.");
            ImGui::TextDisabled("%d x %d vertices, scale %.2f", landscape.sizeX, landscape.sizeZ, landscape.scale);
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Select) {
                ImGui::TextDisabled("Select a brush, then left-click and drag in the viewport.");
            } else if (m_editor.toolMode ==
                           Galeshi::EditorState::ToolMode::PaintLandscape ||
                       m_editor.toolMode ==
                           Galeshi::EditorState::ToolMode::PaintTrees ||
                       m_editor.toolMode ==
                           Galeshi::EditorState::ToolMode::PaintGrass) {
                ImGui::TextColored(
                    ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                    "%s with brush radius %.2f",
                    toolNames[currentTool], m_editor.brushRadius);
            } else {
                ImGui::TextColored(ImVec4(0.5f, 1.0f, 0.5f, 1.0f),
                                   "Sculpting with %s brush", toolNames[currentTool]);
            }
            if (ImGui::Button("Reset Heights")) {
                PushHistory();
                Galeshi::LandscapeMesh::ResetHeights(landscape);
                if (selected[0].HasComponent<Galeshi::MeshComponent>()) {
                    auto& meshComponent = selected[0].GetComponent<Galeshi::MeshComponent>();
                    if (meshComponent.mesh) {
                        Galeshi::LandscapeMesh::UpdateMesh(landscape, *meshComponent.mesh);
                        Galeshi::LandscapeMesh::UpdatePaintWeights(
                            landscape, *meshComponent.mesh);
                    }
                }
                if (selected[0].HasComponent<Galeshi::VegetationComponent>()) {
                    selected[0].GetComponent<Galeshi::VegetationComponent>().dirty = true;
                }
            }
            ImGui::SameLine();
            if (!selected[0].HasComponent<Galeshi::VegetationComponent>()) {
                if (ImGui::Button("Add Vegetation")) {
                    PushHistory();
                    auto& vegetation = selected[0].AddComponent<Galeshi::VegetationComponent>();
                    Galeshi::VegetationGenerator::Generate(landscape, vegetation);
                }
            } else {
                ImGui::TextDisabled("Vegetation component is attached");
            }
        }

        if (selected[0].HasComponent<Galeshi::VegetationComponent>() &&
            beginDetailsSection(
                "Vegetation (Instanced)", ImGuiTreeNodeFlags_DefaultOpen,
                "vegetation trees grass instanced density scale slope seed "
                "colors models painting shadows leaf leaves foliage textures "
                "albedo orm normal")) {
            auto& veg = selected[0].GetComponent<Galeshi::VegetationComponent>();
            bool changed = false;
            changed |= ImGui::Checkbox("Enabled", &veg.enabled);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Tree Shadows", &veg.castShadows);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Grass Shadows", &veg.grassCastShadows);
            ImGui::SetNextItemWidth(170.0f);
            changed |= ImGui::DragInt("Trees", &veg.treeCount, 1.0f, 0, 20000);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Base generated tree count. Each tree species scales its "
                    "count by its Density.");
            }
            ImGui::SetNextItemWidth(170.0f);
            changed |= ImGui::DragInt("Grass", &veg.grassCount, 10.0f, 0, 50000);
            changed |= ImGui::DragFloatRange2("Tree Scale", &veg.treeMinScale, &veg.treeMaxScale,
                                              0.01f, 0.05f, 10.0f, "Min %.2f", "Max %.2f");
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Scale range for default procedural trees. Custom tree "
                    "types use their own Size.");
            }
            changed |= ImGui::DragFloatRange2("Grass Scale", &veg.grassMinScale, &veg.grassMaxScale,
                                              0.01f, 0.05f, 10.0f, "Min %.2f", "Max %.2f");
            changed |= ImGui::DragFloat("Slope Limit", &veg.slopeLimit, 0.01f, 0.0f, 5.0f);
            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &veg.seed);
            changed |= ImGui::ColorEdit4("Trunk Color", glm::value_ptr(veg.trunkColor));
            changed |= ImGui::ColorEdit4("Canopy Color", glm::value_ptr(veg.canopyColor));
            changed |= ImGui::ColorEdit4("Grass Color", glm::value_ptr(veg.grassColor));

            ImGui::Separator();
            ImGui::Text("Tree Models");
            changed |= ImGui::Checkbox("Hide Default Instances",
                                       &veg.hideDefaultInstances);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Hide generated procedural trees and grass. Authored tree "
                    "species and painted vegetation stay visible.");
            }

            const float dropWidth = ImGui::GetContentRegionAvail().x;
            for (int typeIndex = 0;
                 typeIndex < static_cast<int>(veg.treeTypes.size());
                 ++typeIndex) {
                auto& type = veg.treeTypes[static_cast<size_t>(typeIndex)];
                EnsureTreeTypeMaterial(type, typeIndex);
                ImGui::PushID(typeIndex);
                ImGui::Separator();
                ImGui::Text("Tree Type %d", typeIndex + 1);
                ImGui::SameLine();
                if (ImGui::SmallButton("Remove")) {
                    PushHistory();
                    veg.treeTypes.erase(veg.treeTypes.begin() + typeIndex);
                    if (m_paintTreeType >= typeIndex) --m_paintTreeType;
                    changed = true;
                    ImGui::PopID();
                    break;
                }

                ImGui::InvisibleButton("##tree_type_drop",
                                       ImVec2(dropWidth, 44.0f));
                const ImVec2 dropMin = ImGui::GetItemRectMin();
                const ImVec2 dropMax = ImGui::GetItemRectMax();
                const bool dropHovered = ImGui::IsItemHovered();
                ImDrawList* drawList = ImGui::GetWindowDrawList();
                drawList->AddRectFilled(
                    dropMin, dropMax,
                    dropHovered ? IM_COL32(44, 48, 56, 255)
                                : IM_COL32(28, 30, 34, 255), 4.0f);
                drawList->AddRect(
                    dropMin, dropMax,
                    dropHovered ? IM_COL32(120, 160, 220, 255)
                                : IM_COL32(80, 84, 92, 255), 4.0f);
                if (type.meshPath.empty()) {
                    drawList->AddText(
                        ImVec2(dropMin.x + 10.0f, dropMin.y + 7.0f),
                        IM_COL32(200, 205, 215, 255),
                        "Drop a tree model here");
                    drawList->AddText(
                        ImVec2(dropMin.x + 10.0f, dropMin.y + 25.0f),
                        IM_COL32(130, 135, 145, 255),
                        "glTF / GLB / OBJ / DAE");
                } else {
                    const ImVec4 clipRect(dropMin.x, dropMin.y, dropMax.x,
                                          dropMax.y);
                    drawList->AddText(
                        ImGui::GetFont(), ImGui::GetFontSize(),
                        ImVec2(dropMin.x + 10.0f, dropMin.y + 7.0f),
                        IM_COL32(220, 225, 235, 255), type.meshPath.c_str(),
                        nullptr, dropWidth - 20.0f, &clipRect);
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Drag a model from the Content Browser or click to choose.\n"
                        "The model keeps its native size; use Size for scaling.");
                }
                if (ImGui::IsItemClicked()) {
                    m_vegetationMeshTarget = selected[0].Handle();
                    m_vegetationMeshTypeIndex = typeIndex;
                    std::string start = "assets";
                    const std::string projectPath =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (!projectPath.empty()) {
                        start = (std::filesystem::path(projectPath) /
                                 "assets").string();
                    }
                    m_vegetationMeshPicker.Open(
                        start, "Select Tree Model",
                        {".gltf", ".glb", ".obj", ".dae"});
                }
                if (ImGui::BeginDragDropTarget()) {
                    if (const ImGuiPayload* payload =
                            ImGui::AcceptDragDropPayload(
                                "GALESHI_ASSET_PATH")) {
                        const char* path =
                            static_cast<const char*>(payload->Data);
                        const std::string candidate = path ? path : "";
                        if (IsDragDropModelAssetPath(candidate)) {
                            AssignLandscapeTreeModel(
                                selected[0], candidate, typeIndex);
                        }
                    }
                    ImGui::EndDragDropTarget();
                }

                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat("Density", &type.density,
                                     0.01f, 0.0f, 100.0f, "%.2f")) {
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Scales this species' generated tree count. With "
                        "multiple species, 1.0 gives each an equal share of "
                        "the base Trees count.");
                }
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat("Size", &type.scale, 0.001f,
                                     0.001f, 100.0f, "%.3f")) {
                    changed = true;
                }
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Multiplier of the model's native size. Dropped models "
                        "start at 0.010.");
                }

                // Each authored tree species owns its own texture row. Leave
                // the Landscape material texture row above untouched; this
                // override is only used when at least one slot is assigned.
                if (ImGui::TreeNodeEx("Whole Tree Textures",
                                      ImGuiTreeNodeFlags_None)) {
                    ImGui::TextDisabled(
                        "Drag textures from the Content Browser. Empty slots "
                        "use the tree model's imported materials.");
                    if (type.material) {
                        DrawTextureSlot("Albedo Map",
                                        type.material->albedoMap, true,
                                        &type.material->albedoSrgb);
                        DrawTextureSlot("Normal Map",
                                        type.material->normalMap, false,
                                        &type.material->normalSrgb);
                        DrawTextureSlot("Metallic/Roughness",
                                        type.material->metallicRoughnessMap,
                                        false,
                                        &type.material->metallicRoughnessSrgb);
                        DrawTextureSlot("Metallic Map",
                                        type.material->metallicMap, false,
                                        &type.material->metallicSrgb);
                        DrawTextureSlot("Roughness Map",
                                        type.material->roughnessMap, false,
                                        &type.material->roughnessSrgb);
                        DrawTextureSlot("Emissive Map",
                                        type.material->emissiveMap, true,
                                        &type.material->emissiveSrgb);
                        DrawTextureSlot("Occlusion Map",
                                        type.material->occlusionMap, false,
                                        &type.material->occlusionSrgb);
                        DrawTextureSlot("Opacity Map",
                                        type.material->opacityMap, false,
                                        &type.material->opacitySrgb);
                        DrawTextureSlot("Specular Map",
                                        type.material->specularMap, false,
                                        &type.material->specularSrgb);
                    }
                    ImGui::TreePop();
                }

                if (ImGui::TreeNodeEx(
                        "Leaf Textures",
                        ImGuiTreeNodeFlags_DefaultOpen)) {
                    ImGui::TextDisabled(
                        "Add one set per leaf mesh. Each set overrides only "
                        "its target mesh; the trunk keeps the model material.");
                    Galeshi::AssetManager::LoadedModel treeModel;
                    if (!type.meshPath.empty()) {
                        treeModel = m_assets->LoadModel(type.meshPath);
                    }
                    if (type.meshPath.empty()) {
                        ImGui::TextDisabled(
                            "Assign a tree model first, then add leaf sets.");
                    } else if (treeModel.meshes.empty()) {
                        ImGui::TextDisabled(
                            "The tree model has no usable meshes.");
                    } else {
                        std::vector<std::string> meshLabels;
                        meshLabels.reserve(treeModel.meshes.size());
                        for (int meshIndex = 0;
                             meshIndex <
                             static_cast<int>(treeModel.meshes.size());
                             ++meshIndex) {
                            meshLabels.push_back(
                                TreeMeshLabel(treeModel, meshIndex));
                        }

                        int removeLeaf = -1;
                        for (int leafIndex = 0;
                             leafIndex <
                             static_cast<int>(type.leafSets.size());
                             ++leafIndex) {
                            auto& leaf = type.leafSets[
                                static_cast<size_t>(leafIndex)];
                            EnsureLeafMaterial(leaf, typeIndex, leafIndex);
                            ImGui::PushID(leafIndex);
                            ImGui::Separator();
                            ImGui::Text("Leaf %d", leafIndex + 1);
                            ImGui::SameLine();
                            if (ImGui::SmallButton("- Remove Leaf")) {
                                removeLeaf = leafIndex;
                            }

                            const char* preview =
                                leaf.meshIndex >= 0 &&
                                    leaf.meshIndex <
                                        static_cast<int>(
                                            meshLabels.size())
                                    ? meshLabels[static_cast<size_t>(
                                          leaf.meshIndex)].c_str()
                                    : "Select leaf mesh";
                            ImGui::SetNextItemWidth(260.0f);
                            if (ImGui::BeginCombo("Leaf Mesh", preview)) {
                                for (int meshIndex = 0;
                                     meshIndex <
                                     static_cast<int>(meshLabels.size());
                                     ++meshIndex) {
                                    const bool isSelected =
                                        leaf.meshIndex == meshIndex;
                                    ImGui::PushID(meshIndex);
                                    if (ImGui::Selectable(
                                            meshLabels[static_cast<size_t>(
                                                meshIndex)].c_str(),
                                            isSelected)) {
                                        PushHistory();
                                        leaf.meshIndex = meshIndex;
                                    }
                                    if (isSelected) {
                                        ImGui::SetItemDefaultFocus();
                                    }
                                    ImGui::PopID();
                                }
                                ImGui::EndCombo();
                            }
                            if (leaf.meshIndex < 0 ||
                                leaf.meshIndex >=
                                    static_cast<int>(meshLabels.size())) {
                                ImGui::TextColored(
                                    ImVec4(1.0f, 0.7f, 0.35f, 1.0f),
                                    "Choose the imported mesh this leaf "
                                    "material replaces.");
                            } else {
                                for (int other = 0;
                                     other <
                                     static_cast<int>(
                                         type.leafSets.size());
                                     ++other) {
                                    if (other == leafIndex) continue;
                                    if (type.leafSets[static_cast<size_t>(
                                            other)].meshIndex ==
                                        leaf.meshIndex) {
                                        ImGui::TextColored(
                                            ImVec4(1.0f, 0.7f, 0.35f, 1.0f),
                                            "Mesh also used by Leaf %d; "
                                            "the first matching set wins.",
                                            other + 1);
                                        break;
                                    }
                                }
                            }

                            if (leaf.material) {
                                DrawTextureSlot(
                                    "Albedo",
                                    leaf.material->albedoMap, true,
                                    &leaf.material->albedoSrgb);
                                DrawTextureSlot(
                                    "ORM",
                                    leaf.material->metallicRoughnessMap,
                                    false,
                                    &leaf.material->
                                        metallicRoughnessSrgb);
                                // ORM is authored as one texture: R=AO,
                                // G=roughness, B=metallic. Bind it to both
                                // material slots the PBR shader reads.
                                leaf.material->occlusionMap =
                                    leaf.material->metallicRoughnessMap;
                                leaf.material->occlusionSrgb =
                                    leaf.material->metallicRoughnessSrgb;
                                if (leaf.material->metallicRoughnessMap) {
                                    leaf.material->metallicFactor = 1.0f;
                                    leaf.material->roughnessFactor = 1.0f;
                                } else {
                                    leaf.material->metallicFactor = 0.0f;
                                    leaf.material->roughnessFactor = 1.0f;
                                }
                                DrawTextureSlot(
                                    "Normal",
                                    leaf.material->normalMap, false,
                                    &leaf.material->normalSrgb);
                                ImGui::TextDisabled(
                                    "ORM: R occlusion, G roughness, "
                                    "B metallic.");

                                bool twoSided =
                                    !leaf.material->backfaceCulling;
                                if (ImGui::Checkbox(
                                        "Two Sided", &twoSided)) {
                                    PushHistory();
                                    leaf.material->twoSided = twoSided;
                                    leaf.material->backfaceCulling =
                                        !twoSided;
                                }
                                bool alphaCutout =
                                    leaf.material->alphaTest;
                                if (ImGui::Checkbox(
                                        "Alpha Cutout",
                                        &alphaCutout)) {
                                    PushHistory();
                                    leaf.material->alphaTest =
                                        alphaCutout;
                                }
                                if (leaf.material->alphaTest) {
                                    ImGui::SetNextItemWidth(160.0f);
                                    ImGui::SliderFloat(
                                        "Alpha Cutoff",
                                        &leaf.material->alphaCutoff,
                                        0.0f, 1.0f);
                                }
                            }
                            ImGui::PopID();
                        }
                        if (removeLeaf >= 0) {
                            PushHistory();
                            type.leafSets.erase(
                                type.leafSets.begin() + removeLeaf);
                        }
                        if (ImGui::Button("+ Add Leaf")) {
                            PushHistory();
                            Galeshi::VegetationLeafSet leaf;
                            leaf.meshIndex = SuggestedLeafMeshIndex(
                                treeModel, type.leafSets);
                            EnsureLeafMaterial(
                                leaf, typeIndex,
                                static_cast<int>(type.leafSets.size()));
                            type.leafSets.push_back(std::move(leaf));
                        }
                        if (type.leafSets.empty()) {
                            ImGui::SameLine();
                            ImGui::TextDisabled(
                                "No leaf mesh overrides.");
                        }
                    }
                    ImGui::TreePop();
                }

                ImGui::TextDisabled("Painted: %zu",
                                    type.paintedInstances.size());
                ImGui::PopID();
            }

            if (ImGui::Button("+ Add Tree Type")) {
                PushHistory();
                veg.treeTypes.push_back(Galeshi::VegetationTreeType{});
                const int newIndex =
                    static_cast<int>(veg.treeTypes.size()) - 1;
                EnsureTreeTypeMaterial(veg.treeTypes.back(), newIndex);
                m_paintTreeType = newIndex;
                changed = true;
            }
            if (veg.treeTypes.empty()) {
                ImGui::SameLine();
                ImGui::TextDisabled("Empty = procedural trees");
            }

            ImGui::Separator();
            ImGui::Text("Painting");
            {
                std::vector<std::string> paintLabelStorage;
                paintLabelStorage.push_back("Default Procedural");
                for (const auto& type : veg.treeTypes) {
                    paintLabelStorage.push_back(
                        type.meshPath.empty()
                            ? "Tree Type"
                            : std::filesystem::path(type.meshPath)
                                  .stem().string());
                }
                std::vector<const char*> paintLabels;
                paintLabels.reserve(paintLabelStorage.size());
                for (const auto& label : paintLabelStorage)
                    paintLabels.push_back(label.c_str());

                int paintSelection = m_paintTreeType + 1;
                if (paintSelection < 0) paintSelection = 0;
                if (paintSelection >= static_cast<int>(paintLabels.size()))
                    paintSelection = 0;
                ImGui::SetNextItemWidth(220.0f);
                if (ImGui::Combo("Paint Tree Type", &paintSelection,
                                 paintLabels.data(),
                                 static_cast<int>(paintLabels.size()))) {
                    m_paintTreeType = paintSelection - 1;
                }
            }
            ImGui::DragFloat("Paint Density", &veg.paintDensity,
                             0.05f, 0.0f, 10.0f);
            ImGui::DragFloat("Paint Size", &veg.paintSize,
                             0.05f, 0.05f, 20.0f);
            if (ImGui::Button("Paint Trees")) {
                m_editor.toolMode = Galeshi::EditorState::ToolMode::PaintTrees;
                m_editor.Select(selected[0]);
                m_wasSculpting = false;
                m_paintStampValid = false;
                // When a landscape already has authored species, start with
                // the first one instead of silently painting procedural trees.
                if (!veg.treeTypes.empty() &&
                    (m_paintTreeType < 0 ||
                     m_paintTreeType >= static_cast<int>(veg.treeTypes.size()))) {
                    m_paintTreeType = 0;
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Paint Grass")) {
                m_editor.toolMode = Galeshi::EditorState::ToolMode::PaintGrass;
                m_editor.Select(selected[0]);
                m_wasSculpting = false;
                m_paintStampValid = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Use Brush Radius in Landscape Sculpting");

            if (changed) veg.dirty = true;
            const bool hasLandscape = selected[0].HasComponent<Galeshi::LandscapeComponent>();
            if (!hasLandscape) ImGui::BeginDisabled();
            if (ImGui::Button("Regenerate")) {
                PushHistory();
                Galeshi::VegetationGenerator::Generate(
                    selected[0].GetComponent<Galeshi::LandscapeComponent>(), veg);
            }
            if (!hasLandscape) ImGui::EndDisabled();
            ImGui::SameLine();
            if (ImGui::Button("Clear Instances")) {
                PushHistory();
                Galeshi::VegetationGenerator::Clear(veg);
            }
            ImGui::SameLine();
            size_t treeTotal = veg.treeInstances.size() +
                veg.paintedTreeInstances.size();
            size_t grassTotal = veg.grassInstances.size() +
                veg.paintedGrassInstances.size();
            for (const auto& type : veg.treeTypes) {
                treeTotal += type.generatedInstances.size() +
                             type.paintedInstances.size();
            }
            ImGui::TextDisabled("%zu trees / %zu grass", treeTotal, grassTotal);
        }

        auto materialUserCount = [&](const Galeshi::Material* material) {
            size_t count = 0;
            auto view = m_scene.Registry().view<Galeshi::MaterialComponent>();
            for (auto handle : view) {
                if (view.get<Galeshi::MaterialComponent>(handle).material.get() == material) {
                    ++count;
                }
            }
            return count;
        };

        // Every renderable mesh owns a material slot, even when the imported
        // asset provided an unnamed material without textures. Create a
        // placeholder slot on demand so Details never silently drops it.
        auto ensureMaterialSlot =
            [&](Galeshi::Entity entity) -> Galeshi::MaterialComponent* {
                if (!entity) return nullptr;
                auto& registry = m_scene.Registry();
                Galeshi::MaterialComponent* slot = nullptr;
                if (registry.all_of<Galeshi::MaterialComponent>(entity.Handle())) {
                    slot = &registry.get<Galeshi::MaterialComponent>(entity.Handle());
                } else {
                    if (!registry.all_of<Galeshi::MeshComponent>(entity.Handle()))
                        return nullptr;
                    slot = &registry.emplace<Galeshi::MaterialComponent>(
                        entity.Handle());
                }
                if (!slot->material) {
                    slot->material = std::make_shared<Galeshi::Material>();
                }
                // Do not synthesize Material::name here. The displayed label
                // falls back to the entity/object name, while the serialized
                // material name remains exactly the exported one.
                return slot;
            };

        auto materialStateHash = [](const Galeshi::Material& mat) {
            size_t h = 1469598103934665603ull;
            auto addHash = [&](size_t value) {
                h ^= value + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
                h *= 1099511628211ull;
            };
            auto addFloat = [&](float value) {
                addHash(std::hash<float>{}(value));
            };
            auto addVec3 = [&](const glm::vec3& value) {
                addFloat(value.x); addFloat(value.y); addFloat(value.z);
            };
            auto addVec4 = [&](const glm::vec4& value) {
                addFloat(value.x); addFloat(value.y);
                addFloat(value.z); addFloat(value.w);
            };
            auto addMap = [&](const std::shared_ptr<Galeshi::Texture>& map) {
                addHash(reinterpret_cast<uintptr_t>(map.get()));
            };

            addHash(std::hash<std::string>{}(mat.name));
            addHash(std::hash<std::string>{}(mat.sourcePath));
            addVec4(mat.albedoFactor);
            addHash(mat.albedoSrgb);
            addHash(mat.metallicRoughnessSrgb);
            addHash(mat.metallicSrgb);
            addHash(mat.roughnessSrgb);
            addHash(mat.normalSrgb);
            addHash(mat.emissiveSrgb);
            addHash(mat.occlusionSrgb);
            addHash(mat.opacitySrgb);
            addHash(mat.specularSrgb);
            addHash(mat.landscapeAlbedoSrgb);
            addHash(mat.landscapeOrmSrgb);
            addHash(mat.landscapeNormalSrgb);
            addHash(mat.landscapeBlend);
            addFloat(mat.metallicFactor);
            addFloat(mat.specularFactor);
            addFloat(mat.roughnessFactor);
            addVec3(mat.emissiveFactor);
            addHash(mat.isGlass);
            addHash(mat.twoSided);
            addHash(mat.backfaceCulling);
            addFloat(mat.glassOpacity);
            addFloat(mat.normalStrength);
            addFloat(mat.occlusionStrength);
            addFloat(mat.alphaCutoff);
            addHash(mat.alphaTest);
            addHash(mat.alphaBlend);
            addHash(mat.subsurfaceEnabled);
            addVec3(mat.subsurfaceColor);
            addFloat(mat.subsurfaceStrength);
            addFloat(mat.subsurfaceRadius);
            addHash(mat.translucencyEnabled);
            addVec3(mat.translucencyColor);
            addFloat(mat.translucencyOpacity);
            addFloat(mat.translucencyStrength);
            addFloat(mat.translucencyThickness);
            addFloat(mat.translucencyDistortion);
            addFloat(mat.translucencyShadowDepth);
            addHash(mat.castsShadow);
            addFloat(mat.shadowDepthOffset);
            addMap(mat.albedoMap);
            addMap(mat.metallicRoughnessMap);
            addMap(mat.metallicMap);
            addMap(mat.roughnessMap);
            addMap(mat.normalMap);
            addMap(mat.emissiveMap);
            addMap(mat.occlusionMap);
            addMap(mat.opacityMap);
            addMap(mat.specularMap);
            addMap(mat.landscapeAlbedoMap);
            addMap(mat.landscapeOrmMap);
            addMap(mat.landscapeNormalMap);
            addHash(reinterpret_cast<uintptr_t>(mat.base.get()));
            return h;
        };

        // Helper lambda to draw material UI safely. During a Details search
        // the header stays visible when the material name or any material
        // property matches; inside, only matching parameter groups remain.
        auto DrawMaterialUI = [&](const char* label, Galeshi::MaterialComponent& matc, uint32_t eid) {
            if (!matc.material) return;
            auto& m = *matc.material;
            const size_t materialStateBefore = materialStateHash(m);
            ImGui::PushID(eid);
            const std::string materialKeywords =
                std::string("material instance base used "
                            "albedo factor albedo sRGB color "
                            "space metallic factor roughness factor normal "
                            "strength occlusion strength "
                            "alpha cutoff alpha test albedo alpha mask alpha blend transparency hair "
                            "glass transparent opacity two sided "
                            "emissive color subsurface scattering sss enable "
                            "subsurface subsurface color subsurface strength "
                            "subsurface radius translucency enable translucency "
                            "translucency color translucency opacity "
                            "translucency strength translucency thickness "
                            "translucency distortion shadows cast shadows shadow "
                            "sharpness shadow depth offset texture maps albedo "
                            "map normal map metallic roughness map metallic map "
                            "roughness map emissive map occlusion map opacity map "
                            "specular map material name") +
                " " + (label ? label : "") + " " + m.name +
                (m.base ? " " + m.base->name : std::string{});
            bool materialChanged = false;
            if (beginDetailsSection(label, ImGuiTreeNodeFlags_DefaultOpen,
                                    materialKeywords.c_str())) {
                const bool showAllMaterialFields =
                    !searchActive || searchContains(label) ||
                    searchContains(m.name.c_str()) ||
                    searchContains("material");
                auto showMaterialField = [&](const char* field,
                                             const char* aliases = "") {
                    if (showAllMaterialFields) return true;
                    return lowerAscii(std::string(field ? field : "") + " " +
                                      (aliases ? aliases : ""))
                               .find(searchLower) != std::string::npos;
                };
                auto showMaterialGroup = [&](const char* title,
                                             const char* members) {
                    return showAllMaterialFields || showMaterialField(title, members);
                };

                // Two Sided and Backface Culling are independent per-material
                // settings; editing one must never change the other.
                ImGui::Checkbox("Two Sided", &m.twoSided);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Flip normals on back-facing fragments for two-sided "
                        "shading.");
                }
                ImGui::SameLine();
                ImGui::Checkbox("Backface Culling", &m.backfaceCulling);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Cull faces pointing away from the camera. This is "
                        "independent of Two Sided.");
                }
                ImGui::Separator();

                const size_t userCount = materialUserCount(&m);
                if (showMaterialGroup(
                        "Material Identity",
                        "name instance base used reset create instance")) {
                if (m.base) {
                    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f),
                        "Instance of: %s", m.base->name.c_str());
                    if (ImGui::Button("Reset to Base")) {
                        const auto base = m.base;
                        const std::string instanceName = m.name;
                        m = *base;
                        m.base = base;
                        m.name = instanceName;
                        m.sceneOverride = true;
                        matc.isGlass = m.isGlass;
                        PushHistory();
                        ImGui::PopID();
                        return;
                    }
                } else {
                    ImGui::TextDisabled("Base material");
                }
                ImGui::SameLine();
                ImGui::TextDisabled("| Used by %zu object%s", userCount, userCount == 1 ? "" : "s");
                if (userCount > 1) {
                    if (ImGui::Button("Create Instance")) {
                        auto base = matc.material;
                        auto instance = std::make_shared<Galeshi::Material>(*base);
                        instance->base = base;
                        instance->name = base->name.empty()
                            ? std::string("Material Instance")
                            : base->name + " Instance";
                        matc.material = instance;
                        matc.material->sceneOverride = true;
                        matc.isGlass = instance->isGlass;
                        PushHistory();
                        ImGui::PopID();
                        return;
                    }
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Create an independent material instance.\n"
                                          "Texture maps are shared, parameters can be edited separately.");
                    }
                }

                char nameBuffer[128];
                std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", m.name.c_str());
                if (ImGui::InputText("Material Name", nameBuffer, sizeof(nameBuffer))) {
                    m.name = nameBuffer;
                }
                }

                if (showMaterialGroup(
                        "Main Parameters",
                        "albedo color metallic specular reflection roughness "
                        "normal occlusion alpha cutoff glass transparent opacity "
                        "two sided emissive")) {
                ImGui::Separator();

                if (showMaterialField("Albedo Factor", "albedo color base"))
                ImGui::ColorEdit4("Albedo Factor", glm::value_ptr(m.albedoFactor));
                if (showMaterialField("Metallic Factor", "metal metallic"))
                ImGui::SliderFloat("Metallic Factor",  &m.metallicFactor,  0.0f, 1.0f);
                if (showMaterialField("Specular Factor", "specular reflection"))
                ImGui::SliderFloat("Specular Factor", &m.specularFactor, 0.0f, 1.0f);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip(
                        "Scales the dielectric specular response. The optional "
                        "specular map multiplies this value.");
                }
                if (showMaterialField("Roughness Factor", "rough"))
                ImGui::SliderFloat("Roughness Factor", &m.roughnessFactor, 0.04f, 1.0f);
                if (showMaterialField("Normal Strength", "normal bump"))
                ImGui::SliderFloat("Normal Strength",  &m.normalStrength,  0.0f, 2.0f);
                if (showMaterialField("Occlusion Strength", "occlusion ao"))
                ImGui::SliderFloat("Occlusion Strength", &m.occlusionStrength, 0.0f, 1.0f);
                if (showMaterialField("Alpha Test", "alpha test cutoff mask albedo")) {
                    ImGui::Checkbox("Alpha Test (albedo alpha)", &m.alphaTest);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Discard fragments using the albedo texture alpha.\n"
                            "Used by glTF MASK materials and nearly opaque cutouts.");
                    }
                }
                if (showMaterialField("Alpha Blend", "alpha blend transparency hair")) {
                    ImGui::Checkbox("Alpha Blend (transparent pass)", &m.alphaBlend);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip(
                            "Render in the sorted alpha-blended forward pass.\n"
                            "Used by glTF BLEND cards such as brows and eyelashes.");
                    }
                }
                if (showMaterialField("Alpha Cutoff", "alpha cutoff mask"))
                ImGui::SliderFloat("Alpha Cutoff",     &m.alphaCutoff,     0.0f, 1.0f);
                if (showMaterialField("Glass (Transparent)", "glass transparent"))
                ImGui::Checkbox("Glass (Transparent)", &m.isGlass);
                if (showMaterialField("Glass Opacity", "glass opacity transparent") && m.isGlass) {
                    ImGui::SliderFloat("Glass Opacity", &m.glassOpacity, 0.0f, 1.0f);
                }
                if (showMaterialField("Emissive Color", "emissive emission"))
                ImGui::ColorEdit3("Emissive Color", glm::value_ptr(m.emissiveFactor));
                }

                if (showMaterialGroup(
                        "Subsurface Scattering",
                        "subsurface scattering sss skin wax leaves enable color "
                        "strength radius")) {
                ImGui::Separator();
                ImGui::Text("Subsurface Scattering");
                ImGui::Checkbox("Enable Subsurface", &m.subsurfaceEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Wrapped diffuse/back-scatter lighting for skin, wax and leaves.\n"
                                      "Evaluated in the deferred lighting pass.");
                }
                if (m.subsurfaceEnabled) {
                    ImGui::ColorEdit3("Subsurface Color", glm::value_ptr(m.subsurfaceColor));
                    ImGui::SliderFloat("Subsurface Strength", &m.subsurfaceStrength, 0.0f, 2.0f);
                    ImGui::SliderFloat("Subsurface Radius", &m.subsurfaceRadius, 0.01f, 5.0f);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("Scattering radius in world units.");
                    }
                }
                }

                if (showMaterialGroup(
                        "Translucency",
                        "translucency translucent transmission enable color opacity "
                        "strength thickness distortion shadow depth")) {
                ImGui::Separator();
                ImGui::Text("Translucency");
                ImGui::Checkbox("Enable Translucency", &m.translucencyEnabled);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Render this material in the lit forward translucency pass.\n"
                                      "Backlight is transmitted through shadow depth.");
                }
                if (m.translucencyEnabled) {
                    ImGui::ColorEdit3("Translucency Color", glm::value_ptr(m.translucencyColor));
                    ImGui::SliderFloat("Translucency Opacity", &m.translucencyOpacity, 0.0f, 1.0f);
                    ImGui::SliderFloat("Translucency Strength", &m.translucencyStrength, 0.0f, 4.0f);
                    ImGui::SliderFloat("Translucency Thickness", &m.translucencyThickness, 0.0f, 1.0f);
                    ImGui::SliderFloat("Translucency Distortion", &m.translucencyDistortion, 0.0f, 1.0f);
                    ImGui::SliderFloat("Shadow Depth Scale", &m.translucencyShadowDepth, 0.0f, 1.0f);
                    if (ImGui::IsItemHovered()) {
                        ImGui::SetTooltip("How strongly shadow-map depth attenuates transmitted light.");
                    }
                }
                }

                if (showMaterialGroup(
                        "Shadows",
                        "shadows shadow sharpness pssm depth offset bias")) {
                ImGui::Separator();
                ImGui::Text("Shadows");
                ImGui::SliderFloat("Shadow Sharpness (Global)",
                                   &m_pssmShadowSharpness,
                                   0.0f, 1.0f, "%.2f");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Global PSSM sharpness: 1.0 = razor-sharp single-tap\n"
                                      "shadows, 0.0 = soft PCF. Affects every cast shadow.");
                }
                ImGui::SliderFloat("Shadow Depth Offset", &m.shadowDepthOffset, -0.25f, 0.25f, "%.3f m");
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Offsets the caster along its normal in the shadow-depth pass.\n"
                                      "This changes self-shadow bias, not edge sharpness.");
                }
                }

                if (showMaterialGroup(
                        "Texture Maps",
                        "texture map maps albedo normal metallic roughness emissive "
                        "occlusion opacity specular")) {
                ImGui::Separator();
                ImGui::Text("Texture Maps (drag from Content Browser):");

                if (showMaterialField("Albedo Map", "texture map color"))
                DrawTextureSlot("Albedo Map",           m.albedoMap,           true,
                                &m.albedoSrgb, &materialChanged);
                if (showMaterialField("Normal Map", "texture map normal bump"))
                DrawTextureSlot("Normal Map",           m.normalMap,           false,
                                &m.normalSrgb, &materialChanged);
                if (showMaterialField("Metallic/Roughness/Occlusion",
                                      "texture map orm mr metallic roughness occlusion ao")) {
                    auto* previousOrm = m.metallicRoughnessMap.get();
                    DrawTextureSlot("Metallic/Roughness/Occlusion (ORM)",
                                    m.metallicRoughnessMap, false,
                                    &m.metallicRoughnessSrgb, &materialChanged);
                    if (m.metallicRoughnessMap.get() != previousOrm) {
                        // ORM layout: R = occlusion, G = roughness,
                        // B = metallic. Keep the dedicated AO slot in sync
                        // unless the artist had assigned a separate map.
                        if (m.metallicRoughnessMap) {
                            m.occlusionMap = m.metallicRoughnessMap;
                        } else if (m.occlusionMap.get() == previousOrm) {
                            m.occlusionMap.reset();
                        }
                    }
                }
                if (showMaterialField("Metallic Map", "texture map metal"))
                DrawTextureSlot("Metallic Map",         m.metallicMap,         false,
                                &m.metallicSrgb, &materialChanged);
                if (showMaterialField("Roughness Map", "texture map rough"))
                DrawTextureSlot("Roughness Map",        m.roughnessMap,        false,
                                &m.roughnessSrgb, &materialChanged);
                if (showMaterialField("Emissive Map", "texture map emission"))
                DrawTextureSlot("Emissive Map",         m.emissiveMap,         true,
                                &m.emissiveSrgb, &materialChanged);
                if (showMaterialField("Occlusion Map", "texture map ao"))
                DrawTextureSlot("Occlusion Map",        m.occlusionMap,        false,
                                &m.occlusionSrgb, &materialChanged);
                if (showMaterialField("Opacity Map", "texture map alpha"))
                DrawTextureSlot("Opacity Map",          m.opacityMap,          false,
                                &m.opacitySrgb, &materialChanged);
                if (showMaterialField("Specular Map", "texture map specular"))
                DrawTextureSlot("Specular Map",         m.specularMap,         false,
                                &m.specularSrgb, &materialChanged);
                }
            }

            // Compare the complete editable material state. This catches
            // sliders, checkboxes and texture drops even when no ImGui item
            // remains active on the release frame.
            if (materialStateHash(m) != materialStateBefore) {
                m.sceneOverride = true;
                PushHistory();
            } else if (ImGui::IsAnyItemActive()) {
                m.sceneOverride = true;
            }
            ImGui::PopID();
        };

        // 1. Draw material for the selected entity itself (if any). A
        // collision capsule can own a character subtree; in that case its
        // child material slots are still valid Details content.
        if ((!selectedIsCollisionShape || selectionHasMaterials) &&
            (selected[0].HasComponent<Galeshi::MaterialComponent>() ||
             selected[0].HasComponent<Galeshi::MeshComponent>())) {
            if (auto* matc = ensureMaterialSlot(selected[0])) {
                std::string label =
                    (matc->material && !matc->material->name.empty())
                        ? matc->material->name
                        : (selected[0].HasComponent<Galeshi::NameComponent>()
                               ? selected[0]
                                     .GetComponent<Galeshi::NameComponent>()
                                     .name
                               : std::string("Material"));
                DrawMaterialUI(label.c_str(), *matc,
                               (uint32_t)selected[0].Handle());
            }
        }

        // 2. Draw materials for all descendants (skeletal model wrappers can
        // sit below a capsule, and their sub-meshes below another model root).
        if (!selectedIsCollisionShape || selectionHasMaterials) {
            for (auto e : m_scene.AllEntities()) {
                if (e.Handle() == selectedHandle) continue;
                entt::entity cursor = e.Handle();
                bool belongsToSelection = false;
                for (int depth = 0; depth < 128; ++depth) {
                    if (!reg.all_of<Galeshi::ParentComponent>(cursor)) break;
                    cursor = reg.get<Galeshi::ParentComponent>(cursor).parent;
                    if (!reg.valid(cursor)) break;
                    if (cursor == selectedHandle) {
                        belongsToSelection = true;
                        break;
                    }
                }
                if (!belongsToSelection ||
                    (!e.HasComponent<Galeshi::MaterialComponent>() &&
                     !e.HasComponent<Galeshi::MeshComponent>())) {
                    continue;
                }
                auto* matc = ensureMaterialSlot(e);
                if (!matc) continue;
                std::string label = "Material Slot [" +
                    std::to_string((uint32_t)e.Handle()) + "]";
                if (matc->material && !matc->material->name.empty()) {
                    label = matc->material->name;
                } else if (e.HasComponent<Galeshi::NameComponent>()) {
                    label = e.GetComponent<Galeshi::NameComponent>().name;
                }
                DrawMaterialUI(label.c_str(), *matc, (uint32_t)e.Handle());
            }
        }

        if (selected[0].HasComponent<Galeshi::NavAgentComponent>() && beginDetailsSection(
                "AI Agent", ImGuiTreeNodeFlags_DefaultOpen,
                "ai agent navigation nav speed path waypoints target")) {
            auto& agent = selected[0].GetComponent<Galeshi::NavAgentComponent>();
            ImGui::DragFloat("Speed", &agent.speed, 0.1f, 0.1f, 20.0f);
            ImGui::Text("Path Waypoints: %zu", agent.path.size());
            ImGui::Text("Target: (%.1f, %.1f, %.1f)", agent.targetPosition.x, agent.targetPosition.y, agent.targetPosition.z);
        }

        if (selected[0].HasComponent<Galeshi::AIControllerComponent>() && beginDetailsSection(
                "AI Controller", ImGuiTreeNodeFlags_DefaultOpen,
                "ai controller script path lua behavior")) {
            auto& ctrl = selected[0].GetComponent<Galeshi::AIControllerComponent>();
            char buf[256]; std::snprintf(buf, sizeof(buf), "%s", ctrl.scriptPath.c_str());
            if (ImGui::InputText("Script Path", buf, sizeof(buf))) ctrl.scriptPath = buf;
            
            // Drag & Drop support for scripts
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                    const char* path = static_cast<const char*>(payload->Data);
                    std::string p = path;
                    if (p.find(".lua") != std::string::npos) {
                        ctrl.scriptPath = p;
                    }
                }
                ImGui::EndDragDropTarget();
            }
        }

        // The AI & Gameplay section is only relevant for AI agents (both the
        // Behavior Tree and Lua Patrol templates add NavAgentComponent).
        const bool isAiAgent =
            selected[0].HasComponent<Galeshi::NavAgentComponent>() ||
            selected[0].HasComponent<Galeshi::AIControllerComponent>();

        if (isAiAgent && beginDetailsSection(
                "AI & Gameplay", 0,
                "ai gameplay perception behavior tree player tag add")) {
            if (!selected[0].HasComponent<Galeshi::PerceptionComponent>()) {
                if (ImGui::Button("+ Add Perception")) {
                    PushHistory();
                    selected[0].AddComponent<Galeshi::PerceptionComponent>();
                }
            }
            if (!selected[0].HasComponent<Galeshi::BehaviorTreeComponent>()) {
                if (ImGui::Button("+ Add Behavior Tree")) {
                    PushHistory();
                    auto& tree = selected[0].AddComponent<Galeshi::BehaviorTreeComponent>();
                    tree = Galeshi::BehaviorTreeSystem::MakeDefaultBehaviorTree();
                    if (!selected[0].HasComponent<Galeshi::PerceptionComponent>()) {
                        selected[0].AddComponent<Galeshi::PerceptionComponent>();
                    }
                    if (!selected[0].HasComponent<Galeshi::NavAgentComponent>()) {
                        selected[0].AddComponent<Galeshi::NavAgentComponent>();
                    }
                }
            }
            if (!selected[0].HasComponent<Galeshi::PlayerTagComponent>()) {
                if (ImGui::Button("+ Add Player Tag")) {
                    PushHistory();
                    selected[0].AddComponent<Galeshi::PlayerTagComponent>();
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::PerceptionComponent>() &&
            beginDetailsSection(
                "Perception", ImGuiTreeNodeFlags_DefaultOpen,
                "perception sight field of view hearing eye height detection "
                "interval memory line of sight player target")) {
            auto& perception = selected[0].GetComponent<Galeshi::PerceptionComponent>();
            ImGui::SliderFloat("Sight Radius", &perception.sightRadius, 0.0f, 200.0f);
            ImGui::SliderFloat("Field of View", &perception.fieldOfViewDegrees, 1.0f, 360.0f);
            ImGui::SliderFloat("Hearing Radius", &perception.hearingRadius, 0.0f, 100.0f);
            ImGui::SliderFloat("Eye Height", &perception.eyeHeight, 0.0f, 4.0f);
            ImGui::SliderFloat("Detection Interval", &perception.detectionInterval, 0.02f, 2.0f);
            ImGui::SliderFloat("Memory Duration", &perception.memoryDuration, 0.0f, 30.0f);
            ImGui::Checkbox("Require Line of Sight", &perception.requireLineOfSight);
            ImGui::SameLine();
            ImGui::Checkbox("Player Only", &perception.detectPlayerOnly);
            char targetName[128];
            std::snprintf(targetName, sizeof(targetName), "%s", perception.targetName.c_str());
            if (ImGui::InputText("Target Name", targetName, sizeof(targetName))) {
                perception.targetName = targetName;
            }
            if (ImGui::Button("Reset Perception")) {
                Galeshi::BehaviorTreeSystem::ResetPerception(perception);
            }
            ImGui::SameLine();
            ImGui::TextColored(perception.canSeeTarget ? ImVec4(0.4f, 1.0f, 0.4f, 1.0f)
                                                       : ImVec4(0.7f, 0.7f, 0.7f, 1.0f),
                               perception.canSeeTarget ? "Target visible" : "No target");
            ImGui::Text("Last seen: %.2fs ago", perception.timeSinceSeen);
        }

        if (selected[0].HasComponent<Galeshi::BehaviorTreeComponent>() &&
            beginDetailsSection(
                "Behavior Tree", ImGuiTreeNodeFlags_DefaultOpen,
                "behavior tree node selector sequence condition action tick "
                "status reset load default")) {
            auto& tree = selected[0].GetComponent<Galeshi::BehaviorTreeComponent>();
            ImGui::Checkbox("Enabled", &tree.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(100.0f);
            ImGui::DragFloat("Tick Interval", &tree.tickInterval, 0.01f, 0.0f, 2.0f);
            ImGui::Text("Status: %s (node %d)", tree.statusText.c_str(), tree.runningNode);
            if (ImGui::Button("Reset Runtime")) {
                Galeshi::BehaviorTreeSystem::ResetRuntime(tree);
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Default Tree")) {
                PushHistory();
                tree = Galeshi::BehaviorTreeSystem::MakeDefaultBehaviorTree();
                if (!selected[0].HasComponent<Galeshi::PerceptionComponent>()) {
                    selected[0].AddComponent<Galeshi::PerceptionComponent>();
                }
                if (!selected[0].HasComponent<Galeshi::NavAgentComponent>()) {
                    selected[0].AddComponent<Galeshi::NavAgentComponent>();
                }
            }

            const char* nodeTypeNames[] = {"Selector", "Sequence", "Condition", "Action"};
            const char* conditionNames[] = {
                "Always True", "Has Target", "Target Visible", "Target In Range",
                "Target Lost", "Parameter True"
            };
            const char* actionNames[] = {
                "Wait", "Set Target To Player", "Move To Target", "Patrol", "Attack",
                "Look At Target", "Set Animation State", "Clear Target", "Set Nav Speed"
            };

            std::function<void(int, int)> drawNode = [&](int index, int depth) {
                if (depth > 32 || index < 0 ||
                    index >= static_cast<int>(tree.nodes.size())) return;
                auto& node = tree.nodes[static_cast<size_t>(index)];
                ImGui::PushID(index);
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                           ImGuiTreeNodeFlags_DefaultOpen;
                if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
                char label[256];
                std::snprintf(label, sizeof(label), "[%d] %s", index, node.name.c_str());
                const bool open = ImGui::TreeNodeEx("##node", flags, "%s", label);
                ImGui::SameLine();
                ImGui::TextDisabled("(%s)", nodeTypeNames[static_cast<int>(node.type)]);

                char nodeName[128];
                std::snprintf(nodeName, sizeof(nodeName), "%s", node.name.c_str());
                if (ImGui::InputText("Name", nodeName, sizeof(nodeName))) {
                    node.name = nodeName;
                }
                int type = static_cast<int>(node.type);
                if (ImGui::Combo("Type", &type, nodeTypeNames, IM_ARRAYSIZE(nodeTypeNames))) {
                    node.type = static_cast<Galeshi::BehaviorNodeType>(type);
                }

                if (node.type == Galeshi::BehaviorNodeType::Condition) {
                    int condition = static_cast<int>(node.condition);
                    if (ImGui::Combo("Condition", &condition, conditionNames,
                                     IM_ARRAYSIZE(conditionNames))) {
                        node.condition = static_cast<Galeshi::BehaviorCondition>(condition);
                    }
                    if (node.condition == Galeshi::BehaviorCondition::TargetInRange) {
                        ImGui::DragFloat("Range", &node.range, 0.1f, 0.0f, 100.0f);
                    }
                    if (node.condition == Galeshi::BehaviorCondition::ParameterTrue) {
                        char parameter[128];
                        std::snprintf(parameter, sizeof(parameter), "%s", node.parameter.c_str());
                        if (ImGui::InputText("Parameter", parameter, sizeof(parameter))) {
                            node.parameter = parameter;
                        }
                    }
                } else if (node.type == Galeshi::BehaviorNodeType::Action) {
                    int action = static_cast<int>(node.action);
                    if (ImGui::Combo("Action", &action, actionNames,
                                     IM_ARRAYSIZE(actionNames))) {
                        node.action = static_cast<Galeshi::BehaviorAction>(action);
                    }
                    switch (node.action) {
                        case Galeshi::BehaviorAction::Wait:
                            ImGui::DragFloat("Duration", &node.duration, 0.05f, 0.0f, 60.0f);
                            break;
                        case Galeshi::BehaviorAction::MoveToTarget:
                            ImGui::DragFloat("Arrive Range", &node.range, 0.05f, 0.0f, 20.0f);
                            break;
                        case Galeshi::BehaviorAction::Patrol:
                            ImGui::DragFloat("Patrol Radius", &node.radius, 0.25f, 0.5f, 200.0f);
                            ImGui::DragFloat("Wait Time", &node.duration, 0.05f, 0.0f, 60.0f);
                            break;
                        case Galeshi::BehaviorAction::Attack:
                            ImGui::DragFloat("Attack Range", &node.range, 0.05f, 0.0f, 20.0f);
                            ImGui::DragFloat("Cooldown", &node.duration, 0.05f, 0.1f, 10.0f);
                            break;
                        case Galeshi::BehaviorAction::SetAnimationState: {
                            char state[128];
                            std::snprintf(state, sizeof(state), "%s", node.value.c_str());
                            if (ImGui::InputText("State", state, sizeof(state))) node.value = state;
                            break;
                        }
                        case Galeshi::BehaviorAction::SetNavSpeed: {
                            char speed[128];
                            std::snprintf(speed, sizeof(speed), "%s", node.value.c_str());
                            if (ImGui::InputText("Speed", speed, sizeof(speed))) node.value = speed;
                            break;
                        }
                        default:
                            break;
                    }
                } else {
                    ImGui::TextDisabled("Children: %zu", node.children.size());
                }

                if (open) {
                    std::vector<int> children = node.children;
                    for (int child : children) drawNode(child, depth + 1);
                    ImGui::TreePop();
                }
                ImGui::PopID();
            };
            if (!tree.nodes.empty()) drawNode(tree.root, 0);
        }

        if (selected[0].HasComponent<Galeshi::PlayerTagComponent>()) {
            ImGui::TextColored(ImVec4(0.5f, 0.8f, 1.0f, 1.0f), "Player Tag");
            ImGui::SameLine();
            if (ImGui::SmallButton("Remove##playerTag")) {
                selected[0].RemoveComponent<Galeshi::PlayerTagComponent>();
            }
        }

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() && beginDetailsSection(
                "Skeletal Animation", ImGuiTreeNodeFlags_DefaultOpen,
                "skeletal animation playing speed source active clip time "
                "bone skeleton")) {
            auto& skel = selected[0].GetComponent<Galeshi::SkeletalComponent>();
            ImGui::Checkbox("Playing", &skel.isAnimated);
            ImGui::DragFloat("Speed", &skel.animationSpeed, 0.1f, 0.0f, 10.0f);
            ImGui::TextDisabled("Source: %s", skel.sourcePath.empty() ? "(none)" : skel.sourcePath.c_str());
            ImGui::Text("Active Clip: %d  Time: %.2fs", skel.activeAnimation, skel.currentTime);
        }

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() ||
            selected[0].HasComponent<Galeshi::AnimationStateMachineComponent>()) {
            if (beginDetailsSection(
                    "Animation State Machine", ImGuiTreeNodeFlags_DefaultOpen,
                    "animation state machine states transitions parameters "
                    "clip loop speed blend playback animation scale")) {
                DrawAnimationStateMachineContents(selected[0]);
            }
        }

        if (selected[0].HasComponent<Galeshi::DirectionalLightComponent>() && beginDetailsSection(
                "Light", ImGuiTreeNodeFlags_DefaultOpen,
                "directional light sun direction color intensity shadow")) {
            auto& dl = selected[0].GetComponent<Galeshi::DirectionalLightComponent>();
            ImGui::DragFloat3("Direction", glm::value_ptr(dl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", glm::value_ptr(dl.color));
            ImGui::SliderFloat("Intensity", &dl.intensity, 0.0f, 10.0f);
        }

        if (selected[0].HasComponent<Galeshi::PointLightComponent>() && beginDetailsSection(
                "Point Light", ImGuiTreeNodeFlags_DefaultOpen,
                "point light color intensity radius shadow")) {
            auto& pl = m_scene.Registry().get<Galeshi::PointLightComponent>(selected[0].Handle());
            ImGui::ColorEdit3("Point Color", glm::value_ptr(pl.color));
            {
                float oldInt = pl.intensity;
                if (ImGui::DragFloat("Point Intensity", &pl.intensity, 0.1f, 0.0f, 100.0f)) {
                }
            }
            ImGui::DragFloat("Point Radius", &pl.radius, 0.1f, 0.1f, 100.0f);
        }

        if (selected[0].HasComponent<Galeshi::SpotLightComponent>() && beginDetailsSection(
                "Spot Light", ImGuiTreeNodeFlags_DefaultOpen,
                "spot light direction color intensity radius inner outer cone")) {
            auto& sl = m_scene.Registry().get<Galeshi::SpotLightComponent>(selected[0].Handle());
            ImGui::DragFloat3("Spot Direction", glm::value_ptr(sl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Spot Color", glm::value_ptr(sl.color));
            ImGui::SliderFloat("Spot Intensity", &sl.intensity, 0.0f, 20.0f);
            ImGui::DragFloat("Spot Radius", &sl.radius, 0.1f, 0.1f, 100.0f);
            ImGui::SliderFloat("Inner Cone", &sl.innerConeAngle, 0.0f, 90.0f);
            ImGui::SliderFloat("Outer Cone", &sl.outerConeAngle, 0.0f, 90.0f);
        }

        if (selected[0].HasComponent<Galeshi::ParticleEmitterComponent>() &&
            beginDetailsSection(
                "Particle Emitter", ImGuiTreeNodeFlags_DefaultOpen,
                "particle emitter enabled emit rate lifetime start end size "
                "color velocity gravity drag max particles texture")) {
            auto& pe = m_scene.Registry().get<Galeshi::ParticleEmitterComponent>(selected[0].Handle());
            ImGui::Checkbox("Enabled", &pe.enabled);
            ImGui::DragFloat("Emit Rate", &pe.emitRate, 0.5f, 0.0f, 500.0f);
            ImGui::DragFloat("Lifetime", &pe.lifetime, 0.01f, 0.05f, 30.0f);
            ImGui::DragFloat("Lifetime Variance", &pe.lifetimeVariance, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("Start Size", &pe.startSize, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat("End Size", &pe.endSize, 0.01f, 0.0f, 10.0f);
            ImGui::ColorEdit4("Start Color", glm::value_ptr(pe.startColor));
            ImGui::ColorEdit4("End Color", glm::value_ptr(pe.endColor));
            ImGui::DragFloat3("Velocity", glm::value_ptr(pe.velocity), 0.05f, -50.0f, 50.0f);
            ImGui::DragFloat("Velocity Variance", &pe.velocityVariance, 0.05f, 0.0f, 50.0f);
            ImGui::DragFloat("Spawn Radius", &pe.spawnRadius, 0.01f, 0.0f, 10.0f);
            ImGui::DragFloat3("Spawn Box Size", glm::value_ptr(pe.spawnBoxSize), 0.05f, 0.0f, 100.0f);
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Half extents of a box-shaped spawn volume.\n"
                                  "Any value > 0 switches from the sphere to the box.\n"
                                  "Use it for rain/snow areas.");
            }
            ImGui::DragFloat3("Gravity", glm::value_ptr(pe.gravity), 0.05f, -50.0f, 50.0f);
            ImGui::DragFloat("Drag", &pe.drag, 0.01f, 0.0f, 10.0f);
            int maxParticles = static_cast<int>(pe.maxParticles);
            if (ImGui::DragInt("Max Particles", &maxParticles, 1.0f, 1, 20000)) {
                pe.maxParticles = static_cast<uint32_t>(std::max(maxParticles, 1));
            }

            ImGui::Separator();
            ImGui::Text("Particle Texture (drag from Content Browser):");
            DrawTextureSlot("Particle Texture", pe.texture, true);

            if (m_particles) {
                ImGui::TextDisabled("Live particles: %zu",
                                    m_particles->ParticleCount(selected[0].Handle()));
            }
        }

        if (selected[0].HasComponent<Galeshi::UICanvasComponent>()) {
            auto& uiCanvas = selected[0].GetComponent<Galeshi::UICanvasComponent>().canvas;
            if (m_uiCanvasEntity != selected[0].Handle()) {
                m_uiCanvasEntity = selected[0].Handle();
                m_uiWidgetIndex = -1;
            }
            DrawUICanvasProperties(uiCanvas);
            if (m_uiWidgetIndex >= 0 &&
                m_uiWidgetIndex < static_cast<int>(uiCanvas.widgets.size())) {
                DrawUIWidgetProperties(
                    uiCanvas, uiCanvas.widgets[static_cast<size_t>(m_uiWidgetIndex)]);
            }
        }

        if (searchActive && !detailsAnyMatch) {
            ImGui::TextDisabled(
                "No properties or materials match \"%s\".",
                m_detailsSearch.c_str());
        }

        ImGui::EndChild();
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Content Browser & Assets
    // -------------------------------------------------------------------
    void SetCodeEditorContent(const std::string& path,
                              const std::string& text,
                              bool cppMode) {
        m_currentCodeFile = path;
        m_codeEditorCppMode = cppMode;
        m_codeEditorTabRequest = 0;
        const bool shader = IsShaderAssetPath(path);
        m_codeEditor.SetLanguageDefinition(
            cppMode || shader ? TextEditor::LanguageDefinition::CPlusPlus()
                              : TextEditor::LanguageDefinition::Lua());
        m_codeEditor.SetPalette(TextEditor::GetDarkPalette());
        m_codeEditor.SetText(text);
        m_codeEditorInitialized = true;
        m_codeEditorSyntaxErrors.clear();
        m_codeEditorLastSyntaxSource.clear();
        m_codeEditorSyntaxDirty = !path.empty() && !shader;
        m_codeEditorLastEditTime = ImGui::GetTime();
        // The text input lives in a child window. Focusing only the parent
        // leaves the editor non-editable until it is clicked manually.
        m_focusCodeEditor = !path.empty();
    }

    void OpenCodeFile(const std::string& path) {
        std::string text;
        std::ifstream in(path);
        if (in.is_open()) {
            std::stringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
        SetCodeEditorContent(path, text, IsCppCodeAssetPath(path));
        if (IsShaderAssetPath(path)) {
            CompileShaderPreview(path);
        } else {
            m_shaderPreviewError.clear();
        }
        ImGui::SetWindowFocus("Code Editor");
    }

    void SyncCodeEditorStateToSerializer() {
        if (!m_serializer) return;
        if (m_currentCodeFile.empty()) {
            m_serializer->SetCodeEditorState({}, {}, false);
        } else {
            m_serializer->SetCodeEditorState(
                m_currentCodeFile, m_codeEditor.GetText(),
                m_codeEditorCppMode);
        }
    }

    void RestoreCodeEditorStateFromSerializer() {
        if (!m_serializer) return;
        if (m_serializer->CodeEditorPath().empty()) {
            SetCodeEditorContent({}, "-- Select a code file.", false);
            return;
        }
        SetCodeEditorContent(
            m_serializer->CodeEditorPath(),
            m_serializer->CodeEditorText(),
            m_serializer->CodeEditorCppMode());
        if (IsShaderAssetPath(m_serializer->CodeEditorPath())) {
            CompileShaderPreview(m_serializer->CodeEditorPath());
        }
    }

    static int DiagnosticLineNumber(const std::string& line) {
        for (size_t i = 0; i + 1 < line.size(); ++i) {
            if (line[i] != ':') continue;
            size_t cursor = i + 1;
            int value = 0;
            bool hasDigits = false;
            while (cursor < line.size() &&
                   std::isdigit(static_cast<unsigned char>(line[cursor]))) {
                value = value * 10 + (line[cursor] - '0');
                cursor++;
                hasDigits = true;
            }
            if (hasDigits && cursor < line.size() && line[cursor] == ':') {
                return value;
            }
        }
        return -1;
    }

    std::string FormatSyntaxDiagnostics(const std::string& diagnostics,
                                        bool cpp) {
        const std::vector<std::string> lines = m_codeEditor.GetTextLines();
        std::istringstream input(diagnostics);
        std::string line;
        std::string formatted;
        while (std::getline(input, line)) {
            const bool diagnosticLine =
                cpp ? (line.find("error:") != std::string::npos ||
                       line.find("fatal error:") != std::string::npos ||
                       line.find("warning:") != std::string::npos)
                    : true;
            if (diagnosticLine) {
                const int lineNumber = DiagnosticLineNumber(line);
                if (lineNumber > 0 &&
                    lineNumber <= static_cast<int>(lines.size())) {
                    formatted += "Line " + std::to_string(lineNumber) + ": " +
                                 lines[static_cast<size_t>(lineNumber - 1)] +
                                 "\n";
                }
            }
            formatted += line + "\n";
        }
        return formatted;
    }

    struct CodeSyntaxResult {
        bool ok = false;
        bool cpp = false;
        std::string source;
        std::string path;
        std::string diagnostics;
    };

    void StartCodeSyntaxCheck() {
        if (m_codeEditorSyntaxRunning || m_currentCodeFile.empty() ||
            IsShaderAssetPath(m_currentCodeFile)) {
            m_codeEditorSyntaxDirty = false;
            return;
        }
        const std::string source = m_codeEditor.GetText();
        if (source == m_codeEditorLastSyntaxSource &&
            m_currentCodeFile == m_codeEditorSyntaxPendingPath) {
            m_codeEditorSyntaxDirty = false;
            return;
        }
        m_codeEditorSyntaxPendingSource = source;
        m_codeEditorSyntaxPendingPath = m_currentCodeFile;
        const bool cpp = IsCppCodeAssetPath(m_currentCodeFile);
        m_codeEditorSyntaxRunning = true;
        m_codeEditorSyntaxDirty = false;
        Galeshi::ScriptEngine* scripts = &m_scripts;
        try {
            m_codeEditorSyntaxFuture = std::async(
                std::launch::async,
                [scripts, source, path = m_currentCodeFile, cpp]() {
                    CodeSyntaxResult result;
                    result.cpp = cpp;
                    result.source = source;
                    result.path = path;
                    result.ok = cpp
                        ? scripts->CheckCppSyntax(
                              source, path, result.diagnostics)
                        : scripts->CheckLuaSyntax(
                              source, path, result.diagnostics);
                    return result;
                });
        } catch (const std::exception& error) {
            m_codeEditorSyntaxRunning = false;
            m_codeEditorSyntaxErrors =
                std::string("Could not start syntax watcher: ") +
                error.what() + "\n";
        }
    }

    void PollCodeSyntaxCheck() {
        if (!m_codeEditorSyntaxRunning ||
            !m_codeEditorSyntaxFuture.valid()) {
            return;
        }
        if (m_codeEditorSyntaxFuture.wait_for(
                std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        CodeSyntaxResult result;
        try {
            result = m_codeEditorSyntaxFuture.get();
        } catch (const std::exception& error) {
            m_codeEditorSyntaxRunning = false;
            m_codeEditorSyntaxErrors =
                std::string("Syntax watcher failed: ") + error.what() + "\n";
            return;
        } catch (...) {
            m_codeEditorSyntaxRunning = false;
            m_codeEditorSyntaxErrors =
                "Syntax watcher failed with an unknown exception.\n";
            return;
        }
        m_codeEditorSyntaxRunning = false;
        if (result.path != m_currentCodeFile ||
            result.source != m_codeEditor.GetText()) {
            m_codeEditorSyntaxDirty = true;
            return;
        }
        m_codeEditorLastSyntaxSource = result.source;
        if (result.ok) {
            m_codeEditorSyntaxErrors.clear();
        } else if (result.diagnostics.empty()) {
            m_codeEditorSyntaxErrors =
                "Syntax check failed without diagnostics.\n";
        } else {
            m_codeEditorSyntaxErrors = FormatSyntaxDiagnostics(
                result.diagnostics, result.cpp);
        }
    }

    std::string CodeEditorErrorsText() const {
        if (IsShaderAssetPath(m_currentCodeFile) &&
            !m_shaderPreviewError.empty()) {
            return m_shaderPreviewError;
        }
        std::string errors = m_codeEditorSyntaxErrors;
        const std::string& buildErrors = m_scripts.GetCppBuildErrors();
        if (!buildErrors.empty() &&
            (m_currentCodeFile.empty() ||
             IsCppCodeAssetPath(m_currentCodeFile))) {
            if (!errors.empty()) {
                errors += "\n--- C++ build ---\n";
            }
            errors += buildErrors;
        }
        return errors;
    }

    static std::string ShellQuote(const std::string& value) {
        std::string quoted = "'";
        for (char c : value) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        quoted += "'";
        return quoted;
    }

    std::string FindShadercExecutable() const {
        std::error_code ec;
        const auto check = [&ec](const std::filesystem::path& path) {
            return !path.empty() && std::filesystem::is_regular_file(path, ec)
                       ? path.string()
                       : std::string{};
        };
        std::string result = check(
            std::filesystem::current_path() / "shaderc");
        if (!result.empty()) return result;
#if defined(__linux__)
        const auto executable =
            std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            result = check(executable.parent_path() / "shaderc");
            if (!result.empty()) return result;
        }
#endif
        result = check("/usr/bin/shaderc");
        if (!result.empty()) return result;
        return check("/usr/local/bin/shaderc");
    }

    std::filesystem::path ShaderIncludeDirectory() const {
        std::error_code ec;
        const auto valid = [&ec](const std::filesystem::path& path) {
            return std::filesystem::is_regular_file(
                       path / "bgfx_shader.sh", ec) &&
                   std::filesystem::is_regular_file(
                       path / "varying_fullscreen.def.sc", ec);
        };
        std::filesystem::path include =
            std::filesystem::current_path() / "assets/shaders/include";
        if (valid(include)) return include;
        for (const char* relative : {
                 "../_deps/bgfx-src/bgfx/src",
                 "../../_deps/bgfx-src/bgfx/src",
                 "../../../_deps/bgfx-src/bgfx/src",
             }) {
            include = std::filesystem::current_path() / relative;
            if (valid(include)) return include;
        }
        return {};
    }

    void DestroyShaderPreviewProgram() {
        if (bgfx::isValid(m_shaderPreviewProgram)) {
            bgfx::destroy(m_shaderPreviewProgram);
            m_shaderPreviewProgram = BGFX_INVALID_HANDLE;
        }
    }

    void ShutdownShaderPreview() {
        DestroyShaderPreviewProgram();
        if (bgfx::isValid(m_shaderPreviewFbo)) {
            bgfx::destroy(m_shaderPreviewFbo);
            m_shaderPreviewFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewTex)) {
            bgfx::destroy(m_shaderPreviewTex);
            m_shaderPreviewTex = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewVB)) {
            bgfx::destroy(m_shaderPreviewVB);
            m_shaderPreviewVB = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewIB)) {
            bgfx::destroy(m_shaderPreviewIB);
            m_shaderPreviewIB = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewSampler)) {
            bgfx::destroy(m_shaderPreviewSampler);
            m_shaderPreviewSampler = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewTime)) {
            bgfx::destroy(m_shaderPreviewTime);
            m_shaderPreviewTime = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewResolution)) {
            bgfx::destroy(m_shaderPreviewResolution);
            m_shaderPreviewResolution = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewFallbackTex)) {
            bgfx::destroy(m_shaderPreviewFallbackTex);
            m_shaderPreviewFallbackTex = BGFX_INVALID_HANDLE;
        }
    }

    void EnsureShaderPreviewResources() {
        if (!bgfx::isValid(m_shaderPreviewSampler)) {
            m_shaderPreviewSampler = bgfx::createUniform(
                "s_texColor", bgfx::UniformType::Sampler);
            m_shaderPreviewTime = bgfx::createUniform(
                "u_shaderTime", bgfx::UniformType::Vec4);
            m_shaderPreviewResolution = bgfx::createUniform(
                "u_shaderResolution", bgfx::UniformType::Vec4);
        }
        if (!bgfx::isValid(m_shaderPreviewFallbackTex)) {
            const uint32_t white = 0xffffffffu;
            m_shaderPreviewFallbackTex = bgfx::createTexture2D(
                1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                bgfx::copy(&white, sizeof(white)));
        }
        if (!bgfx::isValid(m_shaderPreviewTex)) {
            constexpr uint16_t size = 256;
            m_shaderPreviewTex = bgfx::createTexture2D(
                size, size, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                    BGFX_SAMPLER_V_CLAMP);
        }
        if (bgfx::isValid(m_shaderPreviewTex) &&
            !bgfx::isValid(m_shaderPreviewFbo)) {
            m_shaderPreviewFbo = bgfx::createFrameBuffer(
                1, &m_shaderPreviewTex, false);
        }
        if (!bgfx::isValid(m_shaderPreviewVB)) {
            struct PreviewVertex {
                float x, y, z;
                float u, v;
            };
            static const PreviewVertex quad[4] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
                { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
            };
            static const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
            bgfx::VertexLayout layout;
            layout.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
            m_shaderPreviewVB = bgfx::createVertexBuffer(
                bgfx::makeRef(quad, sizeof(quad)), layout);
            m_shaderPreviewIB = bgfx::createIndexBuffer(
                bgfx::makeRef(indices, sizeof(indices)));
        }
    }

    std::filesystem::path ResolveShaderSourcePath(
        const std::string& path) const {
        std::filesystem::path source(path);
        std::error_code ec;
        if (source.is_absolute() ||
            std::filesystem::is_regular_file(source, ec)) {
            return source;
        }
        const std::string project = Galeshi::ProjectManager::GetProjectPath();
        if (!project.empty()) {
            const std::filesystem::path projectPath(project);
            const std::filesystem::path candidates[] = {
                projectPath / source,
                projectPath / "assets" / source,
                projectPath / "assets/scripts" / source.filename(),
            };
            for (const auto& candidate : candidates) {
                ec.clear();
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    return candidate;
                }
            }
        }
        return source;
    }

    void CompileShaderPreview(const std::string& path,
                              const std::string& sourceOverride = {}) {
        DestroyShaderPreviewProgram();
        m_shaderPreviewPath = path;
        m_shaderPreviewError.clear();
        if (!IsShaderAssetPath(path)) {
            m_shaderPreviewError = "Not a shader file: " + path;
            return;
        }

        std::string source = sourceOverride;
        if (source.empty()) {
            const std::filesystem::path resolved =
                ResolveShaderSourcePath(path);
            std::ifstream in(resolved, std::ios::binary);
            if (!in.is_open()) {
                m_shaderPreviewError =
                    "Cannot open shader file: " + path;
                return;
            }
            std::stringstream buffer;
            buffer << in.rdbuf();
            source = buffer.str();
        }
        if (source.empty()) {
            m_shaderPreviewError = "Shader file is empty.";
            return;
        }

        const bool vertexShader = IsVertexShaderSource(source);
        const std::string shaderc = FindShadercExecutable();
        if (shaderc.empty()) {
            m_shaderPreviewError =
                "shaderc executable was not found. Build or install shaderc.";
            return;
        }
        const std::filesystem::path include = ShaderIncludeDirectory();
        if (include.empty()) {
            m_shaderPreviewError =
                "Shader include directory with bgfx_shader.sh was not found.";
            return;
        }

        std::error_code ec;
        const std::filesystem::path temp =
            std::filesystem::temp_directory_path(ec);
        const std::filesystem::path root =
            ec ? std::filesystem::current_path() : temp;
        const std::filesystem::path sourcePath =
            root / ("galeshi_shader_preview" +
                    std::filesystem::path(path).extension().string());
        const std::filesystem::path binaryPath =
            root / "galeshi_shader_preview.bin";
        const std::filesystem::path logPath =
            root / "galeshi_shader_preview.log";
        {
            std::ofstream out(sourcePath, std::ios::binary);
            if (!out.is_open()) {
                m_shaderPreviewError =
                    "Cannot write temporary shader source.";
                return;
            }
            out << source;
        }
        std::filesystem::remove(binaryPath, ec);
        ec.clear();
        std::filesystem::remove(logPath, ec);

#if defined(__linux__)
        const char* platform = "linux";
#elif defined(__APPLE__)
        const char* platform = "osx";
#else
        const char* platform = "windows";
#endif
        const std::string command =
            ShellQuote(shaderc) + " -f " + ShellQuote(sourcePath.string()) +
            " -o " + ShellQuote(binaryPath.string()) +
            " --type " + (vertexShader ? "vertex" : "fragment") +
            " --platform " + platform +
            " --profile spirv -i " + ShellQuote(include.string()) +
            " --varyingdef " +
            ShellQuote((include / "varying_fullscreen.def.sc").string()) +
            " > " + ShellQuote(logPath.string()) + " 2>&1";
        const int status = std::system(command.c_str());

        std::string diagnostics;
        {
            std::ifstream log(logPath);
            if (log.is_open()) {
                std::stringstream buffer;
                buffer << log.rdbuf();
                diagnostics = buffer.str();
            }
        }
        if (status != 0 ||
            !std::filesystem::is_regular_file(binaryPath, ec)) {
            m_shaderPreviewError = "Shader compilation failed.";
            if (!diagnostics.empty()) {
                m_shaderPreviewError += "\n" + diagnostics;
            }
            return;
        }

        m_shaderPreviewProgram = vertexShader
            ? Galeshi::ShaderManager::LoadProgram(
                  binaryPath.string(), "shaders/fs_passthrough.bin")
            : Galeshi::ShaderManager::LoadProgram(
                  "shaders/vs_fullscreen.bin", binaryPath.string());
        if (!bgfx::isValid(m_shaderPreviewProgram)) {
            m_shaderPreviewError =
                "Compiled shader could not be loaded by bgfx.";
            return;
        }

        EnsureShaderPreviewResources();
        if (!bgfx::isValid(m_shaderPreviewFbo)) {
            m_shaderPreviewError =
                "Could not create the shader preview framebuffer.";
            DestroyShaderPreviewProgram();
            return;
        }
        spdlog::info("[Shader] Compiled preview: {}", path);
    }

    void RenderShaderPreview() {
        if (!bgfx::isValid(m_shaderPreviewProgram) ||
            !bgfx::isValid(m_shaderPreviewFbo) ||
            !bgfx::isValid(m_shaderPreviewSampler)) {
            return;
        }
        constexpr uint16_t size = 256;
        bgfx::setViewFrameBuffer(m_shaderPreviewView, m_shaderPreviewFbo);
        bgfx::setViewRect(m_shaderPreviewView, 0, 0, size, size);
        bgfx::setViewClear(m_shaderPreviewView, BGFX_CLEAR_COLOR,
                           0x0b1220ff);
        const bgfx::TextureHandle input =
            m_logoTexture && m_logoTexture->Valid()
                ? m_logoTexture->Handle()
                : m_shaderPreviewFallbackTex;
        bgfx::setTexture(0, m_shaderPreviewSampler, input);
        const float time[4] = {
            static_cast<float>(ImGui::GetTime()), 0.0f, 0.0f, 0.0f};
        const float resolution[4] = {
            static_cast<float>(size), static_cast<float>(size),
            1.0f / static_cast<float>(size),
            1.0f / static_cast<float>(size)};
        bgfx::setUniform(m_shaderPreviewTime, time);
        bgfx::setUniform(m_shaderPreviewResolution, resolution);
        bgfx::setVertexBuffer(0, m_shaderPreviewVB);
        bgfx::setIndexBuffer(m_shaderPreviewIB);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(m_shaderPreviewView, m_shaderPreviewProgram);
    }

    void DrawShaderPreviewPane() {
        if (!m_shaderPreviewPath.empty()) {
            ImGui::TextDisabled("%s",
                                std::filesystem::path(m_shaderPreviewPath)
                                    .filename()
                                    .string()
                                    .c_str());
        }
        if (!m_shaderPreviewError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.45f, 0.42f, 1.0f));
            ImGui::TextWrapped("%s", m_shaderPreviewError.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        if (!bgfx::isValid(m_shaderPreviewProgram) ||
            !bgfx::isValid(m_shaderPreviewTex)) {
            ImGui::TextDisabled(
                "Compile a GLSL/HLSL fragment shader to see its effect.");
            return;
        }
        RenderShaderPreview();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float size =
            std::max(32.0f, std::min(available.x, available.y));
        ImGui::Image(static_cast<ImTextureID>(m_shaderPreviewTex.idx),
                     ImVec2(size, size), ImVec2(0.0f, 0.0f),
                     ImVec2(1.0f, 1.0f));
        ImGui::TextDisabled("256 x 256 live preview");
    }

    void DrawCodeEditor() {
        ImGui::Begin("Code Editor");
        const std::string chosenCodeFile = m_codeFilePicker.Draw();
        if (!chosenCodeFile.empty()) OpenCodeFile(chosenCodeFile);
        if (!m_codeEditorInitialized) {
            m_codeEditor.SetLanguageDefinition(
                m_codeEditorCppMode
                    ? TextEditor::LanguageDefinition::CPlusPlus()
                    : TextEditor::LanguageDefinition::Lua());
            m_codeEditor.SetPalette(TextEditor::GetDarkPalette());
            m_codeEditor.SetText(
                "-- Select a .lua, .cpp, .glsl or .hlsl file from Content Browser");
            m_codeEditorInitialized = true;
        }

        auto drawDiagnostics = [](const char* childId,
                                  const std::string& text,
                                  bool errorText) {
            if (text.empty()) {
                ImGui::TextDisabled(errorText ? "No C++ errors."
                                              : "No C++ build output.");
                return;
            }
            if (errorText) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.52f, 0.48f, 1.0f));
            }
            ImGui::BeginChild(childId, ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(text.c_str());
            ImGui::EndChild();
            if (errorText) ImGui::PopStyleColor();
        };

        if (ImGui::BeginTabBar("CodeEditorTabs",
                               ImGuiTabBarFlags_Reorderable)) {
            const auto selectFlag = [this](int tab) {
                return m_codeEditorTabRequest == tab
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem("Code", nullptr, selectFlag(0))) {
                m_codeEditorTabRequest = -1;
                if (ImGui::IsItemClicked()) m_focusCodeEditor = true;
                if (ImGui::Button("Open...")) {
                    std::filesystem::path start =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (start.empty()) start = std::filesystem::current_path();
                    start /= "scripts";
                    if (!std::filesystem::is_directory(start)) {
                        start = std::filesystem::path(
                            Galeshi::ProjectManager::GetProjectPath()) /
                            "assets/scripts";
                    }
                    if (!std::filesystem::is_directory(start)) {
                        start = std::filesystem::current_path() /
                                "assets/scripts";
                    }
                    m_codeFilePicker.Open(
                        start.string(), "Open Code File",
                        {".lua", ".cpp", ".cc", ".cxx", ".hpp", ".h",
                         ".glsl", ".hlsl"});
                }
                ImGui::SameLine();
                if (m_currentCodeFile.empty()) {
                    ImGui::TextDisabled(
                        "No file opened. Double-click a .lua, .cpp, .glsl or "
                        ".hlsl file in Content Browser, or use Open...");
                } else {
                    const bool cppFile =
                        IsCppCodeAssetPath(m_currentCodeFile);
                    const bool shaderFile =
                        IsShaderAssetPath(m_currentCodeFile);
                    if (shaderFile) {
                        ImGui::TextDisabled("Shader");
                    } else if (ImGui::Checkbox("C++ mode",
                                               &m_codeEditorCppMode)) {
                        m_codeEditor.SetLanguageDefinition(
                            m_codeEditorCppMode
                                ? TextEditor::LanguageDefinition::CPlusPlus()
                                : TextEditor::LanguageDefinition::Lua());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save")) {
                        std::ofstream out(m_currentCodeFile);
                        if (out.is_open()) {
                            out << m_codeEditor.GetText();
                            out.close();
                            spdlog::info(
                                "[Editor] Saved code file: {}",
                                std::filesystem::absolute(
                                    m_currentCodeFile).string());
                            bool saved = true;
                            if (cppFile && !m_scripts.ReloadCppScripts(true)) {
                                m_codeEditorTabRequest = 2;
                                saved = false;
                            }
                            if (shaderFile) {
                                CompileShaderPreview(
                                    m_currentCodeFile,
                                    m_codeEditor.GetText());
                                if (!m_shaderPreviewError.empty()) {
                                    m_codeEditorTabRequest = 2;
                                }
                            }
                            if (saved) m_focusCodeEditor = true;
                        } else {
                            spdlog::error(
                                "[Editor] FAILED to open file for saving: {}",
                                m_currentCodeFile);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reload")) {
                        if (shaderFile) {
                            OpenCodeFile(m_currentCodeFile);
                        } else if (cppFile) {
                            if (!m_scripts.ReloadCppScripts(true)) {
                                m_codeEditorTabRequest = 2;
                            } else {
                                m_focusCodeEditor = true;
                            }
                        } else if (m_scripts.HotReload(m_currentCodeFile)) {
                            spdlog::info("[Editor] Reloaded {}",
                                         m_currentCodeFile);
                            m_focusCodeEditor = true;
                        }
                    }
                    if (cppFile) {
                        ImGui::SameLine();
                        if (ImGui::Button("Compile C++")) {
                            if (!m_scripts.ReloadCppScripts(true)) {
                                m_codeEditorTabRequest = 2;
                            } else {
                                m_focusCodeEditor = true;
                            }
                        }
                    } else if (shaderFile) {
                        ImGui::SameLine();
                        if (ImGui::Button("Compile Shader")) {
                            CompileShaderPreview(
                                m_currentCodeFile, m_codeEditor.GetText());
                            if (!m_shaderPreviewError.empty()) {
                                m_codeEditorTabRequest = 2;
                            } else {
                                m_focusCodeEditor = true;
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", m_currentCodeFile.c_str());
                    ImGui::Separator();

                    const auto drawEditorSurface = [&]() {
                        if (m_focusCodeEditor) {
                            m_codeEditor.RequestEditorFocus();
                            m_focusCodeEditor = false;
                        }
                        m_codeEditor.Render("CodeEditor");

                        if (ImGui::IsWindowHovered(
                                ImGuiHoveredFlags_ChildWindows) &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            ImGui::OpenPopup("Code Editor Context");
                        }
                        if (ImGui::BeginPopup("Code Editor Context")) {
                            const bool readOnly = m_codeEditor.IsReadOnly();
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                m_codeEditor.Copy();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem(
                                    "Cut", "Ctrl+X", false,
                                    !readOnly && m_codeEditor.HasSelection())) {
                                m_codeEditor.Cut();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem("Paste", "Ctrl+V", false,
                                                !readOnly)) {
                                m_codeEditor.Paste();
                                m_focusCodeEditor = true;
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                                m_codeEditor.SelectAll();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                                m_codeEditor.CanUndo())) {
                                m_codeEditor.Undo();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
                                                m_codeEditor.CanRedo())) {
                                m_codeEditor.Redo();
                                m_focusCodeEditor = true;
                            }
                            ImGui::EndPopup();
                        }

                        if (m_codeEditor.IsTextChanged()) {
                            m_codeEditorSyntaxDirty = true;
                            m_codeEditorLastEditTime = ImGui::GetTime();
                        }
                        PollCodeSyntaxCheck();
                        if (m_codeEditorSyntaxDirty &&
                            !m_codeEditorSyntaxRunning &&
                            ImGui::GetTime() - m_codeEditorLastEditTime >=
                                0.6) {
                            StartCodeSyntaxCheck();
                        }
                    };

                    if (shaderFile) {
                        const ImVec2 available =
                            ImGui::GetContentRegionAvail();
                        const float previewWidth = std::clamp(
                            available.x * 0.34f, 220.0f, 460.0f);
                        ImGui::BeginChild(
                            "CodeEditorSurface",
                            ImVec2(std::max(
                                       120.0f,
                                       available.x - previewWidth - 8.0f),
                                   0.0f),
                            false);
                        drawEditorSurface();
                        ImGui::EndChild();
                        ImGui::SameLine();
                        ImGui::BeginChild("ShaderPreview", ImVec2(0, 0), true);
                        DrawShaderPreviewPane();
                        ImGui::EndChild();
                    } else {
                        drawEditorSurface();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Output", nullptr, selectFlag(1))) {
                m_codeEditorTabRequest = -1;
                drawDiagnostics("CodeEditorOutput",
                                m_scripts.GetCppBuildOutput(), false);
                ImGui::EndTabItem();
            }
            const std::string errors = CodeEditorErrorsText();
            const bool hasErrors = !errors.empty();
            if (hasErrors) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.45f, 0.42f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Tab,
                                      ImVec4(0.35f, 0.06f, 0.06f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabHovered,
                                      ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabSelected,
                                      ImVec4(0.60f, 0.12f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabDimmed,
                                      ImVec4(0.28f, 0.05f, 0.05f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,
                                      ImVec4(0.45f, 0.08f, 0.08f, 1.0f));
            }
            const bool errorsTabOpen =
                ImGui::BeginTabItem("Errors", nullptr, selectFlag(2));
            if (hasErrors) ImGui::PopStyleColor(6);
            if (errorsTabOpen) {
                m_codeEditorTabRequest = -1;
                drawDiagnostics("CodeEditorErrors", errors, true);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

    void DrawAudioMixer() {
        if (!m_showAudioMixer) return;
        if (ImGui::Begin("Audio Mixer", &m_showAudioMixer)) {
            auto& buses = m_scene.AudioBuses();
            ImGui::TextDisabled(
                "Audio Sources are routed through these buses. "
                "Values are saved with the scene.");
            ImGui::Separator();

            for (int i = 0; i < Galeshi::kAudioBusCount; ++i) {
                const auto bus = static_cast<Galeshi::AudioBus>(i);
                ImGui::PushID(i);
                ImGui::TextUnformatted(Galeshi::AudioBusName(bus));
                ImGui::SameLine(110.0f);
                ImGui::SetNextItemWidth(200.0f);
                float volume = buses[static_cast<size_t>(i)].volume;
                if (ImGui::SliderFloat("##Volume", &volume, 0.0f, 1.0f,
                                       "%.2f")) {
                    buses[static_cast<size_t>(i)].volume = volume;
                }
                ImGui::SameLine();
                bool muted = buses[static_cast<size_t>(i)].muted;
                if (ImGui::Checkbox("Mute", &muted)) {
                    buses[static_cast<size_t>(i)].muted = muted;
                }
                if (i == static_cast<int>(Galeshi::AudioBus::Master)) {
                    ImGui::SameLine();
                    ImGui::TextDisabled("(final output)");
                }
                ImGui::PopID();
            }

            ImGui::Separator();
            if (ImGui::Button("Reset Mixer")) {
                for (auto& bus : buses) {
                    bus.volume = 1.0f;
                    bus.muted = false;
                }
            }
        }
        ImGui::End();
    }

    void DrawOutputLog() {
        if (!m_showOutputLog) return;

        // Keep the new tab next to UI Designer on first use, while still
        // allowing the user to detach or move it afterwards.
        if (ImGuiWindow* designer = ImGui::FindWindowByName("UI Designer")) {
            if (designer->DockId != 0) {
                ImGui::SetNextWindowDockID(designer->DockId,
                                           ImGuiCond_FirstUseEver);
            }
        }
        if (!ImGui::Begin("Output Log", &m_showOutputLog)) {
            ImGui::End();
            return;
        }

        const std::vector<EditorLogSink::Entry> entries =
            g_editorLogSink ? g_editorLogSink->Snapshot()
                            : std::vector<EditorLogSink::Entry>{};

        if (ImGui::Button("Clear")) {
            if (g_editorLogSink) g_editorLogSink->Clear();
        }
        ImGui::SameLine();
        if (ImGui::Button("Copy All") && g_editorLogSink) {
            std::string all;
            all.reserve(entries.size() * 96);
            for (const auto& entry : entries) {
                all += entry.text;
                all += '\n';
            }
            ImGui::SetClipboardText(all.c_str());
        }
        ImGui::SameLine();
        ImGui::Checkbox("Auto-scroll", &m_logAutoScroll);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(130.0f);
        const char* filters[] = {"All", "Info+", "Warnings+", "Errors"};
        ImGui::Combo("Level", &m_logLevelFilter, filters,
                     static_cast<int>(sizeof(filters) / sizeof(filters[0])));
        ImGui::SameLine();
        ImGui::TextDisabled("%zu messages", entries.size());
        ImGui::SameLine();
        ImGui::TextDisabled("| Select text and Ctrl+C to copy | PgUp/PgDn to scroll");
        ImGui::Separator();

        int threshold = static_cast<int>(spdlog::level::trace);
        switch (m_logLevelFilter) {
            case 1: threshold = static_cast<int>(spdlog::level::info); break;
            case 2: threshold = static_cast<int>(spdlog::level::warn); break;
            case 3: threshold = static_cast<int>(spdlog::level::err); break;
            default: break;
        }

        // A read-only InputTextMultiline gives native text selection and
        // Ctrl+C support. Build the visible text from the filtered snapshot.
        std::string visibleText;
        visibleText.reserve(entries.size() * 96);
        size_t visibleLines = 0;
        for (const auto& entry : entries) {
            if (static_cast<int>(entry.level) < threshold) continue;
            visibleText += entry.text;
            visibleText += '\n';
            ++visibleLines;
        }
        m_outputLogBuffer.resize(visibleText.size() + 1);
        std::copy(visibleText.begin(), visibleText.end(),
                  m_outputLogBuffer.begin());
        m_outputLogBuffer[visibleText.size()] = '\0';

        ImGui::BeginChild("OutputLogScroll", ImVec2(0, 0), false,
                          ImGuiWindowFlags_HorizontalScrollbar);
        const bool wasAtBottom =
            ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f;
        const float lineHeight = ImGui::GetTextLineHeightWithSpacing();
        // Make the read-only editor tall enough for all lines so the parent
        // child owns the scrollbar. This avoids nested scroll areas and lets
        // PageUp/PageDown move the whole log by a page.
        const float textHeight =
            lineHeight * static_cast<float>(std::max<size_t>(visibleLines, 1) + 3) +
            ImGui::GetStyle().FramePadding.y * 2.0f;
        ImGui::InputTextMultiline(
            "##OutputLogText", m_outputLogBuffer.data(),
            m_outputLogBuffer.size(),
            ImVec2(-FLT_MIN, textHeight),
            ImGuiInputTextFlags_ReadOnly);
        const ImGuiID logInputId = ImGui::GetItemID();
        ImGuiInputTextState* logInputState =
            ImGui::GetInputTextState(logInputId);
        if (logInputState) {
            if (logInputState->HasSelection()) {
                const int start = std::clamp(
                    std::min(logInputState->GetSelectionStart(),
                             logInputState->GetSelectionEnd()),
                    0, static_cast<int>(visibleText.size()));
                const int end = std::clamp(
                    std::max(logInputState->GetSelectionStart(),
                             logInputState->GetSelectionEnd()),
                    start, static_cast<int>(visibleText.size()));
                m_outputLogSelection.assign(visibleText.data() + start,
                                            static_cast<size_t>(end - start));
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Right) &&
                       !ImGui::IsPopupOpen("OutputLogContext")) {
                m_outputLogSelection.clear();
            }
        }

        // Use the parent log child for the context menu. Starting it from the
        // InputText item can be blocked by the input's own child window.
        if (ImGui::IsWindowHovered(
                ImGuiHoveredFlags_ChildWindows |
                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) &&
            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            ImGui::OpenPopup("OutputLogContext");
        }
        if (ImGui::BeginPopup("OutputLogContext")) {
            const bool hasSelection = !m_outputLogSelection.empty();
            if (ImGui::MenuItem("Copy Selected", "Ctrl+C", false,
                                hasSelection)) {
                ImGui::SetClipboardText(m_outputLogSelection.c_str());
            }
            if (ImGui::MenuItem("Select All", "Ctrl+A", false,
                                logInputState != nullptr)) {
                if (logInputState) logInputState->SelectAll();
                m_outputLogSelection = visibleText;
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Copy All")) {
                ImGui::SetClipboardText(visibleText.c_str());
            }
            ImGui::EndPopup();
        }

        bool manualScroll = false;
        const bool canScroll =
            ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) ||
            ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows);
        if (canScroll) {
            const float pageStep =
                std::max(lineHeight, ImGui::GetWindowHeight() - lineHeight * 2.0f);
            if (ImGui::IsKeyPressed(ImGuiKey_PageDown, true)) {
                ImGui::SetScrollY(std::min(ImGui::GetScrollY() + pageStep,
                                           ImGui::GetScrollMaxY()));
                manualScroll = true;
            }
            if (ImGui::IsKeyPressed(ImGuiKey_PageUp, true)) {
                ImGui::SetScrollY(std::max(ImGui::GetScrollY() - pageStep, 0.0f));
                manualScroll = true;
            }
        }
        if (m_logAutoScroll && wasAtBottom && !manualScroll) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void DrawLuaConsole() {
        ImGui::Begin("Lua Console");
        if (ImGui::Button("Clear")) m_scripts.ClearLogs();
        ImGui::Separator();
        
        ImGui::BeginChild("ConsoleScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        auto logs = m_scripts.GetLogs();
        for (const auto& log : logs) {
            ImGui::TextWrapped("%s", log.c_str());
        }
        if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 10.0f) {
            ImGui::SetScrollHereY(1.0f);
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void DrawScriptComponents() {
        ImGui::Begin("Script Components");
        ImGui::BeginChild("ComponentsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        auto& reg = m_scene.Registry();
        auto view = reg.view<Galeshi::AIControllerComponent>();
        if (view.begin() == view.end()) {
            ImGui::TextDisabled("No script components found.");
        } else {
            for (auto e : view) {
                auto& ctrl = view.get<Galeshi::AIControllerComponent>(e);
                std::string name = "Entity " + std::to_string((uint32_t)e);
                if (reg.all_of<Galeshi::NameComponent>(e)) name = reg.get<Galeshi::NameComponent>(e).name;
                ImGui::Text("%s -> %s", name.c_str(), ctrl.scriptPath.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Reload")) {
                    // Force reload AI scripts
                    m_scripts.HotReload("assets/scripts/init.lua");
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }

    void DrawContentBrowser() {
        ImGui::Begin("Content Browser");
        std::string clicked = m_browser->Draw();
        if (!clicked.empty()) OnAssetDoubleClicked(clicked);
        ImGui::End();
    }

    void OpenFileDialog() {
        const char* home = std::getenv("HOME");
        std::string start = home ? home : "/";
        m_filePicker.Open(start, "Load Model",
                          { ".gltf", ".glb", ".obj", ".dae" });
    }

    void EnsureTreeTypeMaterial(Galeshi::VegetationTreeType& type,
                                int typeIndex) {
        if (type.material) return;
        type.material = std::make_shared<Galeshi::Material>();
        type.material->name = "Tree Type " + std::to_string(typeIndex + 1) +
                              " Textures";
        type.material->twoSided = true;
        type.material->backfaceCulling = false;
        type.material->castsShadow = true;
    }

    void EnsureLeafMaterial(Galeshi::VegetationLeafSet& leaf,
                            int typeIndex, int leafIndex) {
        if (leaf.material) {
            // ORM textures carry their own metallic channel; the material
            // factor must be neutral for that channel to have an effect.
            if (leaf.material->metallicRoughnessMap) {
                leaf.material->metallicFactor = 1.0f;
                leaf.material->roughnessFactor = 1.0f;
            }
            return;
        }
        leaf.material = std::make_shared<Galeshi::Material>();
        leaf.material->name = "Tree " + std::to_string(typeIndex + 1) +
                              " Leaf " + std::to_string(leafIndex + 1);
        leaf.material->twoSided = true;
        leaf.material->backfaceCulling = false;
        leaf.material->castsShadow = true;
        // Leaf textures commonly carry an alpha mask. The shader ignores
        // this when the alpha channel is fully opaque.
        leaf.material->alphaTest = true;
        leaf.material->alphaCutoff = 0.5f;
        leaf.material->sceneOverride = true;
    }

    static bool IsLeafMeshName(const std::string& meshName,
                               const std::string& materialName) {
        const std::string haystack =
            ToLowerCopy(meshName + " " + materialName);
        for (const char* token :
             {"leaf", "leaves", "foliage", "frond", "needle", "canopy"}) {
            if (haystack.find(token) != std::string::npos) return true;
        }
        return false;
    }

    static std::string TreeMeshLabel(
        const Galeshi::AssetManager::LoadedModel& model, int index) {
        std::string label =
            index >= 0 &&
                index < static_cast<int>(model.meshNames.size()) &&
                !model.meshNames[static_cast<size_t>(index)].empty()
                ? model.meshNames[static_cast<size_t>(index)]
                : "Mesh " + std::to_string(index);
        if (index >= 0 &&
            index < static_cast<int>(model.meshMaterialIndex.size())) {
            const int materialIndex =
                model.meshMaterialIndex[static_cast<size_t>(index)];
            if (materialIndex >= 0 &&
                materialIndex <
                    static_cast<int>(model.materials.size()) &&
                model.materials[static_cast<size_t>(materialIndex)]) {
                const auto& material =
                    model.materials[static_cast<size_t>(materialIndex)];
                if (!material->name.empty()) {
                    label += " (" + material->name + ")";
                }
            }
        }
        return label;
    }

    static int SuggestedLeafMeshIndex(
        const Galeshi::AssetManager::LoadedModel& model,
        const std::vector<Galeshi::VegetationLeafSet>& leafSets) {
        std::vector<bool> used(model.meshes.size(), false);
        for (const auto& leaf : leafSets) {
            if (leaf.meshIndex >= 0 &&
                leaf.meshIndex < static_cast<int>(used.size())) {
                used[static_cast<size_t>(leaf.meshIndex)] = true;
            }
        }
        for (int pass = 0; pass < 2; ++pass) {
            for (int i = 0; i < static_cast<int>(model.meshes.size()); ++i) {
                if (used[static_cast<size_t>(i)]) continue;
                if (!model.meshes[static_cast<size_t>(i)] ||
                    !model.meshes[static_cast<size_t>(i)]->Valid()) {
                    continue;
                }
                std::string materialName;
                if (i < static_cast<int>(model.meshMaterialIndex.size())) {
                    const int materialIndex =
                        model.meshMaterialIndex[static_cast<size_t>(i)];
                    if (materialIndex >= 0 &&
                        materialIndex <
                            static_cast<int>(model.materials.size()) &&
                        model.materials[static_cast<size_t>(materialIndex)]) {
                        materialName =
                            model.materials[static_cast<size_t>(materialIndex)]
                                ->name;
                    }
                }
                const std::string meshName =
                    i < static_cast<int>(model.meshNames.size())
                        ? model.meshNames[static_cast<size_t>(i)]
                        : std::string{};
                if (pass == 1 || IsLeafMeshName(meshName, materialName)) {
                    return i;
                }
            }
        }
        return -1;
    }

    // Assign a static tree model to a Landscape's instanced vegetation.
    // The model is validated before the component/path is changed so a
    // broken FBX cannot silently replace a working tree mesh.
    void AssignLandscapeTreeModel(Galeshi::Entity landscapeEntity,
                                  const std::string& originalPath,
                                  int typeIndex = -1) {
        if (!m_assets || !landscapeEntity) return;
        if (!landscapeEntity.HasComponent<Galeshi::LandscapeComponent>()) {
            spdlog::warn("[Editor] Tree model '{}' was dropped on a non-landscape entity",
                         originalPath);
            return;
        }
        if (!IsDragDropModelAssetPath(originalPath)) {
            spdlog::warn("[Editor] Unsupported tree model format: {} "
                         "(FBX drag & drop is disabled)",
                         originalPath);
            return;
        }

        const std::string path = m_assets->MakeRelativePath(originalPath);
        auto model = m_assets->LoadModel(path);
        if (model.meshes.empty()) {
            spdlog::error("[Editor] Cannot use '{}' as a landscape tree: "
                          "the model contains no meshes", path);
            return;
        }

        PushHistory();
        auto& landscape =
            landscapeEntity.GetComponent<Galeshi::LandscapeComponent>();
        auto& vegetation =
            landscapeEntity.HasComponent<Galeshi::VegetationComponent>()
                ? landscapeEntity.GetComponent<Galeshi::VegetationComponent>()
                : landscapeEntity.AddComponent<Galeshi::VegetationComponent>();

        // A dropped model keeps its native size. The per-field Size control
        // remains available for tuning afterwards.
        if (typeIndex >= 0 &&
            typeIndex < static_cast<int>(vegetation.treeTypes.size())) {
            auto& type = vegetation.treeTypes[static_cast<size_t>(typeIndex)];
            if (type.meshPath != path) {
                // Leaf overrides address mesh indices in the old model.
                type.leafSets.clear();
            }
            type.meshPath = path;
            // Imported source trees are commonly authored in centimeter-like
            // units. Use 0.010 as the default multiplier instead of making
            // the first drop gigantic.
            type.scale = 0.010f;
            if (type.density <= 0.0f) type.density = 1.0f;
            EnsureTreeTypeMaterial(type, typeIndex);
        } else {
            Galeshi::VegetationTreeType type;
            type.meshPath = path;
            type.scale = 0.010f;
            type.density = 1.0f;
            vegetation.treeTypes.push_back(std::move(type));
            const int newIndex =
                static_cast<int>(vegetation.treeTypes.size()) - 1;
            EnsureTreeTypeMaterial(vegetation.treeTypes.back(), newIndex);
            m_paintTreeType = newIndex;
        }
        vegetation.treeMeshPath.clear();
        vegetation.enabled = true;
        vegetation.dirty = true;
        Galeshi::VegetationGenerator::Generate(landscape, vegetation);
        m_editor.Select(landscapeEntity);

        spdlog::info("[Editor] Landscape tree model: {} ({} meshes, {} materials)",
                     path, model.meshes.size(), model.materials.size());
    }

    // -------------------------------------------------------------------
    // Character model replacement
    // -------------------------------------------------------------------
    static std::string MaterialNameKey(const std::string& name) {
        std::string key;
        key.reserve(name.size());
        for (unsigned char c : name) {
            if (std::isalnum(c)) {
                key.push_back(
                    static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
            }
        }
        return key;
    }

    static float ModelMeshHeight(const std::vector<std::shared_ptr<Galeshi::Mesh>>& meshes) {
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        for (const auto& mesh : meshes) {
            if (!mesh) continue;
            for (const auto& vertex : mesh->Vertices()) {
                minY = std::min(minY, vertex.position.y);
                maxY = std::max(maxY, vertex.position.y);
            }
        }
        return maxY > minY ? maxY - minY : 0.0f;
    }

    // Height of the already-authored model in world space. This must be
    // measured per mesh entity rather than through the wrapper's local scale:
    // the wrapper can be the skeleton root, a capsule child, or omitted
    // entirely for older scenes.
    float ModelEntityWorldHeight(
        const std::vector<entt::entity>& meshEntities) {
        float minY = std::numeric_limits<float>::max();
        float maxY = std::numeric_limits<float>::lowest();
        auto& reg = m_scene.Registry();
        for (entt::entity entity : meshEntities) {
            if (!reg.valid(entity) ||
                !reg.all_of<Galeshi::MeshComponent>(entity)) {
                continue;
            }
            const auto& mesh = reg.get<Galeshi::MeshComponent>(entity).mesh;
            if (!mesh) continue;
            const glm::mat4 world = EntityWorldMatrix(entity);
            for (const auto& vertex : mesh->Vertices()) {
                const float y =
                    (world * glm::vec4(vertex.position, 1.0f)).y;
                minY = std::min(minY, y);
                maxY = std::max(maxY, y);
            }
        }
        return maxY > minY ? maxY - minY : 0.0f;
    }

    // Imported objects start with a non-metallic material. Clone the asset
    // material so this default does not mutate the AssetManager cache or
    // another instance the artist already adjusted manually.
    static std::shared_ptr<Galeshi::Material> ImportedMaterial(
        const std::shared_ptr<Galeshi::Material>& source,
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>>& clones) {
        if (!source) return nullptr;
        auto [it, inserted] = clones.try_emplace(source.get());
        if (inserted) {
            it->second = std::make_shared<Galeshi::Material>(*source);
            it->second->metallicFactor = 0.0f;
            it->second->sceneOverride = true;
        }
        return it->second;
    }

    Galeshi::Entity CreateMaterialSlotEntity(
        entt::entity parent, const std::string& name,
        const std::shared_ptr<Galeshi::Material>& sourceMaterial) {
        if (!m_scene.Registry().valid(parent) || !sourceMaterial) return {};
        auto placed = std::make_shared<Galeshi::Material>(*sourceMaterial);
        if (placed->name.empty()) placed->name = name;
        auto slot = m_scene.CreateEntity("__material_slot__" + name);
        slot.AddComponent<Galeshi::ParentComponent>(
            Galeshi::ParentComponent{parent});
        auto& matc = slot.AddComponent<Galeshi::MaterialComponent>(
            Galeshi::MaterialComponent{placed});
        matc.isGlass = placed->isGlass;
        return slot;
    }

    Galeshi::Entity FindSkeletalRootForReplacement(entt::entity target) {
        auto& reg = m_scene.Registry();
        if (!reg.valid(target)) return {};
        if (reg.all_of<Galeshi::SkeletalComponent>(target))
            return Galeshi::Entity(target, &m_scene);

        // A capsule/model attachment may be selected instead of the character
        // root. Search both directions so the drop target is forgiving.
        std::vector<entt::entity> queue{target};
        for (size_t head = 0; head < queue.size(); ++head) {
            const entt::entity current = queue[head];
            auto children = reg.view<Galeshi::ParentComponent>();
            for (auto child : children) {
                if (children.get<Galeshi::ParentComponent>(child).parent ==
                    current) {
                    if (reg.all_of<Galeshi::SkeletalComponent>(child))
                        return Galeshi::Entity(child, &m_scene);
                    queue.push_back(child);
                }
            }
        }

        entt::entity current = target;
        for (int depth = 0; depth < 128; ++depth) {
            if (!reg.all_of<Galeshi::ParentComponent>(current)) break;
            current = reg.get<Galeshi::ParentComponent>(current).parent;
            if (!reg.valid(current)) break;
            if (reg.all_of<Galeshi::SkeletalComponent>(current))
                return Galeshi::Entity(current, &m_scene);
        }
        return {};
    }

    bool PathsReferToSameAsset(const std::string& a, const std::string& b) {
        if (a.empty() || b.empty() || !m_assets) return false;
        const std::string ra = m_assets->MakeRelativePath(a);
        const std::string rb = m_assets->MakeRelativePath(b);
        if (ra == rb) return true;
        std::error_code ecA, ecB;
        const auto ca = std::filesystem::weakly_canonical(ra, ecA);
        const auto cb = std::filesystem::weakly_canonical(rb, ecB);
        return !ecA && !ecB && ca == cb;
    }

    Galeshi::Entity FindModelContainer(entt::entity skeletonRoot,
                                       const std::string& sourcePath,
                                       std::vector<entt::entity>& outMeshes) {
        auto& reg = m_scene.Registry();
        outMeshes.clear();
        if (!reg.valid(skeletonRoot)) return {};

        std::vector<entt::entity> descendants{skeletonRoot};
        for (size_t head = 0; head < descendants.size(); ++head) {
            const entt::entity current = descendants[head];
            auto children = reg.view<Galeshi::ParentComponent>();
            for (auto child : children) {
                if (children.get<Galeshi::ParentComponent>(child).parent ==
                    current) {
                    descendants.push_back(child);
                }
            }
        }

        for (entt::entity entity : descendants) {
            if (!reg.all_of<Galeshi::MeshComponent>(entity)) continue;
            const auto& mesh = reg.get<Galeshi::MeshComponent>(entity);
            if (!mesh.mesh || mesh.sourceAssetPath.empty()) continue;
            if (!sourcePath.empty() &&
                !PathsReferToSameAsset(mesh.sourceAssetPath, sourcePath)) {
                continue;
            }
            outMeshes.push_back(entity);
        }
        if (outMeshes.empty()) return Galeshi::Entity(skeletonRoot, &m_scene);

        // Climb from a sub-mesh to the outermost imported-model wrapper. Stop
        // at gameplay attachment nodes such as a CollisionCapsule, so the
        // capsule and its parenting are never removed during a swap.
        entt::entity container = skeletonRoot;
        bool containerChosen = false;
        for (entt::entity meshEntity : outMeshes) {
            entt::entity candidate = meshEntity;
            entt::entity parent = entt::null;
            if (reg.all_of<Galeshi::ParentComponent>(meshEntity))
                parent = reg.get<Galeshi::ParentComponent>(meshEntity).parent;

            for (int depth = 0; depth < 128; ++depth) {
                if (!reg.valid(parent) || parent == skeletonRoot) {
                    candidate = skeletonRoot;
                    break;
                }
                if (reg.all_of<Galeshi::ColliderComponent>(parent) ||
                    reg.all_of<Galeshi::RigidBodyComponent>(parent) ||
                    reg.all_of<Galeshi::SkeletalComponent>(parent)) {
                    break;
                }
                candidate = parent;
                if (!reg.all_of<Galeshi::ParentComponent>(parent)) break;
                parent = reg.get<Galeshi::ParentComponent>(parent).parent;
            }

            if (!containerChosen) {
                container = candidate;
                containerChosen = true;
            }
        }
        return reg.valid(container)
            ? Galeshi::Entity(container, &m_scene)
            : Galeshi::Entity(skeletonRoot, &m_scene);
    }

    // Replace the skinned/static model below a character root (or the model
    // attached to its capsule) with a glTF/GLB asset. This is intentionally
    // separate from SpawnFromModel: the character root, capsule physics and
    // all hierarchy links stay intact.
    bool ReplaceCharacterModel(entt::entity target,
                               const std::string& originalPath) {
        if (!m_assets) return false;
        if (!IsGltfModelAssetPath(originalPath)) {
            spdlog::warn(
                "[ModelSwap] '{}' is not a glTF/GLB asset. FBX drag & drop "
                "replacement is disabled; export the new character as glTF.",
                originalPath);
            return false;
        }
        Galeshi::Entity skeletonEntity = FindSkeletalRootForReplacement(target);
        if (!skeletonEntity) {
            spdlog::warn(
                "[ModelSwap] No skeletal character found around the drop "
                "target. Drop the glTF on the character or its capsule.");
            return false;
        }

        auto& reg = m_scene.Registry();
        auto& skeleton =
            reg.get<Galeshi::SkeletalComponent>(skeletonEntity.Handle());
        const std::string oldPath = skeleton.sourcePath;

        std::vector<entt::entity> oldMeshEntities;
        Galeshi::Entity modelContainer =
            FindModelContainer(skeletonEntity.Handle(), oldPath,
                               oldMeshEntities);
        if (!modelContainer) modelContainer = skeletonEntity;

        // If the old model has no source path on the skeleton, gather every
        // descendant imported mesh (not primitives) as a fallback.
        if (oldMeshEntities.empty()) {
            modelContainer = FindModelContainer(skeletonEntity.Handle(), {},
                                                oldMeshEntities);
        }

        const std::string newPath = m_assets->MakeRelativePath(originalPath);
        auto newModel = m_assets->LoadModel(newPath);
        if (newModel.meshes.empty()) {
            spdlog::error("[ModelSwap] glTF '{}' contains no meshes", newPath);
            return false;
        }
        if (newModel.totalBones == 0) {
            // Replacing a skinned character with a static glTF destroys the
            // skeleton that external clips retarget onto. Keep the working
            // character instead of silently turning it into a T-pose prop.
            spdlog::error(
                "[ModelSwap] '{}' has no skin/armature; keeping the existing "
                "character model. Export the glTF with the armature and skin.",
                newPath);
            return false;
        }

        // Capture old material values before the old mesh entities disappear.
        struct MaterialSlot {
            std::string key;
            std::string label;
            int meshIndex = -1;
            std::shared_ptr<Galeshi::Material> material;
            bool used = false;
        };
        std::vector<MaterialSlot> oldSlots;
        oldSlots.reserve(oldMeshEntities.size());
        for (entt::entity entity : oldMeshEntities) {
            if (!reg.all_of<Galeshi::MaterialComponent>(entity)) continue;
            const auto& matc = reg.get<Galeshi::MaterialComponent>(entity);
            if (!matc.material) continue;
            MaterialSlot slot;
            slot.key = MaterialNameKey(matc.material->name);
            slot.label = matc.material->name;
            if (slot.label.empty() &&
                reg.all_of<Galeshi::NameComponent>(entity)) {
                // Empty material names keep the Blender object/slot name as a
                // non-persisted display label.
                slot.label =
                    reg.get<Galeshi::NameComponent>(entity).name;
            }
            if (reg.all_of<Galeshi::MeshComponent>(entity))
                slot.meshIndex = reg.get<Galeshi::MeshComponent>(entity).meshIndex;
            slot.material = matc.material;
            oldSlots.push_back(std::move(slot));
        }
        std::sort(oldSlots.begin(), oldSlots.end(),
                  [](const MaterialSlot& a, const MaterialSlot& b) {
                      if (a.meshIndex != b.meshIndex)
                          return a.meshIndex < b.meshIndex;
                      return a.material.get() < b.material.get();
                  });

        float oldHeight = 0.0f;
        {
            std::vector<std::shared_ptr<Galeshi::Mesh>> oldMeshes;
            for (entt::entity entity : oldMeshEntities) {
                if (reg.all_of<Galeshi::MeshComponent>(entity)) {
                    const auto& mesh =
                        reg.get<Galeshi::MeshComponent>(entity);
                    if (mesh.mesh) oldMeshes.push_back(mesh.mesh);
                }
            }
            oldHeight = ModelMeshHeight(oldMeshes);
        }
        const float newHeight = ModelMeshHeight(newModel.meshes);

        const bool containerWasMesh =
            std::find(oldMeshEntities.begin(), oldMeshEntities.end(),
                      modelContainer.Handle()) != oldMeshEntities.end();
        if (containerWasMesh) modelContainer = skeletonEntity;

        glm::vec3 oldContainerScale(1.0f);
        if (modelContainer.HasComponent<Galeshi::TransformComponent>()) {
            oldContainerScale =
                modelContainer.GetComponent<Galeshi::TransformComponent>().scale;
        }

        // Measure the old model in world units. The model wrapper may be
        // parented below the character root and/or a capsule, so its local
        // scale alone is not enough: the old FBX wrapper in the sample scene
        // is 0.01 while its parent chain has another 2x character scale.
        // Using the actual mesh entity world matrices avoids double-counting
        // the wrapper and keeps the new model the same world size even if the
        // wrapper was selected as the skeleton root.
        const float oldWorldHeight =
            ModelEntityWorldHeight(oldMeshEntities);

        entt::entity modelParent = entt::null;
        if (reg.all_of<Galeshi::ParentComponent>(modelContainer.Handle())) {
            modelParent =
                reg.get<Galeshi::ParentComponent>(modelContainer.Handle()).parent;
        }
        float parentWorldScaleY = 1.0f;
        if (reg.valid(modelParent)) {
            const glm::mat4 parentWorld = EntityWorldMatrix(modelParent);
            const float measured = glm::length(glm::vec3(parentWorld[1]));
            if (measured > 1e-6f) parentWorldScaleY = measured;
        }

        PushHistory();

        // Remove only old imported mesh nodes. Capsules, cameras and other
        // gameplay attachments are deliberately left in place.
        for (entt::entity entity : oldMeshEntities) {
            if (reg.valid(entity)) {
                if (m_editor.IsSelected(Galeshi::Entity(entity, &m_scene)))
                    m_editor.RemoveFromSelection(
                        Galeshi::Entity(entity, &m_scene));
                m_scene.DestroyEntity(Galeshi::Entity(entity, &m_scene));
            }
        }

        // Preserve the character's visible world-space size. The local mesh
        // height and the wrapper's world scale are separate quantities:
        // multiplying the old wrapper scale by oldHeight/newHeight
        // double-counts parent/capsule scales and made swaps either gigantic
        // or invisible. Solve for the new local scale from the world-space
        // bind-pose height instead.
        if (modelContainer.HasComponent<Galeshi::TransformComponent>() &&
            oldWorldHeight > 1e-4f && newHeight > 1e-4f &&
            parentWorldScaleY > 1e-6f) {
            const float targetLocalScaleY =
                oldWorldHeight / (newHeight * parentWorldScaleY);
            if (std::isfinite(targetLocalScaleY) &&
                targetLocalScaleY > 1e-5f && targetLocalScaleY < 1e5f) {
                auto& transform =
                    modelContainer.GetComponent<Galeshi::TransformComponent>();
                transform.scale = glm::vec3(targetLocalScaleY);
                spdlog::info(
                    "[ModelSwap] Preserving world height: oldWorld={:.3f} "
                    "newLocal={:.3f} parentScaleY={:.3f} targetScale={:.4f} "
                    "(oldLocal={:.4f})",
                    oldWorldHeight, newHeight, parentWorldScaleY,
                    targetLocalScaleY, oldContainerScale.y);
            } else {
                spdlog::warn(
                    "[ModelSwap] Invalid scale correction "
                    "(oldWorld={}, newLocal={}, parentScaleY={}); "
                    "keeping wrapper transform",
                    oldWorldHeight, newHeight, parentWorldScaleY);
            }
        } else if (modelContainer.HasComponent<Galeshi::TransformComponent>()) {
            // No usable old geometry (or no new geometry): keep the wrapper
            // transform untouched rather than writing a zero/NaN scale.
            auto& transform =
                modelContainer.GetComponent<Galeshi::TransformComponent>();
            if (!std::isfinite(transform.scale.y) ||
                std::abs(transform.scale.y) < 1e-5f) {
                transform.scale = oldContainerScale;
            }
            spdlog::warn(
                "[ModelSwap] Could not measure bind-pose height "
                "(oldLocal={}, newLocal={}); scale left at {:.4f}",
                oldHeight, newHeight, transform.scale.y);
        }

        const std::string stem =
            std::filesystem::path(newPath).stem().string();
        size_t preservedMaterials = 0;
        std::vector<entt::entity> newMeshEntities;
        newMeshEntities.reserve(newModel.meshes.size());
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>> importedMaterials;
        for (size_t i = 0; i < newModel.meshes.size(); ++i) {
            std::shared_ptr<Galeshi::Material> newMaterial;
            if (i < newModel.meshMaterialIndex.size()) {
                const int materialIndex = newModel.meshMaterialIndex[i];
                if (materialIndex >= 0 &&
                    materialIndex < static_cast<int>(newModel.materials.size())) {
                    newMaterial = ImportedMaterial(
                        newModel.materials[static_cast<size_t>(materialIndex)],
                        importedMaterials);
                }
            }
            if (!newMaterial && !newModel.materials.empty())
                newMaterial = ImportedMaterial(
                    newModel.materials.front(), importedMaterials);
            if (!newMaterial)
                newMaterial = std::make_shared<Galeshi::Material>();

            const std::string newKey = MaterialNameKey(newMaterial->name);
            int matchedOld = -1;
            if (!newKey.empty()) {
                for (size_t s = 0; s < oldSlots.size(); ++s) {
                    if (!oldSlots[s].used && oldSlots[s].key == newKey) {
                        matchedOld = static_cast<int>(s);
                        break;
                    }
                }
            }
            if (matchedOld < 0) {
                for (size_t s = 0; s < oldSlots.size(); ++s) {
                    if (!oldSlots[s].used &&
                        oldSlots[s].meshIndex == static_cast<int>(i)) {
                        matchedOld = static_cast<int>(s);
                        break;
                    }
                }
            }

            if (matchedOld >= 0) {
                // Use the material values authored in the replacement glTF.
                // Only the material slot itself is retained; the old FBX
                // factors and texture maps must not override the file.
                oldSlots[static_cast<size_t>(matchedOld)].used = true;
                ++preservedMaterials;
            }

            const std::string meshName =
                (i < newModel.meshNames.size() && !newModel.meshNames[i].empty())
                    ? newModel.meshNames[i]
                    : stem + "_" + std::to_string(i);
            auto meshEntity = m_scene.CreateEntity(meshName);
            meshEntity.AddComponent<Galeshi::ParentComponent>(
                Galeshi::ParentComponent{modelContainer.Handle()});
            meshEntity.AddComponent<Galeshi::MeshComponent>(
                Galeshi::MeshComponent{newModel.meshes[i], newPath, "",
                                       static_cast<int>(i)});
            auto& matc =
                meshEntity.AddComponent<Galeshi::MaterialComponent>(
                    Galeshi::MaterialComponent{newMaterial});
            matc.isGlass = newMaterial->isGlass;
            newMeshEntities.push_back(meshEntity.Handle());
        }

        // Preserve old material slots that have no corresponding new mesh.
        // This keeps custom material slots (for example an extra boot or
        // accessory slot) visible in Details even when the replacement asset
        // has fewer primitives.
        size_t retainedMaterialSlots = 0;
        for (auto& slot : oldSlots) {
            if (slot.used || !slot.material) continue;
            const std::string slotName = slot.label.empty()
                ? "Retained Material"
                : slot.label;
            if (CreateMaterialSlotEntity(modelContainer.Handle(), slotName,
                                         slot.material)) {
                slot.used = true;
                ++retainedMaterialSlots;
            }
        }

        spdlog::info("[ModelSwap] New world height after swap: {:.3f}",
                     ModelEntityWorldHeight(newMeshEntities));

        skeleton.sourcePath = newPath;
        skeleton.currentTime = 0.0f;
        skeleton.activeAnimation = 0;
        skeleton.isAnimated = newModel.hasAnimation;
        skeleton.boneMatrices.assign(256, glm::mat4(1.0f));

        if (auto* machine =
                reg.try_get<Galeshi::AnimationStateMachineComponent>(
                    skeletonEntity.Handle())) {
            auto findClip = [&](const std::string& wanted) {
                if (wanted.empty()) return -1;
                const std::string wantedKey = MaterialNameKey(wanted);
                for (size_t i = 0; i < newModel.animationNames.size(); ++i) {
                    if (newModel.animationNames[i] == wanted) {
                        return static_cast<int>(i);
                    }
                }
                for (size_t i = 0; i < newModel.animationNames.size(); ++i) {
                    if (MaterialNameKey(newModel.animationNames[i]) == wantedKey) {
                        return static_cast<int>(i);
                    }
                }
                return -1;
            };
            if (!newModel.animationNames.empty()) {
                for (auto& state : machine->states) {
                    if (!state.animationPath.empty()) continue;
                    int clip = findClip(state.animationName);
                    if (clip < 0) clip = findClip(state.name);
                    if (clip < 0 && state.clip >= 0 &&
                        state.clip < static_cast<int>(newModel.animationNames.size())) {
                        clip = state.clip;
                    }
                    if (clip < 0) clip = 0;
                    state.clip = clip;
                    state.animationName =
                        newModel.animationNames[static_cast<size_t>(clip)];
                }
                machine->currentState = glm::clamp(
                    machine->currentState, 0,
                    std::max(0, static_cast<int>(machine->states.size()) - 1));
                machine->previousState = -1;
                machine->stateTime = 0.0f;
                machine->previousTime = 0.0f;
                machine->blendElapsed = 0.0f;
                machine->initialized = false;
            }
        }

        if (!reg.all_of<Galeshi::ModelComponent>(skeletonEntity.Handle())) {
            reg.emplace<Galeshi::ModelComponent>(skeletonEntity.Handle());
        }
        m_editor.Select(skeletonEntity);
        m_stateMachineEditorEntity = skeletonEntity.Handle();
        m_showStateMachineEditor = true;
        m_editor.camTarget =
            glm::vec3(EntityWorldMatrix(skeletonEntity.Handle())[3]);
        spdlog::info(
            "[ModelSwap] Replaced '{}' with '{}' ({} meshes, {} matched "
            "material slots, {} retained old slots)",
            oldPath, newPath, newModel.meshes.size(), preservedMaterials,
            retainedMaterialSlots);
        return true;
    }

    void OnAssetDoubleClicked(const std::string& path) {
        if (IsCodeAssetPath(path)) {
            OpenCodeFile(path);
            return;
        }
        if (IsModelAssetPath(path)) {
            SpawnFromModel(path, glm::vec3(0.0f));
        } else if (IsImageAssetPath(path)) {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::MaterialComponent>()) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    matc.material->albedoMap = m_assets->LoadTexture(path, true);
                    spdlog::info("[Editor] Applied texture to selected: {}", path);
                }
            }
        }
    }

    glm::vec3 ViewportDropWorldPosition(const ImVec2& mouse, const ImVec2& imageMin,
                                        uint16_t width, uint16_t height) const {
        if (width == 0 || height == 0) return m_editor.camTarget;

        const float u = (mouse.x - imageMin.x) / static_cast<float>(width);
        const float v = (mouse.y - imageMin.y) / static_cast<float>(height);
        const float ndcX = u * 2.0f - 1.0f;
        const float ndcY = 1.0f - v * 2.0f;

        const glm::mat4 invVP =
            glm::inverse(m_camera.Projection() * m_camera.View());
        glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
        glm::vec4 farPoint  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
        if (std::abs(nearPoint.w) < 1e-6f || std::abs(farPoint.w) < 1e-6f)
            return m_editor.camTarget;

        nearPoint /= nearPoint.w;
        farPoint  /= farPoint.w;
        const glm::vec3 rayOrigin(nearPoint);
        const glm::vec3 rayDir = glm::normalize(glm::vec3(farPoint - nearPoint));

        // Build/empty scenes use the y=0 plane as their placement surface.
        // Falling back to the camera target keeps drops usable even when the
        // camera is looking up or parallel to that plane.
        if (std::abs(rayDir.y) > 1e-4f) {
            const float t = -rayOrigin.y / rayDir.y;
            if (t > 0.0f && t < m_camera.FarPlane()) return rayOrigin + rayDir * t;
        }
        return m_editor.camTarget;
    }

    void OnAssetDropped(const std::string& path, glm::vec3 pos) {
        if (IsCodeAssetPath(path)) {
            OpenCodeFile(path);
            return;
        }
        if (IsModelAssetPath(path)) {
            if (!IsDragDropModelAssetPath(path)) {
                spdlog::warn(
                    "[Editor] FBX cannot be dropped as an asset. "
                    "Use glTF/GLB, OBJ or DAE: {}",
                    path);
                return;
            }
            // Dropping a model onto the selected Landscape assigns it as
            // the instanced tree mesh. A glTF dropped while a character or
            // its capsule is selected replaces the character model in place;
            // otherwise spawn a regular model entity.
            Galeshi::Entity selected = m_editor.Selected(&m_scene);
            if (selected &&
                selected.HasComponent<Galeshi::LandscapeComponent>()) {
                AssignLandscapeTreeModel(selected, path);
            } else if (IsGltfModelAssetPath(path) && selected &&
                       FindSkeletalRootForReplacement(selected.Handle())) {
                ReplaceCharacterModel(selected.Handle(), path);
            } else {
                SpawnFromModel(path, pos, true);
            }
            return;
        }

        // Dropping an image on a material sets its albedo map.
        if (IsImageAssetPath(path)) {
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::MaterialComponent>()) {
                auto& matc = sel.GetComponent<Galeshi::MaterialComponent>();
                if (matc.material) {
                    matc.material->albedoMap = m_assets->LoadTexture(path, true);
                    spdlog::info("[Editor] Set albedo map: {}", path);
                }
            }
        }
    }

    void SpawnFromModel(const std::string& originalPath, glm::vec3 pos,
                        bool placeAtPosition = false) {
        PushHistory();
        const std::string path = m_assets->MakeRelativePath(originalPath);
        auto model = m_assets->LoadModel(path);
        if (model.meshes.empty()) return;
        namespace fs = std::filesystem;
        const auto stem = fs::path(path).stem().string();

        auto root = m_scene.CreateEntity(stem);
        root.AddComponent<Galeshi::ModelComponent>();
        auto& rootTf = root.GetComponent<Galeshi::TransformComponent>();
        
        // Model imports are normally created at the origin. A viewport drop
        // additionally positions the new root at the drop point.
        rootTf.position = placeAtPosition ? pos : glm::vec3(0.0f);
        rootTf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        rootTf.scale = glm::vec3(1.0f);
        
        // 2. If the model imports flipped (Z-up), uncomment the next line:
        rootTf.rotation = glm::angleAxis(glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        if (model.hasAnimation || model.totalBones > 0) {
            auto& skel = root.AddComponent<Galeshi::SkeletalComponent>();
            skel.isAnimated = model.hasAnimation;
            skel.sourcePath = path;
            skel.boneMatrices.resize(256, glm::mat4(1.0f));

            if (model.hasAnimation) {
                auto& machine =
                    root.AddComponent<Galeshi::AnimationStateMachineComponent>();
                machine = Galeshi::AnimationSystem::CreateDefaultStateMachine(
                    model.animationNames);
                machine.parameters = {
                    {"Speed", 0.0f},
                    {"PlayerVisible", 0.0f},
                    {"TargetDistance", 100.0f}
                };
            }
        }

        std::unordered_set<int> usedMaterialSlots;
        std::unordered_map<const Galeshi::Material*,
                           std::shared_ptr<Galeshi::Material>> importedMaterials;
        for (size_t i = 0; i < model.meshes.size(); ++i) {
            const std::string meshName =
                (i < model.meshNames.size() && !model.meshNames[i].empty())
                    ? model.meshNames[i]
                    : stem + "_" + std::to_string(i);
            auto e = m_scene.CreateEntity(meshName);
            
            // NOTE: We do NOT reset child transforms here. Assimp gives them local offsets.
            
            e.AddComponent<Galeshi::ParentComponent>(Galeshi::ParentComponent{ root.Handle() });
            e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ model.meshes[i], path, "", static_cast<int>(i) });
            
            std::shared_ptr<Galeshi::Material> mat;
            if (i < model.meshMaterialIndex.size()) {
                int matIdx = model.meshMaterialIndex[i];
                if (matIdx >= 0 && matIdx < static_cast<int>(model.materials.size())) {
                    mat = ImportedMaterial(
                        model.materials[static_cast<size_t>(matIdx)],
                        importedMaterials);
                    usedMaterialSlots.insert(matIdx);
                }
            }
            if (!mat && !model.materials.empty())
                mat = ImportedMaterial(model.materials[0], importedMaterials);
            if (!mat)
                mat = std::make_shared<Galeshi::Material>();
            // Leave the exported material name untouched. Empty names are
            // valid and must not be replaced with a fabricated one.
            e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        }

        // Some exporters write named materials that are not referenced by a
        // visible primitive. Keep only named slots; a synthetic unnamed
        // material is not a Blender material and must not create a fake entry.
        for (size_t i = 0; i < model.materials.size(); ++i) {
            if (usedMaterialSlots.count(static_cast<int>(i)) != 0 ||
                !model.materials[i] || model.materials[i]->name.empty()) {
                continue;
            }
            CreateMaterialSlotEntity(
                root.Handle(), model.materials[i]->name,
                ImportedMaterial(model.materials[i], importedMaterials));
        }

        m_editor.Select(root);
        m_editor.camTarget = pos;
        m_editor.camRadius = 5.0f;
        if (root.HasComponent<Galeshi::SkeletalComponent>()) {
            m_stateMachineEditorEntity = root.Handle();
            m_showStateMachineEditor = true;
        }
    }

    // -------------------------------------------------------------------
    // Scene Helpers
    // -------------------------------------------------------------------
    Galeshi::Entity MakeCube(const std::string& name, glm::vec3 pos) {
        auto e = m_scene.CreateEntity(name);
        auto mesh = m_assets->CreateCubeMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "cube" });
        auto mat = std::make_shared<Galeshi::Material>();
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        e.GetComponent<Galeshi::TransformComponent>().position = pos;
        return e;
    }

    Galeshi::Entity MakeSphere(const std::string& name, glm::vec3 pos) {
        auto e = m_scene.CreateEntity(name);
        auto mesh = m_assets->CreateSphereMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "sphere" });
        auto mat = std::make_shared<Galeshi::Material>();
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        e.GetComponent<Galeshi::TransformComponent>().position = pos;
        return e;
    }

    // -------------------------------------------------------------------
    // Collision shapes
    // -------------------------------------------------------------------
    glm::mat4 EntityWorldMatrix(entt::entity handle) {
        glm::mat4 world(1.0f);
        std::vector<entt::entity> chain;
        entt::entity cur = handle;
        int depth = 0;
        while (cur != entt::null && depth++ < 128) {
            chain.push_back(cur);
            if (m_scene.Registry().all_of<Galeshi::ParentComponent>(cur))
                cur = m_scene.Registry().get<Galeshi::ParentComponent>(cur).parent;
            else
                break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (m_scene.Registry().all_of<Galeshi::TransformComponent>(*it))
                world = world * m_scene.Registry()
                    .get<Galeshi::TransformComponent>(*it).LocalMatrix();
        }
        return world;
    }

    Galeshi::Entity SpawnCollisionShape(const std::string& name,
                                        Galeshi::ColliderShapeType type) {
        auto e = m_scene.CreateEntity(name);
        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = type;

        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
        rb.bodyId = 0xFFFFFFFFu;

        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = m_editor.camTarget;
        m_editor.Select(e);
        spdlog::info("[Physics] Created collision shape '{}'", name);
        return e;
    }

    // Recreate the Jolt body after shape or rigid-body properties changed.
    // Bodies are only created when the scene is playing, so entities without
    // a runtime body are left alone.
    void RebuildPhysicsBody(Galeshi::Entity entity) {
        if (!m_physics || !entity) return;
        auto& reg = m_scene.Registry();
        const entt::entity handle = entity.Handle();
        if (!reg.all_of<Galeshi::ColliderComponent, Galeshi::RigidBodyComponent,
                        Galeshi::TransformComponent>(handle)) {
            return;
        }
        auto& rb = reg.get<Galeshi::RigidBodyComponent>(handle);
        if (rb.bodyId == 0xFFFFFFFFu) return;

        m_physics->DestroyBody(rb.bodyId);
        rb.bodyId = 0xFFFFFFFFu;

        const auto& collider = reg.get<Galeshi::ColliderComponent>(handle);
        const glm::mat4 world = EntityWorldMatrix(handle);
        glm::vec3 scale, translation, skew;
        glm::vec4 perspective;
        glm::quat rotation;
        glm::decompose(world, scale, rotation, translation, skew, perspective);
        rb.bodyId = m_physics->CreateCollider(collider, rb, translation,
                                              glm::normalize(rotation), handle);
    }

    // Creates runtime bodies for every authored collider that does not have
    // one yet. Called when Play starts.
    void EnsurePhysicsBodies() {
        if (!m_physics) return;
        auto& reg = m_scene.Registry();
        auto view = reg.view<Galeshi::ColliderComponent, Galeshi::RigidBodyComponent,
                             Galeshi::TransformComponent>();
        int created = 0;
        for (auto e : view) {
            auto& rb = view.get<Galeshi::RigidBodyComponent>(e);
            if (rb.bodyId != 0xFFFFFFFFu) continue;
            const auto& collider = view.get<Galeshi::ColliderComponent>(e);
            const glm::mat4 world = EntityWorldMatrix(e);
            glm::vec3 scale, translation, skew;
            glm::vec4 perspective;
            glm::quat rotation;
            glm::decompose(world, scale, rotation, translation, skew, perspective);
            rb.bodyId = m_physics->CreateCollider(collider, rb, translation,
                                                  glm::normalize(rotation), e);
            if (rb.bodyId != 0xFFFFFFFFu) ++created;
        }
        if (created > 0) {
            spdlog::info("[Physics] Created {} runtime collider bodies", created);
        }
    }

    // -------------------------------------------------------------------
    // Spawn a cube with a Jolt rigid body attached.
    // Always use this for physics cubes — it wires RigidBodyComponent,
    // motionType, mass, AND the Jolt body (CreateBox), so SyncTransformsToECS
    // will pick it up.
    // -------------------------------------------------------------------
    Galeshi::Entity SpawnPhysicsCube(const std::string& name, glm::vec3 pos,
                                     glm::vec3 scale = glm::vec3(1.0f)) {
        auto e = MakeCube(name, pos);
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.scale = scale;

        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = Galeshi::ColliderShapeType::Box;
        collider.halfExtents = scale * 0.5f;

        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateBox(scale * 0.5f, pos, static_cast<entt::entity>(e));

        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }

    Galeshi::Entity SpawnPhysicsSphere(const std::string& name, glm::vec3 pos, float radius) {
        auto e = m_scene.CreateEntity(name);
        auto mat = std::make_shared<Galeshi::Material>();
        mat->albedoFactor = {0.7f, 0.5f, 0.5f, 1.0f};
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{mat});
        auto mesh = m_assets->CreateSphereMesh();
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "sphere" });
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = pos;
        tf.scale = glm::vec3(radius * 2.0f);  // unit sphere (r=0.5) * scale = diameter
        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = Galeshi::ColliderShapeType::Sphere;
        collider.radius = radius;
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateSphere(radius, pos, static_cast<entt::entity>(e), false);
        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }

    Galeshi::Entity SpawnPhysicsCapsule(const std::string& name, glm::vec3 pos, float halfHeight, float radius) {
        auto e = m_scene.CreateEntity(name);
        auto mat = std::make_shared<Galeshi::Material>();
        mat->albedoFactor = {0.5f, 0.7f, 0.5f, 1.0f};
        e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{mat});
        auto mesh = m_assets->CreateCapsuleMesh(halfHeight, radius);
        e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ mesh, "", "capsule" });
        auto& tf = e.GetComponent<Galeshi::TransformComponent>();
        tf.position = pos;
        tf.scale = glm::vec3(1.0f);  // mesh baked with correct size
        auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
        collider.shapeType = Galeshi::ColliderShapeType::Capsule;
        collider.halfHeight = halfHeight;
        collider.radius = radius;
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.lockRotation = true;
        rb.bodyId = m_physics->CreateCollider(collider, rb, pos,
                                              glm::quat(1.0f, 0.0f, 0.0f, 0.0f),
                                              static_cast<entt::entity>(e));
        spdlog::info("[Physics] Spawned {} bodyId=0x{:08x}", name, rb.bodyId);
        return e;
    }

    void BuildTestScene() {
        auto sun = m_scene.CreateEntity("Sun");
        auto& dl = sun.HasComponent<Galeshi::DirectionalLightComponent>()
                   ? sun.GetComponent<Galeshi::DirectionalLightComponent>()
                   : sun.AddComponent<Galeshi::DirectionalLightComponent>();
        dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
        dl.color = {1.0f, 0.97f, 0.9f}; dl.intensity = 3.0f;
        // Large ground plane to receive shadows.
        auto ground = MakeCube("Ground", {0.0f, -5.0f, 0.0f});
        ground.GetComponent<Galeshi::TransformComponent>().scale = { 100.0f, 10.0f, 100.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->albedoFactor = { 0.3f, 0.3f, 0.3f, 1.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->roughnessFactor = 0.8f;
        {
            auto& rb = ground.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
            rb.bodyId = m_physics->CreateBox(glm::vec3(100.0f, 10.0f, 100.0f) * 0.5f, ground.GetComponent<Galeshi::TransformComponent>().position, static_cast<entt::entity>(ground), true);
            auto& collider = ground.AddComponent<Galeshi::ColliderComponent>();
            collider.shapeType = Galeshi::ColliderShapeType::Box;
            collider.halfExtents = glm::vec3(100.0f, 10.0f, 100.0f) * 0.5f;
        }
        // --- PBR material test grid ---
        // Front row: albedo comparison. Next three rows: metallic sweep
        // (0.0 / 0.5 / 1.0) against roughness sweep (0.05 / 0.35 / 0.80).
        // Every sphere has its own base colour; the material values remain
        // visible in the entity name and the Details panel.
        m_editor.camTarget = {0.0f, 0.8f, 0.0f};
        m_editor.camYaw = 0.30f;
        m_editor.camPitch = 0.55f;
        m_editor.camRadius = 8.0f;

        const float sphereScale = 1.2f;
        const float sphereY = sphereScale * 0.5f;
        const float xSpacing = 2.8f;
        const float zSpacing = 1.8f;
        const float albedoRowZ = 2.7f;
        const float metallicRowZ = 0.9f;

        const float metallicValues[3] = {0.0f, 0.5f, 1.0f};
        const float roughnessValues[3] = {0.05f, 0.35f, 0.8f};
        const char* metallicLabels[3] = {"M00", "M50", "M100"};
        const char* roughnessLabels[3] = {"R05", "R35", "R80"};
        const char* albedoLabels[3] = {"Red", "Green", "Blue"};
        const glm::vec3 albedoTestColors[3] = {
            {0.85f, 0.18f, 0.14f},
            {0.18f, 0.65f, 0.25f},
            {0.15f, 0.35f, 0.85f},
        };
        const glm::vec3 metallicColors[3][3] = {
            {{0.92f, 0.48f, 0.12f}, {0.88f, 0.78f, 0.18f}, {0.10f, 0.62f, 0.58f}},
            {{0.78f, 0.24f, 0.70f}, {0.16f, 0.68f, 0.86f}, {0.56f, 0.82f, 0.22f}},
            {{1.00f, 0.77f, 0.34f}, {0.95f, 0.64f, 0.54f}, {0.82f, 0.84f, 0.88f}},
        };

        auto makePbrSphere = [this, sphereScale](const std::string& name, glm::vec3 pos,
                                                 glm::vec3 albedo, float metallic, float roughness) {
            auto e = MakeSphere(name, pos);
            e.GetComponent<Galeshi::TransformComponent>().scale = glm::vec3(sphereScale);
            auto& mat = *e.GetComponent<Galeshi::MaterialComponent>().material;
            mat.name = name + "_Material";
            mat.albedoFactor = glm::vec4(albedo, 1.0f);
            mat.metallicFactor = metallic;
            mat.roughnessFactor = roughness;
            return e;
        };

        for (int col = 0; col < 3; ++col) {
            const float x = (static_cast<float>(col) - 1.0f) * xSpacing;
            const std::string name = std::string("PBR_Albedo_") + albedoLabels[col];
            makePbrSphere(name, {x, sphereY, albedoRowZ}, albedoTestColors[col], 0.0f, 0.35f);
        }

        for (int row = 0; row < 3; ++row) {
            for (int col = 0; col < 3; ++col) {
                const float x = (static_cast<float>(col) - 1.0f) * xSpacing;
                const float z = metallicRowZ - static_cast<float>(row) * zSpacing;
                const std::string name = std::string("PBR_") + metallicLabels[row] + "_" + roughnessLabels[col];
                makePbrSphere(name, {x, sphereY, z}, metallicColors[row][col],
                              metallicValues[row], roughnessValues[col]);
            }
        }

        // Keep a small dynamic physics demo off to the side of the PBR grid.
        auto spawnDynamicCube = [this](const std::string& name, glm::vec3 pos, glm::vec3 color,
                                       float metallic = 0.0f, float roughness = 0.5f) {
            auto e = MakeCube(name, pos);
            auto& mat = *e.GetComponent<Galeshi::MaterialComponent>().material;
            mat.name = name + "_Material";
            mat.albedoFactor = glm::vec4(color, 1.0f);
            mat.metallicFactor = metallic;
            mat.roughnessFactor = roughness;
            auto& tf = e.GetComponent<Galeshi::TransformComponent>();
            auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
            rb.mass = 1.0f;
            rb.bodyId = m_physics->CreateBox(tf.scale * 0.5f, pos, static_cast<entt::entity>(e), false);
            auto& collider = e.AddComponent<Galeshi::ColliderComponent>();
            collider.shapeType = Galeshi::ColliderShapeType::Box;
            collider.halfExtents = tf.scale * 0.5f;
            return e;
        };
        spawnDynamicCube("Physics_Orange", { 6.0f, 3.0f, -1.0f}, {0.85f, 0.55f, 0.25f}, 0.0f, 0.6f);
        spawnDynamicCube("Physics_White",  { 6.0f, 4.2f, -1.0f}, {0.95f, 0.95f, 0.95f}, 1.0f, 0.15f);
        spawnDynamicCube("Physics_Blue",   { 6.0f, 5.4f, -1.0f}, {0.30f, 0.55f, 0.85f}, 0.0f, 0.8f);


        auto pl = m_scene.CreateEntity("PointLight");
        if (!pl.HasComponent<Galeshi::PointLightComponent>())
            pl.AddComponent<Galeshi::PointLightComponent>();
        pl.GetComponent<Galeshi::TransformComponent>().position = {0.0f, 5.0f, 0.0f};

        // Demo fire emitter next to the material grid.
        auto emitter = m_scene.CreateEntity("FireParticles");
        emitter.AddComponent<Galeshi::ParticleEmitterComponent>();
        emitter.GetComponent<Galeshi::TransformComponent>().position = {4.5f, 0.2f, -1.5f};
    }

    // State.
    Galeshi::Scene          m_scene;
    Galeshi::Camera         m_camera;
    Galeshi::EditorState    m_editor;
    bool                    m_usingGameCamera = false;
    bool                    m_gameLooking = false;
    bool                    m_playerStrafing = false;
    int                     m_gameLookSkipFrames = 0;
    entt::entity            m_activeGameCamera = entt::null;
    entt::entity            m_gameCameraTarget = entt::null;
    glm::vec3               m_gameCameraPivotLocal{0.0f};
    glm::vec3               m_gameCameraEyeLocal{0.0f, 1.6f, 0.0f};
    glm::vec3               m_gameCameraBaseOffset{0.0f};
    glm::vec3               m_gameCameraBaseForward{0.0f, 0.0f, -1.0f};
    glm::vec3               m_gameCameraBaseUp{0.0f, 1.0f, 0.0f};
    bool                    m_gameCameraBaseValid = false;
    GaleshiScriptCameraMode m_gameCameraMode = GALESHI_CAMERA_THIRD_PERSON;
    float                   m_gameCameraYawOffset = 0.0f;
    float                   m_gameCameraPitchOffset = 0.0f;
    float                   m_time = 0.0f;
    float                   m_frameDelta = 1.0f / 60.0f;
    int                     m_renderFrameCounter = 0;
    bool                    m_pendingEffectsEnable = false;
    // When false, the viewport skips all scene, shadow, reflection and
    // post-process GPU work and keeps displaying the last completed image.
    // OnUpdate is deliberately unaffected: only render passes are paused.
    bool                    m_realtimeRendering = true;
    bgfx::TextureHandle     m_lastViewportTexture = BGFX_INVALID_HANDLE;

    std::unique_ptr<Galeshi::AssetManager>    m_assets;
    std::unique_ptr<Galeshi::RenderSystem>    m_render;
    std::unique_ptr<Galeshi::Framebuffer>     m_framebuffer;
    std::unique_ptr<Galeshi::ContentBrowser>  m_browser;
    std::unique_ptr<Galeshi::SceneSerializer> m_serializer;
    std::unique_ptr<Galeshi::EnginePhysicsSystem>   m_physics;
    std::unique_ptr<Galeshi::NavigationSystem> m_navSystem;
    std::unique_ptr<Galeshi::DebugRenderer> m_debug;
    std::unique_ptr<Galeshi::ParticleSystem> m_particles;
    bgfx::TextureHandle m_shadowMaps[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_shadowFbos[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_pointShadowFbos[6] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };

    uint16_t    m_viewportW = 0, m_viewportH = 0;
    bool        m_viewportHovered = false;
    bool        m_editorLooking = false;
    bool        m_editorLookDragging = false;
    int         m_editorLookSkipFrames = 0;
    bool        m_uiOverlayHovered = false;
    bool        m_uiItemWasActive = false;
    ImVec2      m_viewportImagePos = {0.0f, 0.0f};
    ImVec2      m_viewportImageSize = {0.0f, 0.0f};
    std::string m_currentScenePath;
    Galeshi::FilePickerDialog m_filePicker;
    Galeshi::FilePickerDialog m_filePickerSave;
    Galeshi::FilePickerDialog m_codeFilePicker;
    Galeshi::FilePickerDialog m_scenePicker;
    Galeshi::FilePickerDialog m_vegetationMeshPicker;
    entt::entity m_vegetationMeshTarget = entt::null;
    int m_vegetationMeshTypeIndex = -1;
    Galeshi::FilePickerDialog m_characterModelPicker;
    entt::entity m_characterModelTarget = entt::null;
    Galeshi::FilePickerDialog m_animationClipPicker;
    entt::entity m_animationClipTarget = entt::null;
    int m_animationClipState = -1;
    Galeshi::FilePickerDialog m_audioPicker;
    entt::entity m_audioPickerTarget = entt::null;
    int m_paintTreeType = -1;

    // Details search. The scroll flag makes the first matching section jump
    // into view only when the query changes, so the user can still scroll
    // through results after that.
    std::string m_detailsSearch;
    char m_detailsSearchBuffer[256] = {};
    bool m_detailsSearchScrollPending = false;
    TextEditor m_codeEditor;
    std::string m_currentCodeFile;
    bool m_codeEditorInitialized = false;
    bool m_codeEditorCppMode = false;
    int  m_codeEditorTabRequest = -1;
    std::string m_codeEditorSyntaxErrors;
    std::string m_codeEditorLastSyntaxSource;
    std::string m_shaderPreviewPath;
    std::string m_shaderPreviewError;
    static constexpr bgfx::ViewId m_shaderPreviewView = 60;
    bgfx::ProgramHandle m_shaderPreviewProgram = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_shaderPreviewFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_shaderPreviewTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_shaderPreviewFallbackTex = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_shaderPreviewVB = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle m_shaderPreviewIB = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_shaderPreviewSampler = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_shaderPreviewTime = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle m_shaderPreviewResolution = BGFX_INVALID_HANDLE;
    std::string m_codeEditorSyntaxPendingSource;
    std::string m_codeEditorSyntaxPendingPath;
    std::future<CodeSyntaxResult> m_codeEditorSyntaxFuture;
    bool m_codeEditorSyntaxRunning = false;
    double m_codeEditorLastEditTime = 0.0;
    bool m_codeEditorSyntaxDirty = false;
    bool m_focusCodeEditor = false;

    std::vector<std::string> m_history;
    int  m_historyIndex = -1;
    bool m_isRestoringHistory = false;
    bool m_suppressHistory = false;
    bool m_wasUsingGizmo = false;
    bool m_wasSculpting = false;
    entt::entity m_pendingLandscapeBake = entt::null;
    float m_lastSculptHeight = 0.0f;
    bool m_lastBrushHitValid = false;
    glm::vec3 m_lastBrushLocal{0.0f};
    float m_lastBrushRadius = 1.0f;
    glm::vec3 m_lastPaintLocal{0.0f};
    bool m_paintStampValid = false;
    std::mt19937 m_paintRng{0x51ed270bu};
    bool m_showAIDebug = true;

    // Profiler state and rolling frame history.
    bool m_showProfiler = false;
    bool m_profilerDebugEnabled = false;
    CpuProfileSample m_cpuProfile;
    std::vector<GpuPassSample> m_gpuPasses;
    std::array<std::string, 256> m_profilerViewNames{};
    float m_gpuFrameMs = 0.0f;
    std::array<float, kProfileHistorySize> m_frameHistory{};
    std::array<float, kProfileHistorySize> m_gameLogicHistory{};
    std::array<float, kProfileHistorySize> m_gpuHistory{};

    // Screen-space translate drag (LMB-only, no W/E/R arrows)
    bool      m_isDragging = false;
    glm::vec2 m_dragStartMouse{0.0f, 0.0f};
    glm::vec3 m_dragStartObjWorld{0.0f};
};


namespace fs = std::filesystem;

int PackageProject(const std::string& projectPath) {
    spdlog::info("[Package] Starting packaging for: {}", projectPath);

    const char* homeEnv = std::getenv("HOME");
    fs::path homeDir = homeEnv ? homeEnv : "";

    fs::path projPath = fs::absolute(projectPath);
    if (!fs::exists(projPath)) {
        projPath = fs::current_path() / fs::path(projectPath).filename();
    }
    if (!fs::exists(projPath) && !homeDir.empty()) {
        projPath = homeDir / fs::path(projectPath).filename();
    }

    if (!fs::exists(projPath)) {
        spdlog::error("[Package] Project file not found: {}", projPath.string());
        return 1;
    }

    std::ifstream f(projPath);
    if (!f.is_open()) return 1;
    nlohmann::json proj;
    try {
        f >> proj;
    } catch (const std::exception& e) {
        spdlog::error("[Package] Invalid JSON: {}", e.what());
        return 1;
    }
    
    std::string gameName = proj.value("name", "GaleshiGame");
    std::string startScene = proj.value("startScene", "scene.json");

    fs::path outDir = fs::current_path() / "build" / "package" / gameName;
    fs::create_directories(outDir);

    // 1. Copy the executable and make it runnable.
    fs::path exePath = fs::current_path() / "GaleshiEditor";
    if (!fs::exists(exePath)) exePath = fs::current_path() / "build" / "debug" / "bin" / "GaleshiEditor";
    
    if (fs::exists(exePath)) {
        fs::path destExe = outDir / (gameName + ".bin");
        fs::copy_file(exePath, destExe, fs::copy_options::overwrite_existing);
        fs::permissions(destExe, 
            fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
            fs::perm_options::replace);
        spdlog::info("[Package] Copied executable to {} (chmod +x)", destExe.filename().string());

        // C++ scripts run in this helper process. Keep it beside the game
        // binary so runtime isolation also works from packaged builds.
        const fs::path workerPath =
            exePath.parent_path() / "GaleshiScriptWorker";
        if (fs::exists(workerPath)) {
            const fs::path destWorker = outDir / "GaleshiScriptWorker";
            fs::copy_file(workerPath, destWorker,
                          fs::copy_options::overwrite_existing);
            fs::permissions(destWorker,
                fs::perms::owner_all | fs::perms::group_all |
                    fs::perms::others_all,
                fs::perm_options::replace);
        } else {
            spdlog::warn("[Package] C++ script worker not found at {}",
                         workerPath.string());
        }
    } else {
        spdlog::error("[Package] Executable not found at {}", exePath.string());
        return 1;
    }

    // 2. Copy assets and shaders.
    fs::path projectRoot = projPath.parent_path();
    auto copyDir = [&](const std::string& dirName) {
        fs::path srcDir = projectRoot / dirName;
        if (!fs::exists(srcDir)) srcDir = fs::current_path() / dirName;
        if (fs::exists(srcDir)) {
            fs::copy(srcDir, outDir / dirName, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            spdlog::info("[Package] Copied directory: {}", dirName);
        } else {
            spdlog::warn("[Package] Directory not found: {}", srcDir.string());
        }
    };
    copyDir("assets");
    
    // Copy compiled shaders.
    fs::path compiledShaders = fs::current_path() / "build" / "debug" / "bin" / "shaders";
    if (!fs::exists(compiledShaders)) {
        // When running from build/debug/bin, shaders are in the current directory.
        compiledShaders = fs::current_path() / "shaders";
    }
    if (fs::exists(compiledShaders)) {
        fs::copy(compiledShaders, outDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        spdlog::info("[Package] Copied compiled shaders from {}", compiledShaders.string());
    } else {
        spdlog::error("[Package] Compiled shaders not found at {}", compiledShaders.string());
    }

    // 3. Copy the start scene.
    fs::path scenePath = "";
    if (!homeDir.empty() && fs::exists(homeDir / startScene)) {
        scenePath = homeDir / startScene;
    } else if (fs::exists(projectRoot / startScene)) {
        scenePath = projectRoot / startScene;
    }

    if (!scenePath.empty()) {
        fs::copy_file(scenePath, outDir / startScene, fs::copy_options::overwrite_existing);
        spdlog::info("[Package] Copied start scene from {}", scenePath.string());
    } else {
        spdlog::error("[Package] Could not find start scene {} anywhere!", startScene);
    }

    // 4. Save .gproj.
    proj["startScene"] = startScene;
    std::ofstream outProj(outDir / (gameName + ".gproj"));
    outProj << proj.dump(4);
    outProj.close();

    // 5. Create run.sh.
    std::ofstream runScript(outDir / "run.sh");
    runScript << "#!/bin/bash\n";
    runScript << "SCRIPT_DIR=\"$( cd \"$( dirname \"${BASH_SOURCE[0]}\" )\" &> /dev/null && pwd )\"\n";
    runScript << "cd \"$SCRIPT_DIR\"\n";
    runScript << "./" << gameName << ".bin --game " << gameName << ".gproj\n";
    runScript.close();
    fs::permissions(outDir / "run.sh",
        fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
        fs::perm_options::replace);

    spdlog::info("[Package] Packaging complete! Output: {}", outDir.string());
    return 0;
}

int main(int argc, char** argv) {
    // Install the editor log capture before constructing the application so
    // startup diagnostics are available in the Output Log tab.
    auto editorLogSink = std::make_shared<EditorLogSink>();
    g_editorLogSink = editorLogSink;
    if (auto logger = spdlog::default_logger()) {
        logger->sinks().push_back(editorLogSink);
    }

    std::string packagePath = "";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--package" && i + 1 < argc) {
            packagePath = argv[i + 1];
        }
    }

    if (!packagePath.empty()) {
        // Unbuffered output so package errors are not lost.
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        return PackageProject(packagePath);
    }


    bool debugMode = false;
    bool gameMode = false;
    std::string gameProjectPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") debugMode = true;
        if (arg == "--game") {
            gameMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                gameProjectPath = argv[++i];
            }
        }
    }
    if (debugMode) {
        spdlog::set_level(spdlog::level::debug);
    }
    // Unbuffered stdout so logs survive crashes.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    GaleshiEditorApp* app =
        new GaleshiEditorApp(debugMode, gameMode, gameProjectPath);
    int exitCode = app->Run();

    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(exitCode);
}
