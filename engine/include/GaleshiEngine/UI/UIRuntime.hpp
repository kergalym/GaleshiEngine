#pragma once

#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace Galeshi {

class Scene;
struct UICanvas;

// A widget interaction emitted by UIRuntime during a frame.  The application
// (or the Lua bridge) consumes these to react to clicks and value changes.
struct UIEvent {
    enum class Type {
        Clicked,
        ValueChanged,
        Toggled,
        Shown,
        Hover,
    };

    Type        type = Type::Clicked;
    int         canvasEntity = -1;
    std::string canvasId;
    std::string widgetId;
    float       value = 0.0f;
    bool        checked = false;
};

// Renders every entity that owns a UICanvasComponent using ImGui and collects
// the interactions that happened this frame.  Call it from Application::OnImGui
// while an ImGui frame is open (i.e. after ImGui::NewFrame()).
class UIRuntime {
public:
    // Draws all canvases in the scene.  Events from the current frame replace
    // the ones stored by the previous call; poll them with Events().
    void Render(Scene& scene);

    // Draws all canvases into an explicit 2D rectangle (the editor viewport or
    // a game viewport).  Widget positions are interpreted in canvas
    // reference-resolution pixels and scaled to fit the rectangle.
    void RenderInRect(Scene& scene, float x, float y, float width, float height);

    // Draws a single canvas into a 2D rectangle.  Used by the UI Designer.
    void RenderCanvasInRect(UICanvas& canvas, int canvasEntity,
                            float x, float y, float width, float height,
                            const char* windowSuffix = "##canvas2d");

    const std::vector<UIEvent>& Events() const { return m_events; }

    // Convenience hook for script bindings.  Called for every event directly
    // after it is produced.
    void SetEventHandler(std::function<void(const UIEvent&)> handler) {
        m_handler = std::move(handler);
    }

private:
    void Emit(UIEvent event);

    std::vector<UIEvent> m_events;
    std::function<void(const UIEvent&)> m_handler;
    // Stable id -> text buffer for InputText widgets.  Keyed per widget so the
    // buffer survives frames and does not depend on vector reallocation.
    std::vector<std::pair<std::string, std::string>> m_textBuffers;
    std::string& TextBuffer(const std::string& key);

    // Visibility/hover tracking used to emit onShow and onHover once per entry.
    std::unordered_map<std::string, bool> m_canvasVisible;
    std::unordered_set<std::string> m_hoveredWidgets;

    // Set while rendering so nested previews keep unique ImGui ids.
    int m_canvasDepth = 0;
};

} // namespace Galeshi
