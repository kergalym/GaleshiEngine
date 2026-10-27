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
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
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

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <spdlog/spdlog.h>
#include <ImGuizmo.h>
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/matrix_decompose.hpp>
#include <bx/math.h>

#include <algorithm>
#include <functional>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstdio>
#include <filesystem>
#include <limits>
#include <memory>
#include <numeric>
#include <vector>
#include <fstream>
#include <nlohmann/json.hpp>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>

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

struct EditorPreferences {
    float cameraOrbitSensitivity = 0.01f;
    float cameraPanSpeed = 10.0f;
    float cameraZoomSpeed = 0.5f;
    float cameraFovDegrees = 60.0f;
};

class GaleshiEditorApp final : public Galeshi::Application {
    Galeshi::AudioSystem m_audio;
    Galeshi::ScriptEngine m_scripts;
    Galeshi::PostProcessManager m_postProcess;
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
    int m_selectedAxis = 0;
    std::vector<std::string> m_recentProjects;
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
              .width = 1600, .height = 900, 
              .editor = true, // ImGui всегда включен
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
            Galeshi::Texture::LoadFromFile("assets/icons/galeshi_logo_round.png"));
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
        m_uiCanvasEntity = entt::null;
        m_uiWidgetIndex = -1;
        spdlog::info("[Game] Loaded start scene {}", m_currentScenePath);
    }

    void OnFileDrop(const std::string& filepath) override {
        if (!m_projectLoaded || !m_assets) return;
        namespace fs = std::filesystem;
        std::string ext = fs::path(filepath).extension().string();
        for (auto& c : ext) c = std::tolower(c);
        if (ext == ".fbx" || ext == ".gltf" || ext == ".glb" || ext == ".png" || ext == ".jpg" || ext == ".wav" || ext == ".mp3") {
            fs::path dest = fs::path(Galeshi::ProjectManager::GetProjectPath()) / "assets" / fs::path(filepath).filename();
            fs::copy_file(filepath, dest, fs::copy_options::overwrite_existing);
            if (m_browser) m_browser->Refresh();
            spdlog::info("[Editor] Imported asset: {}", dest.string());
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
                if (j.is_array()) m_recentProjects = j.get<std::vector<std::string>>();
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

    void AddRecentProject(const std::string& path) {
        auto it = std::find(m_recentProjects.begin(), m_recentProjects.end(), path);
        if (it != m_recentProjects.end()) m_recentProjects.erase(it);
        m_recentProjects.insert(m_recentProjects.begin(), path);
        if (m_recentProjects.size() > 10) m_recentProjects.resize(10);
        SaveRecentProjects();
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
                m_preferences.cameraPanSpeed =
                    viewport.value("pan_speed", m_preferences.cameraPanSpeed);
                m_preferences.cameraZoomSpeed =
                    viewport.value("zoom_speed", m_preferences.cameraZoomSpeed);
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
        m_preferences.cameraPanSpeed =
            std::clamp(m_preferences.cameraPanSpeed, 1.0f, 50.0f);
        m_preferences.cameraZoomSpeed =
            std::clamp(m_preferences.cameraZoomSpeed, 0.05f, 3.0f);
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
            {"pan_speed", m_preferences.cameraPanSpeed},
            {"zoom_speed", m_preferences.cameraZoomSpeed},
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
                        const auto& proj = m_recentProjects[i];
                        std::string filename = std::filesystem::path(proj).filename().string();
                        
                        if (ImGui::Selectable(filename.c_str(), false, ImGuiSelectableFlags_AllowDoubleClick)) {
                            if (ImGui::IsMouseDoubleClicked(0)) {
                                if (Galeshi::ProjectManager::OpenProject(proj)) {
                                    m_projectLoaded = true;
                                    AddRecentProject(proj);
                                    StartAsyncLoad(filename, [this, proj]() {
                                        InitializeEditor();
                                        if (m_loadingState.cancelRequested) return;
                                        if (m_serializer &&
                                            m_serializer->Load(proj, &m_loadingState.cancelRequested)) {
                                            m_currentScenePath = proj;
                                        }
                                    }, true);
                                }
                            }
                        }
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip("%s\nDouble-click to open.", proj.c_str());
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
                            m_currentScenePath = chosenPath;
                        }
                    }, true);
                }
            } else {
                std::strncpy(m_newProjectPath, chosenPath.c_str(), sizeof(m_newProjectPath) - 1);
            }
        }

        ImGui::End();
    }

    void InitializeEditor() {
        // --- Auto-inserted subsystem init ---
        spdlog::info("[Subsystem] m_audio.Init() begin");
        if (!m_audio.Init()) spdlog::error("[Subsystem] m_audio.Init() failed");
        else spdlog::info("[Subsystem] m_audio.Init() done");
        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        spdlog::info("[Subsystem] m_physics->Init() begin");
        if (!m_physics->Init()) spdlog::error("[Subsystem] m_physics->Init() failed");
        else spdlog::info("[Subsystem] m_physics->Init() done");
        m_scripts.BindFunctions();
        m_scripts.BindECS(m_scene);
        m_scripts.BindInput([this](const std::string& key) -> bool {
            return GetInput().IsKeyDown(key);
        });
        m_scripts.BindInputAxes(
            [this](const std::string& axis) {
                return GetInput().GetAxis(axis);
            },
            [this](const std::string& axis) {
                return GetInput().GetAxisRaw(axis);
            },
            [this]() { return GetInput().MousePosition(); },
            [this]() { return GetInput().MouseDelta(); },
            [this]() { return GetInput().MouseWheel(); },
            [this](const std::string& button) {
                return GetInput().IsMouseButtonDown(button);
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
        m_scripts.BindAudio([this](const std::string& p){ m_audio.PlaySound(p); });
        m_scripts.BindAI();
        m_scripts.BindPhysics([this](const glm::vec3& he, const glm::vec3& pos, entt::entity e) {
            return m_physics->CreateBox(he, pos, e);
        });
        spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) begin");
        if (!m_scripts.RunScript("assets/scripts/init.lua"))
            spdlog::error("[Subsystem] m_scripts.RunScript(init.lua) failed");
        else
            spdlog::info("[Subsystem] m_scripts.RunScript(init.lua) done");
        // --- End auto-inserted ---

        m_uiRuntime.SetEventHandler([this](const Galeshi::UIEvent& event) {
            DispatchUIEvent(event);
        });

        m_assets  = std::make_unique<Galeshi::AssetManager>();
        m_physics = std::make_unique<Galeshi::EnginePhysicsSystem>();
        m_physics->Init();
        m_physics->SetOnContactCallback([this](entt::entity e1, entt::entity e2) {
            // Simple demo: play beep on any collision involving an AudioSource entity
            auto& reg = m_scene.Registry();
            for (entt::entity e : { e1, e2 }) {
                if (reg.valid(e) && reg.all_of<Galeshi::AudioSourceComponent>(e)) {
                    auto& a = reg.get<Galeshi::AudioSourceComponent>(e);
                    if (!a.soundPath.empty()) {
                        m_audio.PlaySound(a.soundPath, a.loop);
                        a.playing = true;
                    }
                }
            }
        });
        m_navSystem = std::make_unique<Galeshi::NavigationSystem>();
        m_navSystem->Init();
        m_debug = std::make_unique<Galeshi::DebugRenderer>();
        m_debug->Init();
        
        m_render  = std::make_unique<Galeshi::RenderSystem>();
        m_render->Init(m_assets.get());
        m_postProcess.Init();
        m_render->SetDeferredEnabled(true);
        // SSAO/SSR включим на 2-м кадре — чтобы первый кадр прошёл
        // без sampling render target (иначе Vulkan layout mismatch).
        m_pendingEffectsEnable = true;
        for (int i = 0; i < 3; ++i) {
            m_shadowMaps[i] = bgfx::createTexture2D(2048, 2048, false, 1, bgfx::TextureFormat::D32F, BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP);
            m_shadowFbos[i] = bgfx::createFrameBuffer(1, &m_shadowMaps[i], false);
        }
        


        m_browser    = std::make_unique<Galeshi::ContentBrowser>("assets");
        m_serializer = std::make_unique<Galeshi::SceneSerializer>(m_scene, *m_assets);

        // The editor deliberately starts with an empty scene.  Scenes are
        // loaded explicitly through File -> Load Scene.
        spdlog::info("[Editor] Starting with an empty scene");

        // Первый снапшот для Undo/Redo
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

        spdlog::trace("[Main] OnUpdate");
        // --- Deferred effect enable (frame 2+) ---
        m_renderFrameCounter++;
        if (m_pendingEffectsEnable && m_renderFrameCounter >= 2) {
            // Temporary debug hook: GALESHI_EFFECTS is a bitmask used to
            // bisect Vulkan layout problems. Without it every effect is on.
            int effectMask = 0x3FF;
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
        if (m_physics) {
            m_physics->Update(m_scene, dt);
            m_physics->SyncTransformsToECS(m_scene);
        }

        // Audio: auto-play & state sync
        {
            auto view = m_scene.Registry().view<Galeshi::AudioSourceComponent>();
            for (auto e : view) {
                auto& a = view.get<Galeshi::AudioSourceComponent>(e);
                bool playingNow = !a.soundPath.empty() && m_audio.IsSoundPlaying(a.soundPath);
                if (a.playing != playingNow) a.playing = playingNow;
                if (a.autoplay && !a.playing && !a.soundPath.empty()) {
                    m_audio.PlaySound(a.soundPath, a.loop);
                    a.playing = true;
                }
            }
        }
        // --- End auto-inserted ---

        m_time += dt;
        m_frameDelta = dt;
        m_scripts.RunAIScripts(m_scene, dt);
        Galeshi::AISystem::Update(m_scene, dt, m_navSystem.get());
        m_scripts.Update(dt);

        // Skeletal playback and animation state machines. This samples the
        // active clip and blends the previous state when a transition fires.
        Galeshi::AnimationSystem::Update(m_scene, *m_assets, dt);

        // Логика камеры
        const bool cameraControllable = m_viewportHovered && !m_editor.GizmoLocked() && !ImGuizmo::IsUsing() && !ImGuizmo::IsOver();
        
        // Зум
        if (m_viewportHovered) {
            float wheel = ImGui::GetIO().MouseWheel;
            if (wheel != 0.0f) {
                m_editor.camRadius -= wheel * m_preferences.cameraZoomSpeed;
                m_editor.camRadius = glm::clamp(m_editor.camRadius, 0.5f, 200.0f);
            }
        }

        // Вращение ПКМ
        if (cameraControllable && ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
            auto d = ImGui::GetIO().MouseDelta;
            m_editor.camYaw   -= d.x * m_preferences.cameraOrbitSensitivity;
            m_editor.camPitch -= d.y * m_preferences.cameraOrbitSensitivity;
            m_editor.camPitch  = glm::clamp(m_editor.camPitch, -1.5f, 1.5f);
        }

        // Перемещение стрелками
        if (cameraControllable && !ImGui::GetIO().WantTextInput) {
            float panSpeed = m_preferences.cameraPanSpeed * dt;
            float yaw = m_editor.camYaw;
            glm::vec3 forward(std::sin(yaw), 0.0f, std::cos(yaw));
            glm::vec3 right(forward.z, 0.0f, -forward.x);

            if (ImGui::IsKeyDown(ImGuiKey_UpArrow))    m_editor.camTarget -= forward * panSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_DownArrow))  m_editor.camTarget += forward * panSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_LeftArrow))  m_editor.camTarget -= right * panSpeed;
            if (ImGui::IsKeyDown(ImGuiKey_RightArrow)) m_editor.camTarget += right * panSpeed;
            
        }

        const float aspect = m_viewportH ? static_cast<float>(m_viewportW) / m_viewportH : 16.0f / 9.0f;
        m_camera.SetPerspective(glm::radians(m_preferences.cameraFovDegrees), aspect, 0.1f, 1000.0f);
        m_camera.Orbit(m_editor.camTarget, m_editor.camYaw, m_editor.camPitch, m_editor.camRadius);
    }

    void OnRender() override {} // Всё во Viewport

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
        DrawMainMenuBar();

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
                spdlog::info("[Lua] Hot-reload init.lua");
                m_scripts.HotReload("assets/scripts/init.lua");
            }
            // Space is now used by Lua (Jump)

            if (ImGui::GetIO().KeyCtrl) {
                if (ImGui::IsKeyPressed(ImGuiKey_I)) OpenFileDialog();
                if (ImGui::IsKeyPressed(ImGuiKey_S)) SaveScene();
                if (ImGui::IsKeyPressed(ImGuiKey_O)) LoadScene();
                if (ImGui::IsKeyPressed(ImGuiKey_Z)) Undo();
                if (ImGui::IsKeyPressed(ImGuiKey_Y)) Redo();
            }
        }

        DrawDockspace();
        DrawViewport();
        DrawOutliner();
        DrawDetails();
        DrawContentBrowser();
        DrawLuaEditor();
        DrawLuaConsole();
        DrawScriptComponents();
        DrawUIDesigner();
        DrawPreferencesWindow();

        // Play mode draws the authored canvases inside the viewport image.
        if (m_editor.isPlaying && !m_uiPreview) {
            m_uiRuntime.RenderInRect(m_scene, m_viewportImagePos.x,
                                     m_viewportImagePos.y, m_viewportImageSize.x,
                                     m_viewportImageSize.y);
        }

        // File Picker (модальный)
        
        // --- File pickers (модальные диалоги) ---
        std::string chosenOpen = m_filePicker.Draw();
        if (!chosenOpen.empty()) {
            SpawnFromModel(chosenOpen, m_editor.camTarget);
        }

        std::string chosenSave = m_filePickerSave.Draw();
        if (!chosenSave.empty()) {
            if (m_serializer) {
                if (m_serializer->Save(chosenSave)) {
                    m_currentScenePath = std::filesystem::absolute(chosenSave).lexically_normal().string();
                    spdlog::info("[Editor] Scene saved to {}", m_currentScenePath);
                } else {
                    spdlog::error("[Editor] Failed to save scene to {}", chosenSave);
                }
            }
        }

        // Load Scene dialog (.json)
        std::string chosenLoad = m_scenePicker.Draw();
        if (!chosenLoad.empty()) {
            m_editor.ClearSelection();
            StartAsyncLoad(chosenLoad);
        }
    }

    void OnShutdown() override {
        spdlog::info("[Editor] OnShutdown: begin");

        // Never let the loader touch bgfx/SDL after they have been shut down.
        if (m_loadingState.loaderThread.joinable()) {
            m_loadingState.cancelRequested = true;
            m_loadingState.loaderThread.join();
        }

        // Автосохранение ОТКЛЮЧЕНО. Сцена сохраняется только вручную
        // через File -> Save Scene (Ctrl+S).

        SavePreferences();

        // Выходим без очистки GPU (иначе NVIDIA deadlock)
        SDL_Quit();
        bgfx::shutdown();
        spdlog::info("[Editor] OnShutdown: exit (skip GPU cleanup)");
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
        // from a clean state.
        if (s.loaderThread.joinable() && !s.workerRunning) {
            s.loaderThread.join();
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

    // The scene must release its shared mesh/material/texture references
    // before the AssetManager cache is cleared; only then are the bgfx buffers
    // and textures actually destroyed.
    void UnloadSceneContent() {
        if (!m_assets) return;

        m_editor.ClearSelection();
        m_editor.isPlaying = false;

        if (m_physics) m_physics->Reset();
        if (m_navSystem) {
            m_navSystem->Shutdown();
            m_navSystem->Init();
        }

        m_scene.Registry().clear();
        m_assets->Clear();

        m_history.clear();
        m_historyIndex = -1;
        m_currentScenePath.clear();
        if (m_serializer) PushHistory(); // empty-scene snapshot for Undo/Redo
    }

    void DrawLoadingModal() {
        auto& s = m_loadingState;
        if (!s.isLoading) return;

        ImGuiViewport* vp = ImGui::GetMainViewport();

        // Затемнение фона
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

        // Прогресс-бар (Цвет кнопки Cancel)
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::ProgressBar(shown, ImVec2(-1, 20), overlay);
        ImGui::PopStyleColor();
        ImGui::Spacing();

        // Под-статус
        const bool cancelling = s.cancelRequested.load();
        ImGui::TextDisabled("%s", cancelling ? "Cancelling..." : LoadingStageText(s.stage.load()));
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();

        // Кнопка Cancel по центру
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
            ImGui::SliderFloat("Pan speed", &m_preferences.cameraPanSpeed,
                               1.0f, 50.0f, "%.1f");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::SliderFloat("Zoom speed", &m_preferences.cameraZoomSpeed,
                               0.05f, 3.0f, "%.2f");
            ImGui::SetNextItemWidth(260.0f);
            ImGui::SliderFloat("Field of view", &m_preferences.cameraFovDegrees,
                               30.0f, 110.0f, "%.0f deg");

            ImGui::Spacing();
            if (ImGui::Button("Restore Viewport Defaults",
                              ImVec2(190.0f, 0.0f))) {
                m_preferences = EditorPreferences{};
            }
            ImGui::TextDisabled("Right mouse drag orbits; arrow keys pan.");
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
            }
            ImGui::SameLine();
            ImGui::TextDisabled("Defaults: Horizontal, Vertical, Jump, Mouse X/Y, Wheel");

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
            ImGui::TextDisabled("Version 0.2.0 - pre-alpha");
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
            if (!m_editor.isPlaying) {
                PushHistory();
                m_editor.isPlaying = true;
            } else {
                m_editor.isPlaying = false;
                m_physics->Reset();
                if (m_historyIndex >= 0) m_serializer->LoadFromString(m_history[m_historyIndex]);
            }
        }
        ImGui::SameLine();
        ImGui::Text("|");
        ImGui::SameLine();

                if (ImGui::BeginMenu("Post-Process")) {
            bool bloom = m_postProcess.BloomEnabled();
            if (ImGui::MenuItem("Bloom", nullptr, &bloom)) m_postProcess.SetBloomEnabled(bloom);
            bool inv = m_postProcess.InvertEnabled();
            if (ImGui::MenuItem("Invert Colors", nullptr, &inv)) m_postProcess.SetInvertEnabled(inv);
            bool fxaa = m_postProcess.FxaaEnabled();
            if (ImGui::MenuItem("FXAA", nullptr, &fxaa)) m_postProcess.SetFxaaEnabled(fxaa);
            bool ssao = m_postProcess.SsaoEnabled();
            if (ImGui::MenuItem("SSAO", nullptr, &ssao)) m_postProcess.SetSsaoEnabled(ssao);
            bool smaa = m_postProcess.SmaaEnabled();
            if (ImGui::MenuItem("SMAA", nullptr, &smaa)) m_postProcess.SetSmaaEnabled(smaa);
            bool taa = m_postProcess.TaaEnabled();
            if (ImGui::MenuItem("TAA", nullptr, &taa)) m_postProcess.SetTaaEnabled(taa);

            bool dof = m_postProcess.DofEnabled();
            if (ImGui::MenuItem("Depth of Field", nullptr, &dof)) m_postProcess.SetDofEnabled(dof);

            bool mb = m_postProcess.MotionBlurEnabled();
            if (ImGui::MenuItem("Motion Blur", nullptr, &mb)) m_postProcess.SetMotionBlurEnabled(mb);

            bool atmo = m_postProcess.AtmosphereEnabled();
            if (ImGui::MenuItem("Atmosphere", nullptr, &atmo)) m_postProcess.SetAtmosphereEnabled(atmo);

            bool gr = m_postProcess.GodRaysEnabled();
            if (ImGui::MenuItem("Volumetric Light", nullptr, &gr)) m_postProcess.SetGodRaysEnabled(gr);
            bool ssr = m_postProcess.SsrEnabled();
            if (ImGui::MenuItem("SSR", nullptr, &ssr)) m_postProcess.SetSsrEnabled(ssr);
            ImGui::Separator();
            if (ImGui::MenuItem("Reset All Effects")) {
                m_postProcess.SetInvertEnabled(false);
                m_postProcess.SetFxaaEnabled(false);
                m_postProcess.SetSsaoEnabled(false); // DEBUG
                m_postProcess.SetSsrEnabled(false); // DEBUG
            }
            ImGui::EndMenu();
        }
if (ImGui::BeginMenu("File")) {
            if (ImGui::MenuItem("Import Model...", "Ctrl+I")) OpenFileDialog();
            if (ImGui::MenuItem("Load Scene", "Ctrl+O")) LoadScene();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) SaveScene();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) RequestExit();
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Edit")) {
            if (ImGui::MenuItem("Undo", "Ctrl+Z")) Undo();
            if (ImGui::MenuItem("Redo", "Ctrl+Y")) Redo();
            ImGui::Separator();
            if (ImGui::MenuItem("Translate", "W")) m_editor.SetMode(Galeshi::GizmoMode::Translate);
            if (ImGui::MenuItem("Rotate",    "E")) m_editor.SetMode(Galeshi::GizmoMode::Rotate);
            if (ImGui::MenuItem("Scale",     "R")) m_editor.SetMode(Galeshi::GizmoMode::Scale);
            ImGui::Separator();
            if (ImGui::MenuItem("Preferences...", "Ctrl+,")) m_showPreferences = true;
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Empty Entity"))
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
            
            if (ImGui::MenuItem("Landscape")) {
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
            if (ImGui::MenuItem("Cube")) {
                auto e = MakeCube("Cube", {0, 0, 0});
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Physics Sphere")) {
                auto e = SpawnPhysicsSphere("PhysicsSphere", {0, 5, 0}, 0.5f);
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Physics Capsule")) {
                auto e = SpawnPhysicsCapsule("PhysicsCapsule", {0, 5, 0}, 0.5f, 0.5f);
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Cloth (SoftBody)")) {
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
            if (ImGui::MenuItem("Physics Cube")) {
                auto e = SpawnPhysicsCube("PhysicsCube", {0, 5, 0});
                e.AddComponent<Galeshi::BoxColliderComponent>();
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("AI Agent (Behavior Tree)")) {
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
            if (ImGui::MenuItem("AI Agent (Lua Patrol)")) {
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
                if (ImGui::MenuItem("Directional Light")) {
                auto e = m_scene.CreateEntity("DirectionalLight");
                auto& dl = e.HasComponent<Galeshi::DirectionalLightComponent>()
                           ? e.GetComponent<Galeshi::DirectionalLightComponent>()
                           : e.AddComponent<Galeshi::DirectionalLightComponent>();
                dl.direction = glm::normalize(glm::vec3(-0.4f, -0.8f, -0.5f));
                dl.intensity = 3.0f;
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Point Light")) {
                auto e = m_scene.CreateEntity("PointLight");
                auto& pl = e.HasComponent<Galeshi::PointLightComponent>()
                           ? e.GetComponent<Galeshi::PointLightComponent>()
                           : e.AddComponent<Galeshi::PointLightComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 2, 0};
                m_editor.Select(e);
            }
            if (ImGui::MenuItem("Spot Light")) {
                auto e = m_scene.CreateEntity("SpotLight");
                auto& sl = e.HasComponent<Galeshi::SpotLightComponent>()
                           ? e.GetComponent<Galeshi::SpotLightComponent>()
                           : e.AddComponent<Galeshi::SpotLightComponent>();
                auto& tc = e.GetComponent<Galeshi::TransformComponent>();
                tc.position = {0, 3, 0};
                m_editor.Select(e);
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Testing")) {
            if (ImGui::MenuItem("Reset Layout")) m_resetLayout = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Build Scene")) {
                m_editor.ClearSelection();
                m_editor.isPlaying = false;
                m_physics->Reset();
                m_scene.Registry().clear();
                m_assets->Clear();
                StartAsyncLoad("Built Scene", [this]() {
                    BuildTestScene();
                    PushHistory();
                });
            }
            ImGui::EndMenu();
        }
        
        if (ImGui::BeginMenu("Stats")) {
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::Text("Entities: %d", (int)m_scene.AllEntities().size());
            ImGui::Separator();
            ImGui::MenuItem("AI Perception Debug", nullptr, &m_showAIDebug);
            ImGui::EndMenu();
        }

        ImGui::EndMainMenuBar();
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
        if (m_historyIndex <= 0) return;
        m_historyIndex--;
        m_isRestoringHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        m_isRestoringHistory = false;
    }

    void Redo() {
        if (m_historyIndex + 1 >= (int)m_history.size()) return;
        m_historyIndex++;
        m_isRestoringHistory = true;
        m_editor.ClearSelection();
        m_serializer->LoadFromString(m_history[m_historyIndex]);
        m_isRestoringHistory = false;
    }

    // -------------------------------------------------------------------
    // Save / Load / Delete
    // -------------------------------------------------------------------
    void SaveScene() {
        const std::string start = SceneStartDirectory();
        const std::string fileName = m_currentScenePath.empty()
            ? "scene.json"
            : std::filesystem::path(m_currentScenePath).filename().string();
        m_filePickerSave.SaveAs(start, fileName, "Save Scene", { ".json" });
    }
    
    void LoadScene() {
        // Load Scene -> модальный диалог для .json файлов
        m_scenePicker.Open(SceneStartDirectory(), "Load Scene", { ".json" });
    }

    void DeleteSelected() {
        if (!m_editor.HasSelection()) return;
        PushHistory();
        auto entities = m_editor.SelectedAll(&m_scene);
        for (auto e : entities) m_scene.DestroyEntity(e);
        m_editor.ClearSelection();
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

        sol::state& lua = m_scripts.GetState();
        sol::object functionObject = lua[handler];
        if (!functionObject.valid() ||
            functionObject.get_type() != sol::type::function) {
            spdlog::warn("[UI] Lua handler '{}' is not defined", handler);
            return;
        }

        sol::protected_function function = functionObject;
        sol::protected_function_result result =
            function(event.widgetId, event.value, event.checked);
        if (!result.valid()) {
            sol::error error = result;
            spdlog::error("[UI] Lua error in '{}': {}", handler, error.what());
        }
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

        // All templates author widgets in the 1920x1080 reference space.  The
        // runtime and the editor viewport scale the complete canvas to the
        // destination rectangle, exactly like a Unity/Unreal UI canvas.
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
        if (ImGui::Button("Open Script")) ImGui::SetWindowFocus("Lua Editor");
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

        ImGui::Checkbox("Override Color", &widget.useColor);
        if (!widget.useColor) ImGui::BeginDisabled();
        ImGui::ColorEdit4("Color RGBA", glm::value_ptr(widget.color),
                          ImGuiColorEditFlags_Uint8 |
                              ImGuiColorEditFlags_AlphaBar);
        if (!widget.useColor) ImGui::EndDisabled();
        ImGui::SliderFloat("Opacity", &widget.opacity, 0.0f, 1.0f, "%.2f");
        ImGui::DragFloat("Radius", &widget.radius, 0.5f, 0.0f, 64.0f);
        ImGui::DragFloat("Border", &widget.border, 0.1f, 0.0f, 16.0f);

        ImGui::Checkbox("Visible##widget", &widget.visible);
        ImGui::SameLine();
        ImGui::Checkbox("Interactable", &widget.interactable);
        ImGui::SameLine();
        ImGui::Checkbox("Block Input", &widget.blockInput);

        switch (widget.type) {
        case Galeshi::UIWidgetType::Button:
            ImGui::DragFloat("Height", &widget.buttonHeight, 0.5f, 0.0f, 400.0f);
            break;
        case Galeshi::UIWidgetType::Slider:
        case Galeshi::UIWidgetType::Progress: {
            ImGui::DragFloatRange2("Range", &widget.sliderMin, &widget.sliderMax,
                                   0.01f, -10000.0f, 10000.0f);
            ImGui::SliderFloat("Value", &widget.sliderValue, widget.sliderMin,
                               widget.sliderMax);
            break;
        }
        case Galeshi::UIWidgetType::CheckBox:
        case Galeshi::UIWidgetType::Toggle:
            ImGui::Checkbox("Checked", &widget.checked);
            break;
        case Galeshi::UIWidgetType::Input:
            ImGui::DragInt("Max Chars", &widget.maxChars, 1.0f, 1, 4096);
            break;
        case Galeshi::UIWidgetType::Image:
            UIEventInput("Image Path", widget.imagePath);
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
        if (ImGui::Button("Open Script")) ImGui::SetWindowFocus("Lua Editor");
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
        auto scaledTextSize = [](const char* text, float fontSize) {
            return ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
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

            switch (widget.type) {
            case Galeshi::UIWidgetType::Panel:
                draw->AddRectFilled(wmin, wmax,
                                    widget.useColor ? toU32(widget.color)
                                                    : IM_COL32(18, 30, 48, 222),
                                    widget.radius * scale);
                if (widget.border > 0.0f) {
                    draw->AddRect(wmin, wmax, IM_COL32(90, 140, 200, 160),
                                  widget.radius * scale, 0,
                                  std::max(1.0f, widget.border * scale * 0.5f));
                }
                break;
            case Galeshi::UIWidgetType::Button: {
                draw->AddRectFilled(wmin, wmax, accent, widget.radius * scale);
                const ImVec2 textSize = ImGui::CalcTextSize(widget.label.c_str());
                draw->AddText(
                    ImVec2(wmin.x + (wmax.x - wmin.x - textSize.x) * 0.5f,
                           wmin.y + (wmax.y - wmin.y - textSize.y) * 0.5f),
                    IM_COL32(255, 255, 255, 255), widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::Text: {
                const float fontSize =
                    std::clamp(size.y * scale * 0.62f, 9.0f, 30.0f);
                draw->AddText(ImGui::GetFont(), fontSize, wmin, kText,
                              widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::Image: {
                draw->AddRectFilled(wmin, wmax, IM_COL32(30, 30, 30, 210),
                                    widget.radius * scale);
                draw->AddRect(wmin, wmax, kBorder, widget.radius * scale);
                const char* caption =
                    !widget.imagePath.empty()
                        ? widget.imagePath.c_str()
                        : (!widget.label.empty() ? widget.label.c_str()
                                                 : "[Image]");
                const float fontSize = std::clamp(12.0f * scale, 9.0f, 18.0f);
                const ImVec2 textSize = scaledTextSize(caption, fontSize);
                draw->AddText(
                    ImGui::GetFont(), fontSize,
                    ImVec2(wmin.x + (wmax.x - wmin.x - textSize.x) * 0.5f,
                           wmin.y + (wmax.y - wmin.y - textSize.y) * 0.5f),
                    IM_COL32(150, 150, 150, 255), caption);
                break;
            }
            case Galeshi::UIWidgetType::Progress: {
                draw->AddRectFilled(wmin, wmax, IM_COL32(25, 25, 25, 220),
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
                    IM_COL32(255, 255, 255, 255), widget.label.c_str());
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
                draw->AddText(ImVec2(wmin.x, wmin.y - 16.0f), kText,
                              widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::CheckBox:
            case Galeshi::UIWidgetType::Toggle: {
                const float box = std::min(18.0f, wmax.y - wmin.y);
                draw->AddRectFilled(wmin, ImVec2(wmin.x + box, wmin.y + box),
                                    IM_COL32(30, 30, 30, 230), 3.0f);
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
                              kText, widget.label.c_str());
                break;
            }
            case Galeshi::UIWidgetType::Input:
            case Galeshi::UIWidgetType::Dropdown:
            case Galeshi::UIWidgetType::ScrollList:
            case Galeshi::UIWidgetType::LayoutGroup:
                draw->AddRectFilled(wmin, wmax, IM_COL32(25, 25, 25, 220),
                                    widget.radius * scale);
                draw->AddRect(wmin, wmax, kBorder, widget.radius * scale);
                draw->AddText(ImVec2(wmin.x + 4.0f, wmin.y + 4.0f),
                              IM_COL32(160, 160, 160, 255),
                              widget.label.c_str());
                break;
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

        return consumed;
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

            // Split the bottom area into Left (Content Browser) and Right (Lua Editor)
            ImGuiID bottomLeft = ImGui::DockBuilderSplitNode(bottom, ImGuiDir_Left, 0.20f, nullptr, &bottom);

            ImGui::DockBuilderDockWindow("Viewport",          center);
            ImGui::DockBuilderDockWindow("World Outliner",    left);
            ImGui::DockBuilderDockWindow("Details",           right);
            
            // Left side of bottom panel
            ImGui::DockBuilderDockWindow("Content Browser",   bottomLeft);
            
            // Right side of bottom panel (tabs)
            ImGui::DockBuilderDockWindow("UI Designer",       bottom);
            ImGui::DockBuilderDockWindow("Lua Editor",        bottom);
            ImGui::DockBuilderDockWindow("Lua Console",       bottom);
            ImGui::DockBuilderDockWindow("Script Components", bottom);

            ImGui::DockBuilderFinish(dockspaceId);
        }
        ImGui::DockSpace(dockspaceId, ImVec2(0, 0), ImGuiDockNodeFlags_None);
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Viewport & Picking & Sculpting
    // -------------------------------------------------------------------
    void DrawViewport() {
        spdlog::trace("[Main] DrawViewport");
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

        if (w > 0 && h > 0) {
            if (!m_framebuffer || m_framebuffer->Width() != w || m_framebuffer->Height() != h) {
                // Recreating an FBO that bgfx still considers bound can make it
                // skip the image-layout transition on the next bind (the new
                // handle may reuse the old index). Touch the backbuffer first
                // so the next view always binds a different framebuffer.
                bgfx::setViewFrameBuffer(0, BGFX_INVALID_HANDLE);
                bgfx::setViewRect(0, 0, 0, 1, 1);
                bgfx::setViewClear(0, BGFX_CLEAR_NONE);
                bgfx::touch(0);

                if (!m_framebuffer) m_framebuffer = std::make_unique<Galeshi::Framebuffer>();
                m_framebuffer->Resize(w, h);
            }
        }

        m_viewportW = w;
        m_viewportH = h;

        if (m_framebuffer && m_framebuffer->Valid()) {
            
            // --- PSSM SHADOW PASS ---
            auto lview = m_scene.Registry().view<Galeshi::DirectionalLightComponent>();
            glm::mat4 lightMtx0, lightMtx1, lightMtx2;
            float cascadeSplits[4] = {15.0f, 40.0f, 100.0f, 0.0f}; // Fallback

            if (!lview.empty()) {
                auto lh = *lview.begin();
                auto& dl = lview.get<Galeshi::DirectionalLightComponent>(lh);
                glm::vec3 lightDir = glm::normalize(dl.direction);

                // 1. Calculate Cascade Splits (Practical Split Scheme)
                float nearPlane = 0.1f;
                float farPlane = 1000.0f;
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

                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), minX, maxX, minY, maxY, -maxZ - 10.0f, -minZ + 10.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
                    
                    const glm::mat4 lightMtx = lightProj * lightView;
                    if (i == 0) lightMtx0 = lightMtx;
                    else if (i == 1) lightMtx1 = lightMtx;
                    else lightMtx2 = lightMtx;

                    m_render->RenderShadowMap(m_scene, lightMtx, 2 + i, m_shadowFbos[i]);
                }
            } else {
                // Fallback (без света)
                cascadeSplits[0] = 15.0f; cascadeSplits[1] = 40.0f; cascadeSplits[2] = 100.0f;
                for (int i = 0; i < 3; ++i) {
                    glm::mat4 lightView = glm::lookAt(glm::vec3(0,20,0), glm::vec3(0), glm::vec3(0,1,0));
                    glm::mat4 lightProj;
                    bx::mtxOrtho(glm::value_ptr(lightProj), -15.0f, 15.0f, -15.0f, 15.0f, 0.1f, 50.0f, 0.0f, bgfx::getCaps()->homogeneousDepth);
                    const glm::mat4 lightMtx = lightProj * lightView;
                    if (i == 0) lightMtx0 = lightMtx;
                    else if (i == 1) lightMtx1 = lightMtx;
                    else lightMtx2 = lightMtx;
                    m_render->RenderShadowMap(m_scene, lightMtx, 2 + i, m_shadowFbos[i]);
                }
            }

            // --- POINT LIGHT SHADOW PASS ---
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

            // --- SPOT LIGHT SHADOW PASS ---
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

bgfx::setViewFrameBuffer(1, m_framebuffer->Handle());
            m_render->Render(m_scene, m_camera, 1, w, h);

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

                glm::mat4 proj = m_camera.Projection();
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
                if (m_postProcess.SsaoEnabled()) {
                    m_postProcess.RenderSSAO(m_framebuffer->WorldPos(), m_framebuffer->Normal(),
                                             m_camera.View(), proj, 15, w, h);
                }

                m_postProcess.RenderLightCulling(plCount, plPosA, currVP, w, h);
                m_postProcess.RenderLighting(
                    m_framebuffer->Color(),
                    m_framebuffer->Normal(),
                    m_framebuffer->WorldPos(),
                    m_framebuffer->Material(),
                    m_shadowMaps[0], m_shadowMaps[1], m_shadowMaps[2],
                    lightMtx0, lightMtx1, lightMtx2,
                    invViewProj,
                    cascadeSplits,
                    camPos, lDirA, lColA,
                    plPosA, plColA, plCount,
                    slPos, slDir, slCol, slCount,
                    20, w, h);

                // The post-processing passes are submitted in increasing view
                // ID order. The ping-pong swaps happen on the CPU, so the call
                // order must match the GPU view order exactly.

                // 1. SSR (View 24)
                if (m_postProcess.SsrEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderSSR(
                            m_postProcess.OutputTexture(), m_framebuffer->Normal(),
                            m_framebuffer->Depth(), m_postProcess.PingFbo(),
                            proj, invProj, camPos, 24, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 2. Motion Blur (velocity view 23, blur view 25)
                if (m_postProcess.MotionBlurEnabled() && m_postProcess.HasPrevViewProj()) {
                    if (bgfx::isValid(m_postProcess.RenderMotionBlur(
                            m_postProcess.OutputTexture(), m_framebuffer->WorldPos(),
                            m_postProcess.GetPrevViewProj(), glm::inverse(currVP),
                            m_postProcess.PingFbo(), 23, 25, w, h, m_frameDelta))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 3. Atmosphere (View 26)
                if (m_postProcess.AtmosphereEnabled()) {
                    float sunDir[4] = { -lDirA[0], -lDirA[1], -lDirA[2], 20.0f };
                    if (bgfx::isValid(m_postProcess.RenderAtmosphere(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            glm::inverse(currVP), camPos, sunDir, m_postProcess.PingFbo(),
                            26, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 4. Volumetric Light / god rays (View 27)
                if (m_postProcess.GodRaysEnabled()) {
                    float sunDir[4] = { -lDirA[0], -lDirA[1], -lDirA[2], 20.0f };
                    if (bgfx::isValid(m_postProcess.RenderGodRays(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            currVP, camPos, sunDir, m_postProcess.PingFbo(),
                            27, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 5. Depth of Field (Views 28..31)
                if (m_postProcess.DofEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderDof(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            proj, invProj, m_postProcess.PingFbo(), 28, w, h,
                            10.0f, 2.8f))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 6. Bloom (Views 32..35)
                if (m_postProcess.BloomEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderBloom(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            32, w, h, 1.2f, 0.8f))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 7. TAA (Views 36..37)
                if (m_postProcess.TaaEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderTaa(
                            m_postProcess.OutputTexture(), m_framebuffer->Depth(),
                            m_postProcess.GetPrevViewProj(), glm::inverse(currVP),
                            m_postProcess.PingFbo(), 36, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 8. Tonemap (View 38)
                m_postProcess.RenderTonemap(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 38, w, h, 1.0f, 1.0f, 1.0f);
                m_postProcess.SwapFbos();

                // 9. Color Correction (View 39)
                m_postProcess.RenderColorCorrection(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 39, w, h);
                m_postProcess.SwapFbos();

                // 10. Vignette (View 40)
                m_postProcess.RenderVignette(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 40, w, h);
                m_postProcess.SwapFbos();

                // 11. Chromatic Aberration (View 41)
                m_postProcess.RenderChromatic(m_postProcess.OutputTexture(), m_postProcess.PingFbo(), 41, w, h);
                m_postProcess.SwapFbos();

                // 12. SMAA (Views 42..43)
                if (m_postProcess.SmaaEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderSmaa(
                            m_postProcess.OutputTexture(), 42, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 13. FXAA (View 44)
                if (m_postProcess.FxaaEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderFxaa(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            44, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // 14. Invert (View 45)
                if (m_postProcess.InvertEnabled()) {
                    if (bgfx::isValid(m_postProcess.RenderInvert(
                            m_postProcess.OutputTexture(), m_postProcess.PingFbo(),
                            45, w, h))) {
                        m_postProcess.SwapFbos();
                    }
                }

                // Store the camera matrices for the next frame's motion blur
                // and TAA reprojection (regardless of whether TAA is on).
                m_postProcess.SetPrevViewProj(currVP);
            } else {
                // View 2 is reserved for the first directional shadow cascade.
                m_postProcess.Render(46, m_framebuffer->Color(), w, h);
            }
        }

        // Debug rendering (View 49)
        if (m_debug && !m_isGameMode && m_framebuffer) {
            m_debug->Begin();
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
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            if (sel && sel.HasComponent<Galeshi::TransformComponent>() && !sel.HasComponent<Galeshi::SkeletalComponent>()) {
                auto& tc = sel.GetComponent<Galeshi::TransformComponent>();
                m_debug->AddBox(tc.position, tc.scale + glm::vec3(0.1f), {1, 1, 0, 1});
            }
            if (m_framebuffer && bgfx::isValid(m_framebuffer->Handle())) {
                // Draw directly into the G-buffer FBO: it already owns the
                // scene depth, and its color attachment has been filled with
                // the post-processed image at view 47. The G-buffer color is
                // no longer sampled after the lighting pass.
                bgfx::setViewFrameBuffer(49, m_framebuffer->Handle());
                m_debug->Render(m_camera, 49, w, h);
            }
        }

        // Copy the final HDR color into the G-buffer color attachment, then
        // blend the transparent layer over it (view 48, before debug at 49 and
        // the ImGui blit at 50). Reusing the persistent G-buffer FBO avoids
        // temporary FBOs whose attachment layout can get out of sync.
        if (m_framebuffer && bgfx::isValid(m_framebuffer->Color()) &&
            m_postProcess.OutputFbo().idx != bgfx::kInvalidHandle) {
            m_postProcess.BlitTexture(m_postProcess.OutputTexture(), m_framebuffer->Color(), 47);
            bgfx::setViewFrameBuffer(48, m_framebuffer->Handle());
            bgfx::setViewRect(48, 0, 0, w, h);
            m_render->RenderGlass(m_scene, m_camera, 48, w, h);
        }

        // Set when the 2D UI canvas overlay handles the click this frame.
        bool uiOverlayConsumed = false;

        // Blit для ImGui (View 50)
        if (m_framebuffer && bgfx::isValid(m_framebuffer->Color())) {
            bgfx::TextureHandle __ppTex = m_framebuffer->Color();
            bgfx::TextureHandle __disp = bgfx::isValid(__ppTex) ? __ppTex : m_framebuffer->Color();
            if (bgfx::isValid(__ppTex)) {
                m_postProcess.BlitTexture(__ppTex, m_postProcess.BlitTextureA(), 50);
                __disp = m_postProcess.BlitTextureA();
            }
            ImTextureID texId = static_cast<ImTextureID>(__disp.idx);
            ImGui::Image(texId, ImVec2(static_cast<float>(w), static_cast<float>(h)), ImVec2(0, 0), ImVec2(1, 1));
            m_viewportImagePos = ImGui::GetItemRectMin();
            m_viewportImageSize = ImGui::GetItemRectSize();

            // Accept assets dragged from the Content Browser. Models are
            // spawned under the cursor; textures are still applied to the
            // selected material by OnAssetDropped().
            const ImVec2 dropImageMin = ImGui::GetItemRectMin();
            const ImVec2 dropImageMax = ImGui::GetItemRectMax();
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
        }

        if (m_framebuffer && bgfx::isValid(m_framebuffer->Color())) {
            if (m_isGameMode) {
                ImGui::End();
                return; // Пропускаем гизмо и выделение мышью
            }

            ImVec2 imgMin = ImGui::GetItemRectMin();
            ImVec2 imgMax = ImGui::GetItemRectMax();

            // --- 1. Drag (только ЛКМ, screen-space, без стрелок W/E/R) ---
            Galeshi::Entity sel = m_editor.Selected(&m_scene);
            const bool selHasTf = sel && sel.HasComponent<Galeshi::TransformComponent>();
            const bool selectedIsUICanvas =
                sel && sel.HasComponent<Galeshi::UICanvasComponent>();
            const bool landscapeSculptActive =
                !m_isGameMode && selHasTf &&
                sel.HasComponent<Galeshi::LandscapeComponent>() &&
                m_editor.toolMode != Galeshi::EditorState::ToolMode::Select;

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
                glm::vec4 rayStart4 = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
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
                if (overImage &&
                    Galeshi::LandscapeMesh::Raycast(landscape, landscapeWorld,
                                                    rayOrigin, rayDir, hit)) {
                    m_lastSculptHeight = hit.height;
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        PushHistory();
                        m_wasSculpting = true;
                    }
                    if (m_wasSculpting && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
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
                            default: break;
                        }
                        settings.radius = m_editor.brushRadius;
                        settings.strength = m_editor.brushStrength;
                        settings.falloff = m_editor.brushFalloff;
                        settings.flattenHeight = m_editor.flattenHeight;
                        settings.noiseFrequency = m_editor.noiseFrequency;
                        if (Galeshi::LandscapeMesh::ApplyBrush(landscape, settings,
                                                               hit.localPosition.x,
                                                               hit.localPosition.z,
                                                               ImGui::GetIO().DeltaTime) > 0) {
                            if (sel.HasComponent<Galeshi::MeshComponent>()) {
                                auto& meshComponent = sel.GetComponent<Galeshi::MeshComponent>();
                                if (meshComponent.mesh) {
                                    Galeshi::LandscapeMesh::UpdateMesh(
                                        landscape, *meshComponent.mesh);
                                }
                            }
                        }
                    }
                }

                if (m_wasSculpting && !ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    m_wasSculpting = false;
                    if (sel.HasComponent<Galeshi::VegetationComponent>()) {
                        sel.GetComponent<Galeshi::VegetationComponent>().dirty = true;
                    }
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
            if (selHasTf && !landscapeSculptActive && !selectedIsUICanvas) {
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
            if (!landscapeSculptActive && !selectedIsUICanvas && !uiOverlayConsumed &&
                ImGui::IsItemHovered() && !m_isDragging &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) && overCenter &&
                !ImGuizmo::IsOver() && !ImGuizmo::IsUsing()) {
                m_isDragging = true;
                m_dragStartMouse = { mouse.x, mouse.y };
                m_dragStartObjWorld = selWorldPos;
            }

            // Perform drag while LMB held
            if (m_isDragging && ImGui::IsMouseDown(ImGuiMouseButton_Left) && selHasTf &&
                !ImGuizmo::IsUsing()) {
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
            if (selOnScreen && !selectedIsUICanvas) {
                ImU32 hintColor = overCenter ? IM_COL32(255, 220, 40, 220)
                                             : IM_COL32(255, 255, 255, 110);
                ImGui::GetWindowDrawList()->AddCircle(
                    ImVec2(selScreen.x, selScreen.y),
                    centerRadiusPx * 0.5f, hintColor, 24, 2.0f);
            }

            // --- 2. ЛКМ Выделение (Raycast по AABB) ---
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
                glm::vec4 rayStart = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
                glm::vec4 rayEnd   = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);
                rayStart /= rayStart.w; rayEnd /= rayEnd.w;
                
                glm::vec3 rayDir = glm::normalize(glm::vec3(rayEnd - rayStart));
                glm::vec3 rayOrigin = glm::vec3(rayStart);

                Galeshi::Entity bestHit;
                float bestDist = 99999.0f;

                auto view = m_scene.Registry().view<Galeshi::TransformComponent>();
                for (auto e : view) {
                    auto& tc = view.get<Galeshi::TransformComponent>(e);
                    
                    // 1. Вычисляем МИРОВУЮ позицию (проходим вверх по родителям)
                    glm::vec3 worldPos = tc.position;
                    entt::entity curr = e;
                    int __depth = 0; while (__depth++ < 128 && m_scene.Registry().all_of<Galeshi::ParentComponent>(curr)) {
                        curr = m_scene.Registry().get<Galeshi::ParentComponent>(curr).parent;
                        if (m_scene.Registry().all_of<Galeshi::TransformComponent>(curr)) {
                            worldPos += m_scene.Registry().get<Galeshi::TransformComponent>(curr).position;
                        }
                    }

                    // 2. Увеличиваем коробку для скелетных моделей
                    float size = 0.5f;
                    if (m_scene.Registry().all_of<Galeshi::SkeletalComponent>(curr)) {
                        size = 1.5f; // Персонажи обычно больше кубов
                    }

                    glm::vec3 minB = worldPos - glm::vec3(size);
                    glm::vec3 maxB = worldPos + glm::vec3(size);
                    
                    // 3. Ray-AABB
                    float tmin = (minB.x - rayOrigin.x) / rayDir.x;
                    float tmax = (maxB.x - rayOrigin.x) / rayDir.x;
                    if (tmin > tmax) std::swap(tmin, tmax);
                    
                    float tymin = (minB.y - rayOrigin.y) / rayDir.y;
                    float tymax = (maxB.y - rayOrigin.y) / rayDir.y;
                    if (tymin > tymax) std::swap(tymin, tymax);
                    
                    if ((tmin > tymax) || (tymin > tmax)) continue;
                    if (tymin > tmin) tmin = tymin;
                    if (tymax < tmax) tmax = tymax;
                    
                    float tzmin = (minB.z - rayOrigin.z) / rayDir.z;
                    float tzmax = (maxB.z - rayOrigin.z) / rayDir.z;
                    if (tzmin > tzmax) std::swap(tzmin, tzmax);
                    
                    if ((tmin > tzmax) || (tzmin > tmax)) continue;
                    if (tzmin > tmin) tmin = tzmin;
                    if (tzmax < tmax) tmax = tzmax;

                    if (tmax < 0) continue; // За спиной
                    
                    if (tmin < bestDist) {
                        bestDist = tmin;
                        bestHit = Galeshi::Entity(curr, &m_scene); // Выделяем корень, а не кость
                    }
                }

                if (bestHit) m_editor.Select(bestHit);
                else m_editor.ClearSelection();


            }

                    }
        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Details
    // -------------------------------------------------------------------
    void DrawTextureSlot(const char* label, std::shared_ptr<Galeshi::Texture>& slot,
                         bool srgb) {
        ImGui::PushID(label);
        ImGui::Text("%s", label);

        if (slot && bgfx::isValid(slot->Handle())) {
            ImTextureID texId = static_cast<ImTextureID>(slot->Handle().idx);
            ImGui::Image(texId, ImVec2(64, 64));
            ImGui::SameLine();
            ImGui::BeginGroup();
            std::string path = slot->Path();
            auto slash = path.find_last_of('/');
            std::string name = (slash != std::string::npos) ? path.substr(slash + 1) : path;
            ImGui::TextWrapped("%s", name.c_str());
            if (ImGui::Button("Clear")) {
                slot.reset();
            }
            ImGui::EndGroup();
        } else {
            ImGui::Button("Empty (drop texture here)", ImVec2(200, 64));
        }

        if (ImGui::BeginDragDropTarget()) {
            if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("GALESHI_ASSET_PATH")) {
                const char* path = static_cast<const char*>(payload->Data);
                std::string p = path;
                auto dotPos = p.find_last_of('.');
                std::string ext = (dotPos != std::string::npos) ? p.substr(dotPos) : "";
                for (auto& c : ext) c = static_cast<char>(std::tolower(c));
                if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                    ext == ".bmp" || ext == ".tga" ||
                    ext == ".dds" || ext == ".ktx") {
                    slot = m_assets->LoadTexture(p, srgb);
                }
            }
            ImGui::EndDragDropTarget();
        }

        ImGui::PopID();
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
                if (pc.parent == e) children.push_back(child);
            });
            if (IsModelRoot(e, children)) {
                children.clear();
            }
            bool hasChildren = !children.empty();

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_DefaultOpen;
            if (m_editor.IsSelected(Galeshi::Entity(e, &m_scene))) flags |= ImGuiTreeNodeFlags_Selected;
            if (!hasChildren) flags |= ImGuiTreeNodeFlags_Leaf;

            bool open = ImGui::TreeNodeEx("##node", flags, "%s", name.c_str());

            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                m_editor.Select(Galeshi::Entity(e, &m_scene));

            if (ImGui::BeginDragDropSource()) {
                ImGui::SetDragDropPayload("ENTITY", &e, sizeof(entt::entity));
                ImGui::Text("Move %s", name.c_str());
                ImGui::EndDragDropSource();
            }
            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY")) {
                    entt::entity dragged = *(const entt::entity*)payload->Data;
                    if (dragged != e) {
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
                if (reg.all_of<Galeshi::ParentComponent>(dragged)) {
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

void DrawDetails() {
        ImGui::Begin("Details");
        auto selected = m_editor.SelectedAll(&m_scene);
        if (selected.empty()) { ImGui::TextDisabled("No selection"); ImGui::End(); return; }

        if (!selected.empty() && selected[0].HasComponent<Galeshi::NameComponent>()) {
            auto& nc = selected[0].GetComponent<Galeshi::NameComponent>();
            char buf[256]; std::snprintf(buf, sizeof(buf), "%s", nc.name.c_str());
            if (ImGui::InputText("Name", buf, sizeof(buf))) nc.name = buf;
        }

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            glm::vec3 pos = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().position : glm::vec3(0.0f);
            glm::vec3 scl = selected[0].HasComponent<Galeshi::TransformComponent>() ? selected[0].GetComponent<Galeshi::TransformComponent>().scale : glm::vec3(1.0f);
            glm::vec3 rot = selected[0].HasComponent<Galeshi::TransformComponent>() ? glm::degrees(glm::eulerAngles(selected[0].GetComponent<Galeshi::TransformComponent>().rotation)) : glm::vec3(0.0f);

            if (ImGui::DragFloat3("Position", glm::value_ptr(pos), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().position = pos; }


        
            if (ImGui::DragFloat3("Scale", glm::value_ptr(scl), 0.05f)) { for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().scale = scl; }
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(rot), 1.0f)) { glm::quat q = glm::quat(glm::radians(rot)); for (auto e : selected) if (e.HasComponent<Galeshi::TransformComponent>()) e.GetComponent<Galeshi::TransformComponent>().rotation = q; }
        }

        if (auto sel = m_editor.Selected(&m_scene); sel) {
            bool isLight = sel.HasComponent<Galeshi::DirectionalLightComponent>() ||
                           sel.HasComponent<Galeshi::PointLightComponent>() ||
                           sel.HasComponent<Galeshi::SpotLightComponent>();
            bool isGround = false;
            if (sel.HasComponent<Galeshi::NameComponent>() && sel.GetComponent<Galeshi::NameComponent>().name == "Ground") {
                isGround = true;
            }

            if (!isLight && !isGround) {
                if (!sel.HasComponent<Galeshi::AudioSourceComponent>()) {
                    if (ImGui::Button("+ Add Audio Source")) {
                        sel.AddComponent<Galeshi::AudioSourceComponent>();
                    }
                } else {
                    auto& audio = sel.GetComponent<Galeshi::AudioSourceComponent>();
                    if (ImGui::CollapsingHeader("Audio Source")) {
                        char buf[256];
                        snprintf(buf, sizeof buf, "%s", audio.soundPath.c_str());
                        if (ImGui::InputText("Sound Path", buf, sizeof buf)) {
                            audio.soundPath = buf;
                        }
                        ImGui::Checkbox("Autoplay", &audio.autoplay);
                        ImGui::Checkbox("Loop", &audio.loop);
                        if (ImGui::Button(audio.playing ? "Stop" : "Play")) {
                            if (audio.playing) {
                                m_audio.StopSound(audio.soundPath);
                                audio.playing = false;
                            } else {
                                m_audio.PlaySound(audio.soundPath, audio.loop);
                                audio.playing = true;
                            }
                        }
                        ImGui::SameLine();
                        ImGui::Text("State: %s", audio.playing ? "playing" : "idle");
                        if (ImGui::Button("Remove Audio Source")) {
                            sel.RemoveComponent<Galeshi::AudioSourceComponent>();
                        }
                    }
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::LandscapeComponent>() &&
            ImGui::CollapsingHeader("Landscape (Sculpting)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& landscape = selected[0].GetComponent<Galeshi::LandscapeComponent>();
            const char* toolNames[] = {"Select", "Raise", "Lower", "Smooth", "Flatten", "Noise"};
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
            ImGui::TextDisabled("%d x %d vertices, scale %.2f", landscape.sizeX, landscape.sizeZ, landscape.scale);
            if (m_editor.toolMode == Galeshi::EditorState::ToolMode::Select) {
                ImGui::TextDisabled("Select a brush, then left-click and drag in the viewport.");
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
            ImGui::CollapsingHeader("Vegetation (Instanced)", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& veg = selected[0].GetComponent<Galeshi::VegetationComponent>();
            bool changed = false;
            changed |= ImGui::Checkbox("Enabled", &veg.enabled);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Tree Shadows", &veg.castShadows);
            ImGui::SameLine();
            changed |= ImGui::Checkbox("Grass Shadows", &veg.grassCastShadows);
            ImGui::SetNextItemWidth(170.0f);
            changed |= ImGui::DragInt("Trees", &veg.treeCount, 1.0f, 0, 20000);
            ImGui::SetNextItemWidth(170.0f);
            changed |= ImGui::DragInt("Grass", &veg.grassCount, 10.0f, 0, 50000);
            changed |= ImGui::DragFloatRange2("Tree Scale", &veg.treeMinScale, &veg.treeMaxScale,
                                              0.01f, 0.05f, 10.0f, "Min %.2f", "Max %.2f");
            changed |= ImGui::DragFloatRange2("Grass Scale", &veg.grassMinScale, &veg.grassMaxScale,
                                              0.01f, 0.05f, 10.0f, "Min %.2f", "Max %.2f");
            changed |= ImGui::DragFloat("Slope Limit", &veg.slopeLimit, 0.01f, 0.0f, 5.0f);
            changed |= ImGui::InputScalar("Seed", ImGuiDataType_U32, &veg.seed);
            changed |= ImGui::ColorEdit4("Trunk Color", glm::value_ptr(veg.trunkColor));
            changed |= ImGui::ColorEdit4("Canopy Color", glm::value_ptr(veg.canopyColor));
            changed |= ImGui::ColorEdit4("Grass Color", glm::value_ptr(veg.grassColor));
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
            ImGui::TextDisabled("%zu trees / %zu grass", veg.treeInstances.size(),
                                veg.grassInstances.size());
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

        // Helper lambda to draw material UI safely
        auto DrawMaterialUI = [&](const char* label, Galeshi::MaterialComponent& matc, uint32_t eid) {
            if (!matc.material) return;
            auto& m = *matc.material;
            ImGui::PushID(eid);
            if (ImGui::CollapsingHeader(label, ImGuiTreeNodeFlags_DefaultOpen)) {
                const size_t userCount = materialUserCount(&m);
                if (m.base) {
                    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f),
                        "Instance of: %s", m.base->name.c_str());
                    if (ImGui::Button("Reset to Base")) {
                        const auto base = m.base;
                        const std::string instanceName = m.name;
                        m = *base;
                        m.base = base;
                        m.name = instanceName;
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

                ImGui::Separator();

                // --- Пресеты ---
                ImGui::Text("Presets:");
                if (ImGui::Button("Metal")) {
                    m.metallicFactor = 1.0f; m.roughnessFactor = 0.1f;
                    m.albedoFactor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
                }
                ImGui::SameLine();
                if (ImGui::Button("Plastic")) {
                    m.metallicFactor = 0.0f; m.roughnessFactor = 0.5f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Rubber")) {
                    m.metallicFactor = 0.0f; m.roughnessFactor = 0.9f;
                }
                ImGui::SameLine();
                if (ImGui::Button("Gold")) {
                    m.metallicFactor = 1.0f; m.roughnessFactor = 0.2f;
                    m.albedoFactor = glm::vec4(1.0f, 0.766f, 0.336f, 1.0f);
                }

                ImGui::Separator();

                // --- Основные параметры ---
                ImGui::ColorEdit4("Albedo Factor", glm::value_ptr(m.albedoFactor));
                ImGui::SliderFloat("Metallic Factor",  &m.metallicFactor,  0.0f, 1.0f);
                ImGui::SliderFloat("Roughness Factor", &m.roughnessFactor, 0.04f, 1.0f);
                ImGui::SliderFloat("Normal Strength",  &m.normalStrength,  0.0f, 2.0f);
                ImGui::SliderFloat("Occlusion Strength", &m.occlusionStrength, 0.0f, 1.0f);
                ImGui::SliderFloat("Alpha Cutoff",     &m.alphaCutoff,     0.0f, 1.0f);
                ImGui::Checkbox("Glass (Transparent)", &m.isGlass);
                if (m.isGlass) {
                    ImGui::SliderFloat("Glass Opacity", &m.glassOpacity, 0.0f, 1.0f);
                }
                ImGui::Checkbox("Two Sided", &m.twoSided);
                if (ImGui::IsItemHovered()) {
                    ImGui::SetTooltip("Render both faces and flip normals on back-facing fragments.\n"
                                      "When disabled, backfaces are culled.");
                }
                ImGui::ColorEdit3("Emissive Color", glm::value_ptr(m.emissiveFactor));

                ImGui::Separator();
                ImGui::Text("Texture Maps (drag from Content Browser):");

                // --- Текстурные слоты ---
                DrawTextureSlot("Albedo Map",           m.albedoMap,           true);
                DrawTextureSlot("Normal Map",           m.normalMap,           false);
                DrawTextureSlot("Metallic/Roughness",   m.metallicRoughnessMap, false);
                DrawTextureSlot("Metallic Map",         m.metallicMap,         false);
                DrawTextureSlot("Roughness Map",        m.roughnessMap,        false);
                DrawTextureSlot("Emissive Map",         m.emissiveMap,         true);
                DrawTextureSlot("Occlusion Map",        m.occlusionMap,        false);
                DrawTextureSlot("Opacity Map",          m.opacityMap,          false);
                DrawTextureSlot("Specular Map",         m.specularMap,         false);
            }
            ImGui::PopID();
        };

        // 1. Draw material for the selected entity itself (if any)
        if (selected[0].HasComponent<Galeshi::MaterialComponent>()) {
            auto& matc = selected[0].GetComponent<Galeshi::MaterialComponent>();
            std::string label = (matc.material && !matc.material->name.empty())
                ? matc.material->name : std::string("Material");
            DrawMaterialUI(label.c_str(), matc, (uint32_t)selected[0].Handle());
        }

        // 2. Draw materials for all children (e.g., skeletal mesh sub-meshes)
        for (auto e : m_scene.AllEntities()) {
            if (e.HasComponent<Galeshi::ParentComponent>() && e.GetComponent<Galeshi::ParentComponent>().parent == selected[0].Handle()) {
                if (e.HasComponent<Galeshi::MaterialComponent>()) {
                    auto& matc = e.GetComponent<Galeshi::MaterialComponent>();
                    std::string label = "Material Slot [" + std::to_string((uint32_t)e.Handle()) + "]";
                    if (matc.material && !matc.material->name.empty()) {
                        label = matc.material->name;
                    } else if (e.HasComponent<Galeshi::NameComponent>()) {
                        label = e.GetComponent<Galeshi::NameComponent>().name;
                    }
                    DrawMaterialUI(label.c_str(), matc, (uint32_t)e.Handle());
                }
            }
        }

        if (selected[0].HasComponent<Galeshi::NavAgentComponent>() && ImGui::CollapsingHeader("AI Agent", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& agent = selected[0].GetComponent<Galeshi::NavAgentComponent>();
            ImGui::DragFloat("Speed", &agent.speed, 0.1f, 0.1f, 20.0f);
            ImGui::Text("Path Waypoints: %zu", agent.path.size());
            ImGui::Text("Target: (%.1f, %.1f, %.1f)", agent.targetPosition.x, agent.targetPosition.y, agent.targetPosition.z);
        }

        if (selected[0].HasComponent<Galeshi::AIControllerComponent>() && ImGui::CollapsingHeader("AI Controller", ImGuiTreeNodeFlags_DefaultOpen)) {
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

        if (ImGui::CollapsingHeader("AI & Gameplay")) {
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
            ImGui::CollapsingHeader("Perception", ImGuiTreeNodeFlags_DefaultOpen)) {
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
            ImGui::CollapsingHeader("Behavior Tree", ImGuiTreeNodeFlags_DefaultOpen)) {
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

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() && ImGui::CollapsingHeader("Skeletal Animation", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& skel = selected[0].GetComponent<Galeshi::SkeletalComponent>();
            ImGui::Checkbox("Playing", &skel.isAnimated);
            ImGui::DragFloat("Speed", &skel.animationSpeed, 0.1f, 0.0f, 10.0f);
            ImGui::TextDisabled("Source: %s", skel.sourcePath.empty() ? "(none)" : skel.sourcePath.c_str());
            ImGui::Text("Active Clip: %d  Time: %.2fs", skel.activeAnimation, skel.currentTime);
        }

        if (selected[0].HasComponent<Galeshi::SkeletalComponent>() &&
            !selected[0].HasComponent<Galeshi::AnimationStateMachineComponent>()) {
            if (ImGui::Button("+ Add Animation State Machine")) {
                PushHistory();
                auto& skel = selected[0].GetComponent<Galeshi::SkeletalComponent>();
                auto model = m_assets->LoadModel(skel.sourcePath);
                auto& machine = selected[0].AddComponent<Galeshi::AnimationStateMachineComponent>();
                machine = Galeshi::AnimationSystem::CreateDefaultStateMachine(model.animationNames);
                machine.parameters = {
                    {"Speed", 0.0f},
                    {"PlayerVisible", 0.0f},
                    {"TargetDistance", 100.0f}
                };
            }
        }

        if (selected[0].HasComponent<Galeshi::AnimationStateMachineComponent>() &&
            ImGui::CollapsingHeader("Animation State Machine", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& machine = selected[0].GetComponent<Galeshi::AnimationStateMachineComponent>();
            int clipCount = 1;
            if (selected[0].HasComponent<Galeshi::SkeletalComponent>()) {
                auto model = m_assets->LoadModel(
                    selected[0].GetComponent<Galeshi::SkeletalComponent>().sourcePath);
                clipCount = std::max<int>(1, static_cast<int>(model.animationNames.size()));
            }

            ImGui::Checkbox("Enabled", &machine.enabled);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120.0f);
            ImGui::DragFloat("Playback Speed", &machine.playbackSpeed, 0.01f, 0.0f, 4.0f);
            ImGui::Text("Current State: %d / %zu",
                        machine.currentState, machine.states.size());
            ImGui::TextDisabled("Clip duration: %.2fs", machine.currentClipDuration);

            if (ImGui::TreeNodeEx("States", ImGuiTreeNodeFlags_DefaultOpen)) {
                int removeState = -1;
                for (int i = 0; i < static_cast<int>(machine.states.size()); ++i) {
                    auto& state = machine.states[static_cast<size_t>(i)];
                    ImGui::PushID(i);
                    char stateName[128];
                    std::snprintf(stateName, sizeof(stateName), "%s", state.name.c_str());
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::InputText("Name", stateName, sizeof(stateName))) {
                        state.name = stateName;
                    }
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragInt("Clip", &state.clip, 1.0f, 0, std::max(0, clipCount - 1));
                    ImGui::SameLine();
                    ImGui::Checkbox("Loop", &state.loop);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(80.0f);
                    ImGui::DragFloat("Speed", &state.speed, 0.01f, 0.0f, 4.0f);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Set")) {
                        machine.currentState = i;
                        machine.previousState = -1;
                        machine.stateTime = 0.0f;
                        machine.blendElapsed = machine.blendDuration;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) removeState = i;
                    ImGui::PopID();
                }
                if (removeState >= 0 && machine.states.size() > 1) {
                    machine.states.erase(machine.states.begin() + removeState);
                    for (auto& transition : machine.transitions) {
                        if (transition.fromState == removeState) transition.fromState = -1;
                        else if (transition.fromState > removeState) --transition.fromState;
                        if (transition.toState == removeState) transition.toState = 0;
                        else if (transition.toState > removeState) --transition.toState;
                    }
                    if (machine.currentState >= static_cast<int>(machine.states.size()))
                        machine.currentState = 0;
                }
                if (ImGui::Button("+ Add State")) {
                    Galeshi::AnimationState state;
                    state.name = "State " + std::to_string(machine.states.size());
                    state.clip = 0;
                    machine.states.push_back(state);
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Transitions", ImGuiTreeNodeFlags_DefaultOpen)) {
                const char* conditionNames[] = {
                    "Bool True", "Bool False", "Greater", "Less", "Trigger", "Always"
                };
                int removeTransition = -1;
                for (int i = 0; i < static_cast<int>(machine.transitions.size()); ++i) {
                    auto& transition = machine.transitions[static_cast<size_t>(i)];
                    ImGui::PushID(1000 + i);
                    int from = transition.fromState + 1;
                    std::vector<const char*> fromNames;
                    fromNames.push_back("Any State");
                    for (const auto& state : machine.states) fromNames.push_back(state.name.c_str());
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::Combo("From", &from, fromNames.data(),
                                     static_cast<int>(fromNames.size()))) {
                        transition.fromState = from - 1;
                    }
                    ImGui::SameLine();
                    int to = std::clamp(transition.toState, 0,
                                        std::max(0, static_cast<int>(machine.states.size()) - 1));
                    std::vector<const char*> toNames;
                    for (const auto& state : machine.states) toNames.push_back(state.name.c_str());
                    ImGui::SetNextItemWidth(150.0f);
                    if (ImGui::Combo("To", &to, toNames.data(),
                                     static_cast<int>(toNames.size()))) {
                        transition.toState = to;
                    }
                    int condition = static_cast<int>(transition.condition);
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::Combo("Condition", &condition, conditionNames,
                                     IM_ARRAYSIZE(conditionNames))) {
                        transition.condition = static_cast<Galeshi::AnimationCondition>(condition);
                    }
                    ImGui::SameLine();
                    char parameter[128];
                    std::snprintf(parameter, sizeof(parameter), "%s", transition.parameter.c_str());
                    ImGui::SetNextItemWidth(120.0f);
                    if (ImGui::InputText("Parameter", parameter, sizeof(parameter))) {
                        transition.parameter = parameter;
                    }
                    ImGui::SetNextItemWidth(90.0f);
                    ImGui::DragFloat("Threshold", &transition.threshold, 0.01f);
                    ImGui::SameLine();
                    ImGui::SetNextItemWidth(90.0f);
                    ImGui::DragFloat("Blend", &transition.duration, 0.01f, 0.0f, 4.0f);
                    ImGui::SameLine();
                    ImGui::Checkbox("On", &transition.enabled);
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Remove")) removeTransition = i;
                    ImGui::Separator();
                    ImGui::PopID();
                }
                if (removeTransition >= 0) {
                    machine.transitions.erase(machine.transitions.begin() + removeTransition);
                }
                if (ImGui::Button("+ Add Transition")) {
                    Galeshi::AnimationTransition transition;
                    transition.fromState = machine.currentState;
                    transition.toState = (machine.currentState + 1) % std::max<size_t>(1, machine.states.size());
                    machine.transitions.push_back(transition);
                }
                ImGui::TreePop();
            }

            if (ImGui::TreeNodeEx("Parameters", ImGuiTreeNodeFlags_DefaultOpen)) {
                int removeParameter = -1;
                for (int i = 0; i < static_cast<int>(machine.parameters.size()); ++i) {
                    auto& parameter = machine.parameters[static_cast<size_t>(i)];
                    ImGui::PushID(2000 + i);
                    char parameterName[128];
                    std::snprintf(parameterName, sizeof(parameterName), "%s", parameter.name.c_str());
                    ImGui::SetNextItemWidth(160.0f);
                    if (ImGui::InputText("Name", parameterName, sizeof(parameterName))) {
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
                    machine.parameters.erase(machine.parameters.begin() + removeParameter);
                }
                if (ImGui::Button("+ Add Parameter")) {
                    machine.parameters.push_back({"Parameter", 0.0f});
                }
                ImGui::TreePop();
            }
        }
        
        if (selected[0].HasComponent<Galeshi::DirectionalLightComponent>() && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& dl = selected[0].GetComponent<Galeshi::DirectionalLightComponent>();
            ImGui::DragFloat3("Direction", glm::value_ptr(dl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Color", glm::value_ptr(dl.color));
            ImGui::SliderFloat("Intensity", &dl.intensity, 0.0f, 10.0f);
        }

        if (selected[0].HasComponent<Galeshi::PointLightComponent>() && ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& pl = m_scene.Registry().get<Galeshi::PointLightComponent>(selected[0].Handle());
            ImGui::ColorEdit3("Point Color", glm::value_ptr(pl.color));
            {
                float oldInt = pl.intensity;
                if (ImGui::DragFloat("Point Intensity", &pl.intensity, 0.1f, 0.0f, 100.0f)) {
                }
            }
            ImGui::DragFloat("Point Radius", &pl.radius, 0.1f, 0.1f, 100.0f);
        }

        if (selected[0].HasComponent<Galeshi::SpotLightComponent>() && ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            auto& sl = m_scene.Registry().get<Galeshi::SpotLightComponent>(selected[0].Handle());
            ImGui::DragFloat3("Spot Direction", glm::value_ptr(sl.direction), 0.05f, -1.0f, 1.0f);
            ImGui::ColorEdit3("Spot Color", glm::value_ptr(sl.color));
            ImGui::SliderFloat("Spot Intensity", &sl.intensity, 0.0f, 20.0f);
            ImGui::DragFloat("Spot Radius", &sl.radius, 0.1f, 0.1f, 100.0f);
            ImGui::SliderFloat("Inner Cone", &sl.innerConeAngle, 0.0f, 90.0f);
            ImGui::SliderFloat("Outer Cone", &sl.outerConeAngle, 0.0f, 90.0f);
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

        ImGui::End();
    }

    // -------------------------------------------------------------------
    // Content Browser & Assets
    // -------------------------------------------------------------------
    void DrawLuaEditor() {
        ImGui::Begin("Lua Editor");
        if (!m_luaEditorInitialized) {
            m_luaEditor.SetLanguageDefinition(TextEditor::LanguageDefinition::Lua());
            m_luaEditor.SetPalette(TextEditor::GetDarkPalette());
            m_luaEditor.SetText("-- Select a .lua file from Content Browser");
            m_luaEditorInitialized = true;
        }
        if (m_currentLuaFile.empty()) {
            ImGui::TextDisabled("No file opened. Double-click a .lua file in Content Browser.");
        } else {
            if (ImGui::Button("Save")) {
                spdlog::info("[Editor] Attempting to save Lua script to: {}", std::filesystem::absolute(m_currentLuaFile).string());
                std::ofstream out(m_currentLuaFile);
                if (out.is_open()) {
                    out << m_luaEditor.GetText();
                    out.close();
                    spdlog::info("[Editor] Successfully saved Lua script.");
                } else {
                    spdlog::error("[Editor] FAILED to open file for saving: {}", m_currentLuaFile);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload")) {
                if (m_scripts.HotReload(m_currentLuaFile)) {
                    spdlog::info("[Editor] Reloaded {}", m_currentLuaFile);
                }
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", m_currentLuaFile.c_str());
            ImGui::Separator();
            m_luaEditor.Render("LuaEditor");
        }
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
        m_filePicker.Open(start, "Load Model", { ".fbx", ".gltf", ".glb", ".obj", ".dae" });
    }

    void OnAssetDoubleClicked(const std::string& path) {
        namespace fs = std::filesystem;
        std::string ext = fs::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".lua") {
            m_currentLuaFile = path;
            std::ifstream in(path);
            if (in.is_open()) {
                std::stringstream ss;
                ss << in.rdbuf();
                m_luaEditor.SetText(ss.str());
            }
            return;
        }
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
            SpawnFromModel(path, glm::vec3(0.0f));
        } else if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
                   ext == ".bmp" || ext == ".tga" ||
                   ext == ".dds" || ext == ".ktx") {
            // Применяем к выделенному объекту
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
        glm::vec4 nearPoint = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
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
            if (t > 0.0f && t < 1000.0f) return rayOrigin + rayDir * t;
        }
        return m_editor.camTarget;
    }

    void OnAssetDropped(const std::string& path, glm::vec3 pos) {
        namespace fs = std::filesystem;
        std::string ext = fs::path(path).extension().string();
        for (auto& c : ext) c = static_cast<char>(std::tolower(c));

        if (ext == ".lua") {
            m_currentLuaFile = path;
            std::ifstream in(path);
            if (in.is_open()) {
                std::stringstream ss;
                ss << in.rdbuf();
                m_luaEditor.SetText(ss.str());
            }
            return;
        }
        if (ext == ".obj" || ext == ".fbx" || ext == ".gltf" || ext == ".glb") {
            SpawnFromModel(path, pos, true);
            return;
        }

        // Если бросили текстуру на объект с материалом — установить в albedo
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" ||
            ext == ".bmp" || ext == ".tga" ||
            ext == ".dds" || ext == ".ktx") {
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
        auto& rootTf = root.GetComponent<Galeshi::TransformComponent>();
        
        // Model imports are normally created at the origin. A viewport drop
        // additionally positions the new root at the drop point.
        rootTf.position = placeAtPosition ? pos : glm::vec3(0.0f);
        rootTf.rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        rootTf.scale = glm::vec3(1.0f);
        
        // 2. If the model imports flipped (Z-up), uncomment the next line:
        rootTf.rotation = glm::angleAxis(glm::radians(0.0f), glm::vec3(1.0f, 0.0f, 0.0f));

        if (model.hasAnimation) {
            auto& skel = root.AddComponent<Galeshi::SkeletalComponent>();
            skel.isAnimated = true;
            skel.sourcePath = path;
            skel.boneMatrices.resize(256, glm::mat4(1.0f));

            auto& machine = root.AddComponent<Galeshi::AnimationStateMachineComponent>();
            machine = Galeshi::AnimationSystem::CreateDefaultStateMachine(model.animationNames);
            machine.parameters = {
                {"Speed", 0.0f},
                {"PlayerVisible", 0.0f},
                {"TargetDistance", 100.0f}
            };
        }

        for (size_t i = 0; i < model.meshes.size(); ++i) {
            auto e = m_scene.CreateEntity(stem + "_" + std::to_string(i));
            
            // NOTE: We do NOT reset child transforms here. Assimp gives them local offsets.
            
            e.AddComponent<Galeshi::ParentComponent>(Galeshi::ParentComponent{ root.Handle() });
            e.AddComponent<Galeshi::MeshComponent>(Galeshi::MeshComponent{ model.meshes[i], path, "", static_cast<int>(i) });
            
            std::shared_ptr<Galeshi::Material> mat;
            if (i < model.meshMaterialIndex.size()) {
                int matIdx = model.meshMaterialIndex[i];
                if (matIdx >= 0 && matIdx < static_cast<int>(model.materials.size()))
                    mat = model.materials[static_cast<size_t>(matIdx)];
            }
            if (!mat && !model.materials.empty())
                mat = model.materials[0];
            if (!mat)
                mat = std::make_shared<Galeshi::Material>();
            e.AddComponent<Galeshi::MaterialComponent>(Galeshi::MaterialComponent{ mat });
        }
        m_editor.Select(root);
        m_editor.camTarget = pos;
        m_editor.camRadius = 5.0f;
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
        auto& rb = e.AddComponent<Galeshi::RigidBodyComponent>();
        rb.motionType = Galeshi::RigidBodyComponent::MotionType::Dynamic;
        rb.mass = 1.0f;
        rb.bodyId = m_physics->CreateCapsule(halfHeight, radius, pos, static_cast<entt::entity>(e), false);
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
        // Добавляем большой пол (чтобы тени падали на него)
        auto ground = MakeCube("Ground", {0.0f, -5.0f, 0.0f});
        ground.GetComponent<Galeshi::TransformComponent>().scale = { 100.0f, 10.0f, 100.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->albedoFactor = { 0.3f, 0.3f, 0.3f, 1.0f };
        ground.GetComponent<Galeshi::MaterialComponent>().material->roughnessFactor = 0.8f;
        {
            auto& rb = ground.AddComponent<Galeshi::RigidBodyComponent>();
            rb.motionType = Galeshi::RigidBodyComponent::MotionType::Static;
            rb.bodyId = m_physics->CreateBox(glm::vec3(100.0f, 10.0f, 100.0f) * 0.5f, ground.GetComponent<Galeshi::TransformComponent>().position, static_cast<entt::entity>(ground), true);
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
            return e;
        };
        spawnDynamicCube("Physics_Orange", { 6.0f, 3.0f, -1.0f}, {0.85f, 0.55f, 0.25f}, 0.0f, 0.6f);
        spawnDynamicCube("Physics_White",  { 6.0f, 4.2f, -1.0f}, {0.95f, 0.95f, 0.95f}, 1.0f, 0.15f);
        spawnDynamicCube("Physics_Blue",   { 6.0f, 5.4f, -1.0f}, {0.30f, 0.55f, 0.85f}, 0.0f, 0.8f);


        auto pl = m_scene.CreateEntity("PointLight");
        if (!pl.HasComponent<Galeshi::PointLightComponent>())
            pl.AddComponent<Galeshi::PointLightComponent>();
        pl.GetComponent<Galeshi::TransformComponent>().position = {0.0f, 5.0f, 0.0f};
    }

    // --- Состояние ---
    Galeshi::Scene          m_scene;
    Galeshi::Camera         m_camera;
    Galeshi::EditorState    m_editor;
    float                   m_time = 0.0f;
    float                   m_frameDelta = 1.0f / 60.0f;
    int                     m_renderFrameCounter = 0;
    bool                    m_pendingEffectsEnable = false;

    std::unique_ptr<Galeshi::AssetManager>    m_assets;
    std::unique_ptr<Galeshi::RenderSystem>    m_render;
    std::unique_ptr<Galeshi::Framebuffer>     m_framebuffer;
    std::unique_ptr<Galeshi::ContentBrowser>  m_browser;
    std::unique_ptr<Galeshi::SceneSerializer> m_serializer;
    std::unique_ptr<Galeshi::EnginePhysicsSystem>   m_physics;
    std::unique_ptr<Galeshi::NavigationSystem> m_navSystem;
    std::unique_ptr<Galeshi::DebugRenderer> m_debug;
    bgfx::TextureHandle m_shadowMaps[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_shadowFbos[3] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_pointShadowFbos[6] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };

    uint16_t    m_viewportW = 0, m_viewportH = 0;
    bool        m_viewportHovered = false;
    ImVec2      m_viewportImagePos = {0.0f, 0.0f};
    ImVec2      m_viewportImageSize = {0.0f, 0.0f};
    std::string m_currentScenePath;
    Galeshi::FilePickerDialog m_filePicker;
    Galeshi::FilePickerDialog m_filePickerSave;
    Galeshi::FilePickerDialog m_scenePicker;
    TextEditor m_luaEditor;
    std::string m_currentLuaFile;
    bool m_luaEditorInitialized = false;

    std::vector<std::string> m_history;
    int  m_historyIndex = -1;
    bool m_isRestoringHistory = false;
    bool m_wasUsingGizmo = false;
    bool m_wasSculpting = false;
    float m_lastSculptHeight = 0.0f;
    bool m_showAIDebug = true;

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

    // 1. Копируем exe как <name>.bin и ставим права
    fs::path exePath = fs::current_path() / "GaleshiEditor";
    if (!fs::exists(exePath)) exePath = fs::current_path() / "build" / "debug" / "bin" / "GaleshiEditor";
    
    if (fs::exists(exePath)) {
        fs::path destExe = outDir / (gameName + ".bin");
        fs::copy_file(exePath, destExe, fs::copy_options::overwrite_existing);
        // Явно ставим права на выполнение для всех
        fs::permissions(destExe, 
            fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
            fs::perm_options::replace);
        spdlog::info("[Package] Copied executable to {} (chmod +x)", destExe.filename().string());
    } else {
        spdlog::error("[Package] Executable not found at {}", exePath.string());
        return 1;
    }

    // 2. Копируем assets и shaders
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
    
    // Копируем СКОМПИЛИРОВАННЫЕ шейдеры из build/debug/bin/shaders
    fs::path compiledShaders = fs::current_path() / "build" / "debug" / "bin" / "shaders";
    if (!fs::exists(compiledShaders)) {
        // Если запускаем из build/debug/bin, то текущая папка уже содержит shaders
        compiledShaders = fs::current_path() / "shaders";
    }
    if (fs::exists(compiledShaders)) {
        fs::copy(compiledShaders, outDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        spdlog::info("[Package] Copied compiled shaders from {}", compiledShaders.string());
    } else {
        spdlog::error("[Package] Compiled shaders not found at {}", compiledShaders.string());
    }

    // 3. Копируем scene.json (из $HOME или из корня)
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

    // 4. Сохраняем .gproj
    proj["startScene"] = startScene;
    std::ofstream outProj(outDir / (gameName + ".gproj"));
    outProj << proj.dump(4);
    outProj.close();

    // 5. Создаем run.sh (переходит в папку игры и запускает .bin)
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
    // Парсинг аргументов
    std::string packagePath = "";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--package" && i + 1 < argc) {
            packagePath = argv[i + 1];
        }
    }

    if (!packagePath.empty()) {
        // Принудительно небуферизированный вывод
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        return PackageProject(packagePath);
    }

    // Стандартный запуск редактора

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
    // Принудительно небуферизированный stdout — чтобы логи
    // появлялись до любого crash.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    GaleshiEditorApp* app =
        new GaleshiEditorApp(debugMode, gameMode, gameProjectPath);
    int exitCode = app->Run();

    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(exitCode);
}
