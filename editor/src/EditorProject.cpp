#include "GaleshiEditorApp.hpp"


    // -------------------------------------------------------------------
    // Scene path helpers
    // -------------------------------------------------------------------
    std::filesystem::path GaleshiEditorApp::FindProjectRoot() {
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



    std::string GaleshiEditorApp::SceneStartDirectory() const {
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



    void GaleshiEditorApp::LoadGameStartScene(const std::string& projectFile) {
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



    void GaleshiEditorApp::LoadRecentProjects() {
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



    void GaleshiEditorApp::SaveRecentProjects() {
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
    void GaleshiEditorApp::AddRecentProject(std::string path) {
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
    std::filesystem::path GaleshiEditorApp::RecentScenesPath() {
        const char* home = std::getenv("HOME");
        if (!home) return {};
        return std::filesystem::path(home) / ".galeshi_recent_scenes.json";
    }



    std::string GaleshiEditorApp::NormalizeRecentScenePath(const std::string& path) {
        if (path.empty()) return {};
        std::error_code ec;
        const auto absolute = std::filesystem::absolute(path, ec);
        return ec ? path : absolute.lexically_normal().string();
    }



    std::string GaleshiEditorApp::RecentScenesProjectKey() const {
        return NormalizeRecentScenePath(
            Galeshi::ProjectManager::GetProjectPath());
    }



    void GaleshiEditorApp::LoadRecentScenes() {
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



    void GaleshiEditorApp::SaveRecentScenes() {
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



    void GaleshiEditorApp::AddRecentScene(std::string path) {
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



    void GaleshiEditorApp::RemoveRecentScene(const std::string& path) {
        const std::string normalized = NormalizeRecentScenePath(path);
        auto it = std::find(m_recentScenes.begin(), m_recentScenes.end(),
                            normalized);
        if (it == m_recentScenes.end()) return;
        m_recentScenes.erase(it);
        SaveRecentScenes();
    }



    void GaleshiEditorApp::ClearRecentScenes() {
        if (m_recentScenes.empty()) return;
        m_recentScenes.clear();
        SaveRecentScenes();
        spdlog::info("[RecentScenes] Cleared");
    }



    std::string GaleshiEditorApp::RecentSceneLabel(const std::string& path) const {
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
    std::filesystem::path GaleshiEditorApp::InputBindingsPath() const {
        const std::string projectPath =
            Galeshi::ProjectManager::GetProjectPath();
        if (!projectPath.empty()) {
            return std::filesystem::path(projectPath) / "input.json";
        }
        return FindProjectRoot() / "input.json";
    }



    void GaleshiEditorApp::LoadInputBindings() {
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



    void GaleshiEditorApp::SaveInputBindings() {
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



    void GaleshiEditorApp::UpdateInputRebind() {
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



    std::filesystem::path GaleshiEditorApp::PreferencesPath() {
        const char* home = std::getenv("HOME");
        if (!home) return {};
        return std::filesystem::path(home) / ".galeshi_preferences.json";
    }



    void GaleshiEditorApp::LoadPreferences() {
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



    void GaleshiEditorApp::SavePreferences() {
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



    void GaleshiEditorApp::DrawProjectManager() {
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
                    int removeRecentProject = -1;
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
                        if (ImGui::BeginPopupContextItem(
                                "recent_project_context")) {
                            if (ImGui::MenuItem("Remove from List")) {
                                removeRecentProject = static_cast<int>(i);
                            }
                            ImGui::EndPopup();
                        }
                        ImGui::PopID();
                    }
                    if (removeRecentProject >= 0 &&
                        removeRecentProject <
                            static_cast<int>(m_recentProjects.size())) {
                        m_recentProjects.erase(
                            m_recentProjects.begin() + removeRecentProject);
                        SaveRecentProjects();
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

