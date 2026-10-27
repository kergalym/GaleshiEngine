#pragma once

#include <cstdint>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Galeshi {

// ---------------------------------------------------------------------------
// UI widget data model
// ---------------------------------------------------------------------------

enum class UIWidgetType : uint8_t {
    None = 0,
    Panel,
    Text,
    Button,
    Image,
    Slider,
    CheckBox,
    Progress,
    Input,
    Dropdown,
    ScrollList,
    Toggle,
    LayoutGroup,
};

enum class UIAnchorPreset : uint8_t {
    TopLeft = 0,
    TopCenter,
    TopRight,
    MiddleLeft,
    MiddleCenter,
    MiddleRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    StretchHorizontal,
    StretchVertical,
    StretchAll,
};

enum class UICanvasRenderMode : uint8_t {
    ScreenSpaceOverlay = 0,
    ScreenSpaceCamera,
    WorldSpace,
};

enum class UICanvasScaleMode : uint8_t {
    ScaleWithScreenSize = 0,
    ConstantPixelSize,
};

struct UIWidget {
    // Identity
    std::string id    = "widget";
    std::string label = "Widget";

    UIWidgetType type = UIWidgetType::None;

    // Hierarchy: index into UICanvas::widgets, -1 = direct canvas child.
    int parent = -1;

    // Draw order inside the canvas; higher values are drawn on top.
    int zOrder = 0;

    // Canvas-relative layout (pixels)
    glm::vec2 position{0.0f};
    glm::vec2 size{0.0f};

    // Anchors (0..1, relative to the parent canvas rect)
    UIAnchorPreset anchorPreset = UIAnchorPreset::TopLeft;
    glm::vec2 anchorMin{0.0f, 0.0f};
    glm::vec2 anchorMax{0.0f, 0.0f};
    glm::vec2 pivot{0.0f, 0.0f};

    // Common properties
    bool  visible     = true;
    bool  enabled     = true;
    bool  interactable = true;
    bool  blockInput  = true;
    bool  locked      = false; // editor-only: lock selection in the designer
    float opacity     = 1.0f;

    // Button
    float buttonHeight = 30.0f;

    // Slider / Progress (Progress stores a 0..1 value in sliderValue)
    float sliderMin   = 0.0f;
    float sliderMax   = 1.0f;
    float sliderValue = 0.5f;

    // Checkbox / Toggle
    bool checked = false;

    // Input
    int maxChars = 128;

    // Image
    std::string imagePath;

    // Style
    bool      useColor = false;
    glm::vec4 color{1.0f};   // RGBA 0..1
    float     radius = 0.0f;
    float     border = 0.0f;

    // Lua event handlers
    std::string onShow;
    std::string onHover;
    std::string onClick;
};

// ---------------------------------------------------------------------------
// UI canvas – a positioned group of widgets that renders as one window
// ---------------------------------------------------------------------------

struct UICanvas {
    // Identity
    std::string canvasId = "Canvas";
    std::string title    = "UI";

    // Screen position (absolute, pixels) and size
    glm::vec2 screenPosition{0.0f};
    glm::vec2 screenSize{800.0f, 600.0f};

    // Behavior flags
    bool resizable        = true;
    bool movable          = true;
    bool closeable        = true;
    bool visible          = true;
    float backgroundAlpha = 1.0f;

    // Canvas settings
    UICanvasRenderMode renderMode = UICanvasRenderMode::ScreenSpaceOverlay;
    glm::ivec2 referenceResolution{1920, 1080};
    UICanvasScaleMode scaleMode = UICanvasScaleMode::ScaleWithScreenSize;
    float matchWidthOrHeight = 0.5f;
    bool pixelPerfect = false;
    int sortOrder = 0;

    // Canvas-level Lua events
    std::string onMenuOpen;
    std::string onMenuHover;
    std::string onMenuClick;

    // Widgets drawn inside this canvas
    std::vector<UIWidget> widgets;
};

} // namespace Galeshi
