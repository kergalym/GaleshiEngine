#include "GaleshiEditorApp.hpp"



    // -------------------------------------------------------------------
    // Preferences
    // -------------------------------------------------------------------
    void GaleshiEditorApp::DrawHistoryModal() {
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



    void GaleshiEditorApp::DrawPreferencesWindow() {
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
    void GaleshiEditorApp::ConfigureProfilerViewNames() {
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



    void GaleshiEditorApp::UpdateProfilerDebugState() {
        if (m_profilerDebugEnabled == m_showProfiler) return;
        bgfx::setDebug(m_showProfiler ? BGFX_DEBUG_PROFILER : BGFX_DEBUG_NONE);
        m_profilerDebugEnabled = m_showProfiler;
    }



    void GaleshiEditorApp::UpdateGpuProfilerStats() {
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



    void GaleshiEditorApp::PushProfilerHistory() {
        PushProfileHistory(m_frameHistory, m_cpuProfile.frameMs);
        PushProfileHistory(m_gameLogicHistory, m_cpuProfile.gameLogicMs);
        PushProfileHistory(m_gpuHistory, m_gpuFrameMs);
    }



    void GaleshiEditorApp::DrawProfilerWindow() {
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



    void GaleshiEditorApp::DrawMainMenuBar() {
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

        // Keep the menu row centered while the engine badge stays anchored
        // at the left edge.
        {
            const char* menuLabels[] = {
                "Post-Process", "Rendering", "File", "Edit", "Audio",
                "Create", "Animation", "Testing", "Stats"
            };
            float menuWidth = 0.0f;
            for (size_t i = 0; i < IM_ARRAYSIZE(menuLabels); ++i) {
                if (i > 0) menuWidth += style.ItemSpacing.x;
                menuWidth += ImGui::CalcTextSize(menuLabels[i]).x +
                             style.FramePadding.x * 2.0f;
            }
            const float centeredX =
                (ImGui::GetWindowWidth() - menuWidth) * 0.5f;
            ImGui::SetCursorPosX(
                std::max(ImGui::GetCursorPosX(), centeredX));
        }

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
                pp.fxaaEnabled = false;
                pp.ssaoEnabled = false;
                pp.ssrEnabled = false;
                pp.ssgiEnabled = false;
                pp.fogEnabled = false;
                pp.fogVolumetric = false;
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

            if (createItem("Player")) {
                // Player is authored once per scene. Existing scenes may
                // contain an untagged Player by name, so check both.
                entt::entity existingPlayer = entt::null;
                auto playerView =
                    m_scene.Registry().view<Galeshi::PlayerTagComponent>();
                for (entt::entity handle : playerView) {
                    existingPlayer = handle;
                    break;
                }
                if (existingPlayer == entt::null) {
                    auto namedView =
                        m_scene.Registry().view<Galeshi::NameComponent>();
                    for (entt::entity handle : namedView) {
                        if (namedView.get<Galeshi::NameComponent>(handle).name ==
                            "Player") {
                            existingPlayer = handle;
                            break;
                        }
                    }
                }
                if (existingPlayer != entt::null) {
                    m_editor.Select(
                        Galeshi::Entity(existingPlayer, &m_scene));
                    spdlog::warn(
                        "[Editor] Player already exists; duplicate discarded");
                } else {
                    // Plain gameplay entity: Name, Transform and Player tag.
                    // Physics, audio, materials and meshes are separate
                    // authored components, not implicit Player properties.
                    auto e = m_scene.CreateEntity("Player");
                    e.AddComponent<Galeshi::PlayerTagComponent>();
                    e.GetComponent<Galeshi::TransformComponent>().position =
                        {0.0f, 1.0f, 0.0f};
                    m_editor.Select(e);
                }
            }

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
                entt::entity existing = entt::null;
                for (auto h : landscapeView) {
                    if (!m_scene.Registry()
                             .get<Galeshi::LandscapeComponent>(h)
                             .waterBed) {
                        existing = h;
                        break;
                    }
                }
                if (existing != entt::null) {
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
            if (ImGui::BeginMenu("Collision Shapes")) {
                if (createItem("Box"))    SpawnCollisionShape("CollisionBox", Galeshi::ColliderShapeType::Box);
                if (createItem("Sphere")) SpawnCollisionShape("CollisionSphere", Galeshi::ColliderShapeType::Sphere);
                if (createItem("Capsule"))SpawnCollisionShape("CollisionCapsule", Galeshi::ColliderShapeType::Capsule);
                ImGui::EndMenu();
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

        // Play/Stop sits in its own thin top bar, below the menu row and
        // above the docked viewport.
        const float playBarHeight =
            ImGui::GetFrameHeight() + style.WindowPadding.y * 2.0f +
            style.ItemSpacing.y + 1.0f;
        if (ImGui::BeginViewportSideBar(
                "##EditorPlayBar", ImGui::GetMainViewport(),
                ImGuiDir_Up, playBarHeight,
                ImGuiWindowFlags_NoScrollbar |
                ImGuiWindowFlags_NoScrollWithMouse |
                ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::Separator();
            constexpr float kPlayWidth = 72.0f;
            ImGui::SetCursorPosX(
                (ImGui::GetWindowWidth() - kPlayWidth) * 0.5f);
            if (ImGui::Button(m_editor.isPlaying ? "Stop" : "Play",
                              ImVec2(kPlayWidth, 0.0f))) {
                // Never show the previous mode's cached viewport after
                // Play/Stop.
                m_lastViewportTexture = BGFX_INVALID_HANDLE;
                if (!m_editor.isPlaying) {
                    PushHistory();
                    m_editor.isPlaying = true;
                    EnsurePhysicsBodies();
                    ActivateGameplayCamera();
                    SetViewportInputFocus(true);
                } else {
                    StopPlay();
                }
            }
            ImGui::End();
        }

        // Start loading after the menu popup has been closed, so the loading
        // state cannot invalidate the menu while it is still being drawn.
        if (!recentSceneToLoad.empty()) {
            LoadSceneFromPath(recentSceneToLoad);
        }
    }

