#include "GaleshiEditorApp.hpp"



    const char* const* GaleshiEditorApp::UIWidgetTypeNames() {
        static const char* names[] = {
            "None", "Panel", "Text", "Button", "Image", "Slider", "Checkbox",
            "Progress", "Input", "Dropdown", "Scroll List", "Toggle",
            "Layout Group"};
        return names;
    }



    const char* GaleshiEditorApp::UIWidgetTypeName(Galeshi::UIWidgetType type) {
        const int index = static_cast<int>(type);
        if (index < 0 || index > 12) return "None";
        return UIWidgetTypeNames()[index];
    }



    const char* GaleshiEditorApp::UIWidgetIdPrefix(Galeshi::UIWidgetType type) {
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



    glm::vec2 GaleshiEditorApp::DefaultUIWidgetSize(Galeshi::UIWidgetType type) {
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



    const char* GaleshiEditorApp::DefaultUIWidgetLabel(Galeshi::UIWidgetType type) {
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



    std::string GaleshiEditorApp::MakeUIWidgetId(const Galeshi::UICanvas& canvas,
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



    void GaleshiEditorApp::SetUIAnchorPreset(Galeshi::UIWidget& widget,
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



    int GaleshiEditorApp::UIWidgetDepth(const Galeshi::UICanvas& canvas,  size_t index) {
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



    bool GaleshiEditorApp::UIEventInput(const char* label,  std::string& value) {
        char buffer[256];
        std::snprintf(buffer, sizeof(buffer), "%s", value.c_str());
        if (ImGui::InputText(label, buffer, sizeof(buffer))) {
            value = buffer;
            return true;
        }
        return false;
    }



    void GaleshiEditorApp::DispatchUIEvent(const Galeshi::UIEvent& event) {
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



    entt::entity GaleshiEditorApp::ActiveUICanvasEntity() {
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



    Galeshi::UICanvas* GaleshiEditorApp::ActiveUICanvas() {
        const entt::entity entity = ActiveUICanvasEntity();
        if (entity == entt::null) return nullptr;
        return &m_scene.Registry().get<Galeshi::UICanvasComponent>(entity).canvas;
    }



    Galeshi::UIWidget& GaleshiEditorApp::AddUIWidget(Galeshi::UICanvas& canvas,
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



    void GaleshiEditorApp::AddDefaultUIWidget(Galeshi::UIWidgetType type) {
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



    void GaleshiEditorApp::ApplyUITemplate(Galeshi::UICanvas& canvas,  int preset) {
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



    void GaleshiEditorApp::CreateUICanvas(const char* name,  int preset) {
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



    void GaleshiEditorApp::DeleteUICanvas(entt::entity entity) {
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



    void GaleshiEditorApp::ReorderSelectedUIWidget(int mode) {
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



    void GaleshiEditorApp::DrawUILayers() {
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



    void GaleshiEditorApp::DrawUIDesigner() {
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



    void GaleshiEditorApp::DrawUICanvasProperties(Galeshi::UICanvas& canvas) {
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



    void GaleshiEditorApp::DrawUIWidgetProperties(Galeshi::UICanvas& canvas,
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



    bool GaleshiEditorApp::DrawUICanvasOverlay(Galeshi::UICanvas& canvas,  ImVec2 imageMin,
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



    void GaleshiEditorApp::DrawDockspace() {
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
    Galeshi::Entity GaleshiEditorApp::FindGameplayCamera() {
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

