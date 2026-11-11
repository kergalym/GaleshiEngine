#include "GaleshiEditorApp.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace {

std::string BehaviorTreeFileStem(Galeshi::Scene& scene, entt::entity agent) {
    std::string name = "AI_Agent";
    if (const auto* nameComponent =
            scene.Registry().try_get<Galeshi::NameComponent>(agent)) {
        if (!nameComponent->name.empty()) name = nameComponent->name;
    }
    for (char& c : name) {
        const unsigned char value = static_cast<unsigned char>(c);
        if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
            c == '"' || c == '<' || c == '>' || c == '|' || value < 0x20) {
            c = '_';
        }
    }
    while (!name.empty() && (name.back() == ' ' || name.back() == '.')) {
        name.pop_back();
    }
    return name.empty() ? "AI_Agent" : name;
}

nlohmann::json BehaviorTreeToJson(
    const Galeshi::BehaviorTreeComponent& tree) {
    nlohmann::json nodes = nlohmann::json::array();
    for (const auto& node : tree.nodes) {
        nodes.push_back({
            {"name", node.name},
            {"type", static_cast<int>(node.type)},
            {"condition", static_cast<int>(node.condition)},
            {"action", static_cast<int>(node.action)},
            {"children", node.children},
            {"range", node.range},
            {"duration", node.duration},
            {"radius", node.radius},
            {"acceptanceRadius", node.acceptanceRadius},
            {"parameter", node.parameter},
            {"value", node.value}
        });
    }
    return {
        {"format", "galeshi-bt"},
        {"version", 1},
        {"enabled", tree.enabled},
        {"root", tree.root},
        {"tickInterval", tree.tickInterval},
        {"nodes", std::move(nodes)}
    };
}

bool BehaviorTreeFromJson(const nlohmann::json& json,
                          Galeshi::BehaviorTreeComponent& tree) {
    if (!json.is_object() || !json.contains("nodes") ||
        !json["nodes"].is_array() || json["nodes"].empty()) {
        return false;
    }

    Galeshi::BehaviorTreeComponent loaded;
    loaded.enabled = json.value("enabled", true);
    loaded.root = json.value("root", 0);
    loaded.tickInterval = std::max(
        0.0f, json.value("tickInterval", 0.0f));

    for (const auto& nodeJson : json["nodes"]) {
        if (!nodeJson.is_object()) continue;
        Galeshi::BehaviorTreeNode node;
        node.name = nodeJson.value("name", std::string("Node"));
        node.type = static_cast<Galeshi::BehaviorNodeType>(std::clamp(
            nodeJson.value("type", 0), 0, 3));
        node.condition = static_cast<Galeshi::BehaviorCondition>(
            std::clamp(nodeJson.value("condition", 0), 0, 5));
        node.action = static_cast<Galeshi::BehaviorAction>(std::clamp(
            nodeJson.value("action", 0), 0, 8));
        if (nodeJson.contains("children") &&
            nodeJson["children"].is_array()) {
            for (const auto& child : nodeJson["children"]) {
                if (child.is_number_integer()) {
                    node.children.push_back(child.get<int>());
                }
            }
        }
        node.range = nodeJson.value("range", node.range);
        node.duration = nodeJson.value("duration", node.duration);
        node.radius = nodeJson.value("radius", node.radius);
        node.acceptanceRadius =
            nodeJson.value("acceptanceRadius", node.acceptanceRadius);
        node.parameter = nodeJson.value("parameter", std::string{});
        node.value = nodeJson.value("value", std::string{});
        loaded.nodes.push_back(std::move(node));
    }
    if (loaded.nodes.empty()) return false;

    const int count = static_cast<int>(loaded.nodes.size());
    loaded.root = std::clamp(loaded.root, 0, count - 1);
    for (int i = 0; i < count; ++i) {
        auto& children = loaded.nodes[static_cast<size_t>(i)].children;
        std::vector<int> valid;
        valid.reserve(children.size());
        for (int child : children) {
            if (child >= 0 && child < count && child != i) {
                valid.push_back(child);
            }
        }
        children = std::move(valid);
    }

    Galeshi::BehaviorTreeSystem::ResetRuntime(loaded);
    tree = std::move(loaded);
    return true;
}

} // namespace



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



    void GaleshiEditorApp::DrawBehaviorTreeEditor() {
        if (!m_showBehaviorTreeEditor) return;

        // Keep the tree next to the animation state machine editor; the tab
        // is a proper docked window instead of a Details section.
        if (ImGuiWindow* asme =
                ImGui::FindWindowByName("Animation State Machine Editor");
            asme && asme->DockId != 0) {
            ImGui::SetNextWindowDockID(asme->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* designer =
                       ImGui::FindWindowByName("UI Designer");
                   designer && designer->DockId != 0) {
            ImGui::SetNextWindowDockID(designer->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* output =
                       ImGui::FindWindowByName("Output Log");
                   output && output->DockId != 0) {
            ImGui::SetNextWindowDockID(output->DockId, ImGuiCond_Always);
        } else if (ImGuiWindow* browser =
                       ImGui::FindWindowByName("Content Browser");
                   browser && browser->DockId != 0) {
            ImGui::SetNextWindowDockID(browser->DockId, ImGuiCond_Always);
        }

        if (!ImGui::Begin("Behavior Tree Editor",
                          &m_showBehaviorTreeEditor)) {
            ImGui::End();
            return;
        }

        auto& registry = m_scene.Registry();
        auto treeView = registry.view<Galeshi::BehaviorTreeComponent>();
        if (treeView.begin() == treeView.end()) {
            ImGui::TextDisabled("No behavior tree agents in the scene.");
            ImGui::End();
            return;
        }

        std::vector<std::pair<std::string, entt::entity>> agents;
        agents.reserve(treeView.size());
        for (entt::entity entity : treeView) {
            std::string name =
                "Entity " + std::to_string(static_cast<uint32_t>(entity));
            if (const auto* nameComponent =
                    registry.try_get<Galeshi::NameComponent>(entity)) {
                if (!nameComponent->name.empty()) name = nameComponent->name;
            }
            agents.emplace_back(std::move(name), entity);
        }
        std::sort(agents.begin(), agents.end(),
                  [](const auto& a, const auto& b) {
                      if (a.first != b.first) return a.first < b.first;
                      return static_cast<uint32_t>(a.second) <
                             static_cast<uint32_t>(b.second);
                  });

        const bool targetValid =
            registry.valid(m_behaviorTreeEditorEntity) &&
            registry.all_of<Galeshi::BehaviorTreeComponent>(
                m_behaviorTreeEditorEntity);
        if (!targetValid) {
            m_behaviorTreeEditorEntity = agents.front().second;
        }

        int current = 0;
        std::vector<const char*> labels;
        labels.reserve(agents.size());
        for (int i = 0; i < static_cast<int>(agents.size()); ++i) {
            if (agents[static_cast<size_t>(i)].second ==
                m_behaviorTreeEditorEntity) {
                current = i;
            }
            labels.push_back(agents[static_cast<size_t>(i)].first.c_str());
        }

        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::Combo("##behavior_tree_agent", &current,
                         labels.data(), static_cast<int>(labels.size()))) {
            m_behaviorTreeEditorEntity =
                agents[static_cast<size_t>(current)].second;
        }
        if (ImGui::Button("Select in Scene")) {
            m_editor.Select(Galeshi::Entity(m_behaviorTreeEditorEntity,
                                            &m_scene));
        }

        const std::filesystem::path setupPath =
            BehaviorTreeSetupPathFor(m_behaviorTreeEditorEntity);
        const std::string setupLabel =
            setupPath.empty()
                ? std::string("(no project)")
                : (m_assets
                       ? m_assets->MakeRelativePath(setupPath.string())
                       : setupPath.string());
        ImGui::TextDisabled("BT setup: %s", setupLabel.c_str());
        if (!setupPath.empty()) {
            if (ImGui::Button("Save Setup")) {
                SaveBehaviorTreeSetup(m_behaviorTreeEditorEntity);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Save this behavior tree to:\n%s",
                                  setupPath.string().c_str());
            }
            ImGui::SameLine();
            if (ImGui::Button("Reload Setup")) {
                PushHistory();
                LoadBehaviorTreeSetup(m_behaviorTreeEditorEntity);
            }
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip("Reload this behavior tree from:\n%s",
                                  setupPath.string().c_str());
            }
        }
        ImGui::Separator();

        auto* tree = registry.try_get<Galeshi::BehaviorTreeComponent>(
            m_behaviorTreeEditorEntity);
        if (!tree) {
            ImGui::TextDisabled("No behavior tree on the selected agent.");
            ImGui::End();
            return;
        }

        ImGui::Checkbox("Enabled", &tree->enabled);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(100.0f);
        ImGui::DragFloat("Tick Interval", &tree->tickInterval,
                         0.01f, 0.0f, 2.0f);
        ImGui::Text("Status: %s (node %d)", tree->statusText.c_str(),
                    tree->runningNode);
        if (ImGui::Button("Reset Runtime")) {
            Galeshi::BehaviorTreeSystem::ResetRuntime(*tree);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Default Tree")) {
            PushHistory();
            *tree = Galeshi::BehaviorTreeSystem::MakeDefaultBehaviorTree();
            if (!registry.all_of<Galeshi::PerceptionComponent>(
                    m_behaviorTreeEditorEntity)) {
                registry.emplace<Galeshi::PerceptionComponent>(
                    m_behaviorTreeEditorEntity);
            }
            if (!registry.all_of<Galeshi::NavAgentComponent>(
                    m_behaviorTreeEditorEntity)) {
                registry.emplace<Galeshi::NavAgentComponent>(
                    m_behaviorTreeEditorEntity);
            }
        }

        const char* nodeTypeNames[] = {
            "Selector", "Sequence", "Condition", "Action"
        };
        const char* conditionNames[] = {
            "Always True", "Has Target", "Target Visible",
            "Target In Range", "Target Lost", "Parameter True"
        };
        const char* actionNames[] = {
            "Wait", "Set Target To Player", "Move To Target", "Patrol",
            "Attack", "Look At Target", "Set Animation State",
            "Clear Target", "Set Nav Speed"
        };

        std::function<void(int, int)> drawNode = [&](int index, int depth) {
            if (depth > 32 || index < 0 ||
                index >= static_cast<int>(tree->nodes.size())) {
                return;
            }
            auto& node = tree->nodes[static_cast<size_t>(index)];
            ImGui::PushID(index);
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                       ImGuiTreeNodeFlags_DefaultOpen;
            if (node.children.empty()) flags |= ImGuiTreeNodeFlags_Leaf;
            char label[256];
            std::snprintf(label, sizeof(label), "[%d] %s", index,
                          node.name.c_str());
            const bool open =
                ImGui::TreeNodeEx("##node", flags, "%s", label);
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)",
                                nodeTypeNames[static_cast<int>(node.type)]);

            char nodeName[128];
            std::snprintf(nodeName, sizeof(nodeName), "%s",
                          node.name.c_str());
            if (ImGui::InputText("Name", nodeName, sizeof(nodeName))) {
                node.name = nodeName;
            }
            int type = static_cast<int>(node.type);
            if (ImGui::Combo("Type", &type, nodeTypeNames,
                             IM_ARRAYSIZE(nodeTypeNames))) {
                node.type = static_cast<Galeshi::BehaviorNodeType>(type);
            }

            if (node.type == Galeshi::BehaviorNodeType::Condition) {
                int condition = static_cast<int>(node.condition);
                if (ImGui::Combo("Condition", &condition, conditionNames,
                                 IM_ARRAYSIZE(conditionNames))) {
                    node.condition =
                        static_cast<Galeshi::BehaviorCondition>(condition);
                }
                if (node.condition ==
                    Galeshi::BehaviorCondition::TargetInRange) {
                    ImGui::DragFloat("Range", &node.range,
                                     0.1f, 0.0f, 100.0f);
                }
                if (node.condition ==
                    Galeshi::BehaviorCondition::ParameterTrue) {
                    char parameter[128];
                    std::snprintf(parameter, sizeof(parameter), "%s",
                                  node.parameter.c_str());
                    if (ImGui::InputText("Parameter", parameter,
                                         sizeof(parameter))) {
                        node.parameter = parameter;
                    }
                }
            } else if (node.type == Galeshi::BehaviorNodeType::Action) {
                int action = static_cast<int>(node.action);
                if (ImGui::Combo("Action", &action, actionNames,
                                 IM_ARRAYSIZE(actionNames))) {
                    node.action =
                        static_cast<Galeshi::BehaviorAction>(action);
                }
                switch (node.action) {
                    case Galeshi::BehaviorAction::Wait:
                        ImGui::DragFloat("Duration", &node.duration,
                                         0.05f, 0.0f, 60.0f);
                        break;
                    case Galeshi::BehaviorAction::MoveToTarget:
                        ImGui::DragFloat("Arrive Range", &node.range,
                                         0.05f, 0.0f, 20.0f);
                        ImGui::DragFloat("Acceptance Radius",
                                         &node.acceptanceRadius,
                                         0.05f, 0.0f, 20.0f);
                        if (ImGui::IsItemHovered()) {
                            ImGui::SetTooltip(
                                "Distance at which the move counts as "
                                "arrived.\n0 = automatic (Arrive Range x 0.25).");
                        }
                        break;
                    case Galeshi::BehaviorAction::Patrol:
                        ImGui::DragFloat("Patrol Radius", &node.radius,
                                         0.25f, 0.5f, 200.0f);
                        ImGui::DragFloat("Wait Time", &node.duration,
                                         0.05f, 0.0f, 60.0f);
                        break;
                    case Galeshi::BehaviorAction::Attack:
                        ImGui::DragFloat("Attack Range", &node.range,
                                         0.05f, 0.0f, 20.0f);
                        ImGui::DragFloat("Cooldown", &node.duration,
                                         0.05f, 0.1f, 10.0f);
                        break;
                    case Galeshi::BehaviorAction::SetAnimationState: {
                        char state[128];
                        std::snprintf(state, sizeof(state), "%s",
                                      node.value.c_str());
                        if (ImGui::InputText("State", state,
                                             sizeof(state))) {
                            node.value = state;
                        }
                        break;
                    }
                    case Galeshi::BehaviorAction::SetNavSpeed: {
                        char speed[128];
                        std::snprintf(speed, sizeof(speed), "%s",
                                      node.value.c_str());
                        if (ImGui::InputText("Speed", speed,
                                             sizeof(speed))) {
                            node.value = speed;
                        }
                        break;
                    }
                    default:
                        break;
                }
            } else {
                ImGui::TextDisabled("Children: %zu",
                                    node.children.size());
            }

            if (open) {
                std::vector<int> children = node.children;
                for (int child : children) drawNode(child, depth + 1);
                ImGui::TreePop();
            }
            ImGui::PopID();
        };
        if (!tree->nodes.empty()) drawNode(tree->root, 0);

        ImGui::End();
    }



    std::filesystem::path GaleshiEditorApp::BehaviorTreeSetupPathFor(
        entt::entity agent) {
        const std::string projectPath =
            Galeshi::ProjectManager::GetProjectPath();
        if (projectPath.empty()) return {};
        return std::filesystem::path(projectPath) / "assets" / "scripts" /
               (BehaviorTreeFileStem(m_scene, agent) + ".bt");
    }



    bool GaleshiEditorApp::SaveBehaviorTreeSetup(entt::entity agent) {
        auto* tree = m_scene.Registry().try_get<
            Galeshi::BehaviorTreeComponent>(agent);
        if (!tree) {
            spdlog::warn("[BT] No behavior tree on the selected AI agent");
            return false;
        }

        const std::filesystem::path path =
            BehaviorTreeSetupPathFor(agent);
        if (path.empty()) {
            spdlog::warn("[BT] Cannot save setup: no project is open");
            return false;
        }

        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            spdlog::error("[BT] Cannot create setup directory '{}': {}",
                          path.parent_path().string(), ec.message());
            return false;
        }

        std::ofstream out(path);
        if (!out.is_open()) {
            spdlog::error("[BT] Cannot write setup '{}'", path.string());
            return false;
        }
        out << BehaviorTreeToJson(*tree).dump(4) << '\n';
        out.close();
        if (!out.good()) {
            spdlog::error("[BT] Failed while writing setup '{}'",
                          path.string());
            return false;
        }

        spdlog::info("[BT] Saved setup: {}", path.string());
        return true;
    }



    bool GaleshiEditorApp::LoadBehaviorTreeSetup(entt::entity agent) {
        auto* tree = m_scene.Registry().try_get<
            Galeshi::BehaviorTreeComponent>(agent);
        if (!tree) {
            spdlog::warn("[BT] No behavior tree on the selected AI agent");
            return false;
        }

        const std::filesystem::path path =
            BehaviorTreeSetupPathFor(agent);
        if (path.empty()) return false;

        std::error_code ec;
        if (!std::filesystem::is_regular_file(path, ec)) {
            spdlog::warn("[BT] Setup file not found: {}", path.string());
            return false;
        }

        std::ifstream in(path);
        if (!in.is_open()) {
            spdlog::error("[BT] Cannot read setup '{}'", path.string());
            return false;
        }

        nlohmann::json json;
        try {
            in >> json;
        } catch (const std::exception& e) {
            spdlog::error("[BT] Invalid setup '{}': {}", path.string(),
                          e.what());
            return false;
        }

        Galeshi::BehaviorTreeComponent loaded;
        if (!BehaviorTreeFromJson(json, loaded)) {
            spdlog::error("[BT] Setup '{}' has no valid nodes",
                          path.string());
            return false;
        }
        *tree = std::move(loaded);
        spdlog::info("[BT] Loaded setup: {}", path.string());
        return true;
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

