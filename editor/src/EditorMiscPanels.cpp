#include "GaleshiEditorApp.hpp"



    void GaleshiEditorApp::DrawOutputLog() {
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



    void GaleshiEditorApp::DrawLuaConsole() {
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



    void GaleshiEditorApp::DrawScriptComponents() {
        ImGui::Begin("Script Components");
        ImGui::BeginChild("ComponentsScroll", ImVec2(0, 0), false, ImGuiWindowFlags_HorizontalScrollbar);
        auto& reg = m_scene.Registry();
        auto view = reg.view<Galeshi::AIControllerComponent>();
        auto scriptView = reg.view<Galeshi::ScriptComponent>();
        if (view.begin() == view.end() && scriptView.begin() == scriptView.end()) {
            ImGui::TextDisabled("No script components found.");
        } else {
            for (auto e : view) {
                auto& ctrl = view.get<Galeshi::AIControllerComponent>(e);
                std::string name = "Entity " + std::to_string((uint32_t)e);
                if (reg.all_of<Galeshi::NameComponent>(e)) name = reg.get<Galeshi::NameComponent>(e).name;
                ImGui::Text("AI  %s -> %s", name.c_str(), ctrl.scriptPath.c_str());
                ImGui::SameLine();
                if (ImGui::SmallButton("Reload")) {
                    // Force reload AI scripts
                    m_scripts.HotReload("assets/scripts/init.lua");
                }
            }
            for (auto e : scriptView) {
                auto& script = scriptView.get<Galeshi::ScriptComponent>(e);
                std::string name = "Entity " + std::to_string((uint32_t)e);
                if (reg.all_of<Galeshi::NameComponent>(e)) {
                    name = reg.get<Galeshi::NameComponent>(e).name;
                }
                ImGui::Text("Lua %s -> %s%s", name.c_str(),
                            script.scriptPath.c_str(),
                            script.enabled ? "" : " (disabled)");
            }
        }
        ImGui::EndChild();
        ImGui::End();
    }



    void GaleshiEditorApp::DrawContentBrowser() {
        ImGui::Begin("Content Browser");
        std::string clicked = m_browser->Draw();
        if (!clicked.empty()) OnAssetDoubleClicked(clicked);
        ImGui::End();
    }



    void GaleshiEditorApp::OpenFileDialog() {
        const char* home = std::getenv("HOME");
        std::string start = home ? home : "/";
        m_filePicker.Open(start, "Load Model",
                          { ".gltf", ".glb", ".obj", ".dae" });
    }

