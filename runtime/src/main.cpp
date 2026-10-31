#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/UI/UIRuntime.hpp"

#include <imgui.h>
#include <spdlog/spdlog.h>

#include <memory>

// Minimal runtime demo: builds an in-game main menu from UI widgets, renders
// it every frame through UIRuntime, and forwards widget events to Lua.
class GaleshiRuntimeApp final : public Galeshi::Application {
public:
    GaleshiRuntimeApp()
        : Galeshi::Application({
              .name = "GaleshiRuntime", .width = 1280, .height = 720, .editor = false}) {}

protected:
    void OnStart() override {
        m_scripts.BindFunctions();
        m_scripts.BindECS(m_scene);
        m_assets = std::make_unique<Galeshi::AssetManager>();
        m_scripts.BindAssets(m_assets.get());
        m_scripts.BindEngine([this]() { RequestExit(); });
        if (!m_scripts.RunScript("assets/scripts/ui_menu.lua")) {
            spdlog::warn("[UI] ui_menu.lua not found; events will only be logged");
        }

        m_ui.SetEventHandler([this](const Galeshi::UIEvent& event) {
            DispatchUIEvent(event);
        });

        Galeshi::Entity entity = m_scene.CreateEntity("MainMenu");
        auto& canvas = entity.AddComponent<Galeshi::UICanvasComponent>().canvas;

        canvas.canvasId = "MainMenu";
        canvas.title = "Main Menu";
        canvas.referenceResolution = {1920, 1080};
        canvas.screenSize = {1920.0f, 1080.0f};
        canvas.backgroundAlpha = 0.0f;
        canvas.resizable = false;
        canvas.closeable = false;
        canvas.onMenuOpen = "on_menu_open";
        canvas.onMenuClick = "on_menu_open";

        auto addWidget = [&canvas](Galeshi::UIWidget widget) {
            widget.zOrder = static_cast<int>(canvas.widgets.size());
            canvas.widgets.push_back(std::move(widget));
        };

        Galeshi::UIWidget panel;
        panel.id = "panel";
        panel.type = Galeshi::UIWidgetType::Panel;
        panel.position = {700.0f, 150.0f};
        panel.size = {520.0f, 780.0f};
        panel.useColor = true;
        panel.color = {18.0f / 255.0f, 30.0f / 255.0f, 48.0f / 255.0f, 0.94f};
        panel.radius = 12.0f;
        panel.border = 2.0f;
        addWidget(panel);

        Galeshi::UIWidget title;
        title.id = "title";
        title.type = Galeshi::UIWidgetType::Text;
        title.label = "Galeshi Engine";
        title.position = {760.0f, 210.0f};
        title.size = {400.0f, 48.0f};
        addWidget(title);

        Galeshi::UIWidget volume;
        volume.id = "volume";
        volume.type = Galeshi::UIWidgetType::Slider;
        volume.label = "Volume";
        volume.position = {760.0f, 300.0f};
        volume.size = {400.0f, 0.0f};
        volume.sliderValue = 0.8f;
        volume.onClick = "on_volume_changed";
        addWidget(volume);

        Galeshi::UIWidget fullscreen;
        fullscreen.id = "fullscreen";
        fullscreen.type = Galeshi::UIWidgetType::CheckBox;
        fullscreen.label = "Fullscreen";
        fullscreen.position = {760.0f, 350.0f};
        fullscreen.onClick = "on_fullscreen_toggled";
        addWidget(fullscreen);

        const char* buttons[] = {"New Game", "Continue", "Settings", "Quit"};
        for (int i = 0; i < 4; ++i) {
            Galeshi::UIWidget button;
            button.id = std::string("button_") + std::to_string(i);
            button.type = Galeshi::UIWidgetType::Button;
            button.label = buttons[i];
            button.position = {760.0f, 430.0f + static_cast<float>(i) * 72.0f};
            button.size = {400.0f, 56.0f};
            button.buttonHeight = 56.0f;
            button.useColor = true;
            button.color = {37.0f / 255.0f, 99.0f / 255.0f, 170.0f / 255.0f, 1.0f};
            button.radius = 6.0f;
            button.onClick = "on_play_clicked";
            addWidget(button);
        }
    }

    void OnImGui() override { m_ui.Render(m_scene); }

private:
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

        spdlog::info("[UI] {} on '{}' (handler '{}')",
                     event.type == Galeshi::UIEvent::Type::Clicked ? "clicked"
                     : event.type == Galeshi::UIEvent::Type::Toggled ? "toggled"
                     : event.type == Galeshi::UIEvent::Type::Shown ? "shown"
                     : event.type == Galeshi::UIEvent::Type::Hover ? "hover"
                                                                    : "changed",
                     event.widgetId, handler);

        m_scripts.CallUIHandler(handler, event.widgetId, event.value,
                                event.checked);
    }

    Galeshi::Scene m_scene;
    std::unique_ptr<Galeshi::AssetManager> m_assets;
    Galeshi::ScriptEngine m_scripts;
    Galeshi::UIRuntime m_ui;
};

int main(int, char**) {
    GaleshiRuntimeApp app;
    return app.Run();
}
