#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/AI/BehaviorTree.hpp"
#include <spdlog/spdlog.h>

namespace Galeshi {

using namespace ScriptEngineInternal;

void ScriptEngine::BuildApiUI(ScriptApiBuildContext& context) {
    auto& readNumber = context.readNumber;
    auto& readBool = context.readBool;
    auto& readString = context.readString;
    auto& readVec2 = context.readVec2;
    auto& readVec3 = context.readVec3;
    auto& readVec4 = context.readVec4;
    auto& readQuat = context.readQuat;
    auto& readVec3Args = context.readVec3Args;
    auto& makeVec2 = context.makeVec2;
    auto& makeVec3 = context.makeVec3;
    auto& makeVec4 = context.makeVec4;
    auto& makeQuat = context.makeQuat;
    auto& validId = context.validId;
    auto& idFromValue = context.idFromValue;
    auto& resolve = context.resolve;
    auto& idFromTable = context.idFromTable;
    auto& resolveTable = context.resolveTable;
    auto& makeEntityTable = context.makeEntityTable;
    auto& makeEntity = context.makeEntity;
    auto& getTransform = context.getTransform;
    auto& world = context.world;
    auto& input = context.input;
    auto& physics = context.physics;
    auto& audio = context.audio;
    auto& animation = context.animation;
    auto& ai = context.ai;
    auto& navigation = context.navigation;
    auto& ui = context.ui;
    auto& particles = context.particles;
    auto& engine = context.engine;
    auto& camera = context.camera;
    auto& player = context.player;
    auto& shader = context.shader;

    // ------------------------------------------------------------------
    // AI and navigation (Detour/Recast through host callbacks).
    // ------------------------------------------------------------------
    auto navAgent = [this, resolve](sol::object value) -> NavAgentComponent* {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return nullptr;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<NavAgentComponent>(entity)) return nullptr;
        return &registry.get<NavAgentComponent>(entity);
    };

    const auto setAITarget = [navAgent, readVec3Args](
                                 sol::object value, sol::variadic_args args) {
        NavAgentComponent* agent = navAgent(value);
        glm::vec3 target{0.0f};
        if (!agent || !readVec3Args(args, target)) return false;
        agent->targetPosition = target;
        agent->hasTarget = true;
        return true;
    };
    const auto isPathDone = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        return !agent || agent->path.empty();
    };
    const auto setAISpeed = [navAgent](sol::object value, float speed) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->speed = speed;
        return true;
    };
    const auto getAISpeed = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        return agent ? agent->speed : 0.0f;
    };
    const auto clearAITarget = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->hasTarget = false;
        agent->path.clear();
        agent->currentWaypoint = 0;
        return true;
    };
    const auto setNavStoppingDistance = [navAgent](sol::object value,
                                                    float distance) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->stoppingDistance = std::max(distance, 0.0f);
        return true;
    };

    ai["set_target"] = setAITarget;
    ai["path_done"] = isPathDone;
    ai["is_path_done"] = isPathDone;
    ai["set_speed"] = setAISpeed;
    ai["speed"] = getAISpeed;
    ai["clear_target"] = clearAITarget;
    ai["set_stopping_distance"] = setNavStoppingDistance;
    m_lua.set_function("set_ai_target", setAITarget);
    m_lua.set_function("is_path_done", isPathDone);
    m_lua.set_function("set_ai_speed", setAISpeed);

    navigation["find_path"] = [this, readVec3, makeVec3](
                                  sol::object startValue,
                                  sol::object endValue) -> sol::object {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        if (!m_findPath || !readVec3(startValue, start) ||
            !readVec3(endValue, end)) {
            return sol::make_object(m_lua, sol::nil);
        }
        std::vector<glm::vec3> path;
        if (!m_findPath(start, end, path)) {
            return sol::make_object(m_lua, sol::nil);
        }
        sol::table result = m_lua.create_table();
        int index = 1;
        for (const glm::vec3& point : path) {
            result[index++] = makeVec3(point);
        }
        return sol::make_object(m_lua, result);
    };
    navigation["build"] = [this]() {
        return m_buildNavMesh ? m_buildNavMesh() : false;
    };

    // ------------------------------------------------------------------
    // UI runtime access and event handler registration.
    // ------------------------------------------------------------------
    auto findCanvas = [this](const std::string& id) -> std::pair<UICanvas*, entt::entity> {
        if (!m_scene) return {nullptr, entt::null};
        auto view = m_scene->Registry().view<UICanvasComponent>();
        for (entt::entity entity : view) {
            UICanvas& canvas = view.get<UICanvasComponent>(entity).canvas;
            if (id.empty() || canvas.canvasId == id) return {&canvas, entity};
        }
        return {nullptr, entt::null};
    };

    auto findWidget = [](UICanvas& canvas, const std::string& id) -> UIWidget* {
        for (UIWidget& widget : canvas.widgets) {
            if (widget.id == id) return &widget;
        }
        return nullptr;
    };

    auto widgetToTable = [this, makeVec2, makeVec4](const UIWidget& widget) {
        sol::table table = m_lua.create_table();
        table["id"] = widget.id;
        table["label"] = widget.label;
        table["type"] = WidgetTypeName(widget.type);
        table["visible"] = widget.visible;
        table["enabled"] = widget.enabled;
        table["interactable"] = widget.interactable;
        table["opacity"] = widget.opacity;
        table["position"] = makeVec2(widget.position);
        table["size"] = makeVec2(widget.size);
        table["value"] = widget.sliderValue;
        table["slider_step"] = widget.sliderStep;
        table["checked"] = widget.checked;
        table["font_size"] = widget.fontSize;
        table["font"] = widget.fontPath;
        table["wrap_width"] = widget.textWrapWidth;
        table["text_offset"] = makeVec2(widget.textOffset);
        table["rotation"] = widget.rotation;
        table["text_align"] =
            widget.textAlign == UITextAlign::Center ? "center"
            : widget.textAlign == UITextAlign::Right ? "right"
                                                     : "left";
        table["use_color"] = widget.useColor;
        table["color"] = makeVec4(widget.color);
        table["use_text_color"] = widget.useTextColor;
        table["text_color"] = makeVec4(widget.textColor);
        table["use_border_color"] = widget.useBorderColor;
        table["border_color"] = makeVec4(widget.borderColor);
        table["tooltip"] = widget.tooltip;
        table["image"] = widget.imagePath;
        table["selected_option"] = widget.selectedOption;
        sol::table options = m_lua.create_table();
        for (size_t i = 0; i < widget.options.size(); ++i) {
            options[i + 1] = widget.options[i];
        }
        table["options"] = options;
        table["on_click"] = widget.onClick;
        table["on_hover"] = widget.onHover;
        table["on_show"] = widget.onShow;
        return table;
    };

    auto applyWidgetProps = [readNumber, readBool, readString, readVec2,
                             readVec4](UIWidget& widget,
                                       const sol::table& props) {
        std::string text;
        bool boolean = false;
        float number = 0.0f;
        glm::vec2 vector2{0.0f};
        glm::vec4 color{1.0f};

        if (readString(props.get<sol::object>("label"), text)) widget.label = text;
        if (readBool(props.get<sol::object>("visible"), boolean)) widget.visible = boolean;
        if (readBool(props.get<sol::object>("enabled"), boolean)) widget.enabled = boolean;
        if (readBool(props.get<sol::object>("interactable"), boolean)) {
            widget.interactable = boolean;
        }
        if (readNumber(props.get<sol::object>("opacity"), number)) {
            widget.opacity = std::clamp(number, 0.0f, 1.0f);
        }
        if (readNumber(props.get<sol::object>("value"), number)) widget.sliderValue = number;
        if (readNumber(props.get<sol::object>("slider_step"), number)) {
            widget.sliderStep = std::max(0.0f, number);
        }
        if (readBool(props.get<sol::object>("checked"), boolean)) widget.checked = boolean;
        if (readNumber(props.get<sol::object>("font_size"), number)) {
            widget.fontSize = std::max(0.0f, number);
        }
        if (readString(props.get<sol::object>("font"), text)) {
            widget.fontPath = text;
        }
        if (readNumber(props.get<sol::object>("wrap_width"), number)) {
            widget.textWrapWidth = std::max(0.0f, number);
        }
        if (readNumber(props.get<sol::object>("rotation"), number)) {
            widget.rotation = number;
        }
        if (readVec2(props.get<sol::object>("text_offset"), vector2)) {
            widget.textOffset = vector2;
        }
        if (readString(props.get<sol::object>("text_align"), text)) {
            if (text == "center") widget.textAlign = UITextAlign::Center;
            else if (text == "right") widget.textAlign = UITextAlign::Right;
            else widget.textAlign = UITextAlign::Left;
        }
        if (readNumber(props.get<sol::object>("selected_option"), number)) {
            widget.selectedOption = static_cast<int>(number);
        }
        if (readString(props.get<sol::object>("tooltip"), text)) {
            widget.tooltip = text;
        }
        if (readString(props.get<sol::object>("image"), text)) widget.imagePath = text;
        if (readString(props.get<sol::object>("on_click"), text)) widget.onClick = text;
        if (readString(props.get<sol::object>("on_hover"), text)) widget.onHover = text;
        if (readString(props.get<sol::object>("on_show"), text)) widget.onShow = text;
        const sol::object options =
            props.get<sol::object>("options");
        if (options.get_type() == sol::type::table) {
            widget.options.clear();
            const sol::table table = options.as<sol::table>();
            for (size_t i = 1; i <= table.size(); ++i) {
                const sol::object value = table[i];
                if (value.get_type() == sol::type::string) {
                    widget.options.push_back(value.as<std::string>());
                }
            }
        }
        if (readBool(props.get<sol::object>("use_color"), boolean)) {
            widget.useColor = boolean;
        }
        if (readBool(props.get<sol::object>("use_text_color"), boolean)) {
            widget.useTextColor = boolean;
        }
        if (readBool(props.get<sol::object>("use_border_color"), boolean)) {
            widget.useBorderColor = boolean;
        }
        if (readVec4(props.get<sol::object>("text_color"), color)) {
            widget.textColor = color;
        }
        if (readVec4(props.get<sol::object>("border_color"), color)) {
            widget.borderColor = color;
        }
        if (readNumber(props.get<sol::object>("z_order"), number)) {
            widget.zOrder = static_cast<int>(number);
        }
        if (readNumber(props.get<sol::object>("parent"), number)) {
            widget.parent = static_cast<int>(number);
        }
        if (readVec2(props.get<sol::object>("position"), vector2)) widget.position = vector2;
        if (readVec2(props.get<sol::object>("size"), vector2)) widget.size = vector2;
        if (readVec4(props.get<sol::object>("color"), color)) widget.color = color;
    };

    ui["find_canvas"] = [this, findCanvas, makeEntity](const std::string& id) {
        const auto [canvas, entity] = findCanvas(id);
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        return makeEntity(static_cast<uint32_t>(entity));
    };
    ui["canvases"] = [this, findCanvas]() {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<UICanvasComponent>();
        for (entt::entity entity : view) {
            const UICanvas& canvas = view.get<UICanvasComponent>(entity).canvas;
            sol::table entry = m_lua.create_table();
            entry["entity"] = static_cast<uint32_t>(entity);
            entry["id"] = canvas.canvasId;
            entry["title"] = canvas.title;
            entry["visible"] = canvas.visible;
            entry["widgets"] = static_cast<int>(canvas.widgets.size());
            result[index++] = entry;
        }
        return result;
    };
    ui["canvas"] = [this, findCanvas](const std::string& id) -> sol::object {
        const auto [canvas, entity] = findCanvas(id);
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        sol::table table = m_lua.create_table();
        table["entity"] = static_cast<uint32_t>(entity);
        table["id"] = canvas->canvasId;
        table["title"] = canvas->title;
        table["visible"] = canvas->visible;
        table["widgets"] = static_cast<int>(canvas->widgets.size());
        return sol::make_object(m_lua, table);
    };
    ui["widget"] = [this, findCanvas, findWidget, widgetToTable](
                       const std::string& canvasId,
                       const std::string& widgetId) -> sol::object {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return sol::make_object(m_lua, sol::nil);
        return sol::make_object(m_lua, widgetToTable(*widget));
    };
    ui["set_widget"] = [findCanvas, findWidget, applyWidgetProps](
                           const std::string& canvasId,
                           const std::string& widgetId,
                           sol::table props) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return false;
        applyWidgetProps(*widget, props);
        return true;
    };
    ui["set_value"] = [findCanvas, findWidget](const std::string& canvasId,
                                               const std::string& widgetId,
                                               float value) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return false;
        widget->sliderValue = value;
        return true;
    };
    ui["value"] = [findCanvas, findWidget](const std::string& canvasId,
                                           const std::string& widgetId) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return 0.0f;
        UIWidget* widget = findWidget(*canvas, widgetId);
        return widget ? widget->sliderValue : 0.0f;
    };
    ui["show_canvas"] = [findCanvas](const std::string& canvasId, bool visible) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        canvas->visible = visible;
        return true;
    };
    ui["on"] = [this](const std::string& name, sol::protected_function function) {
        if (!function.valid()) return false;
        m_uiHandlers[name] = function;
        return true;
    };
    ui["off"] = [this](const std::string& name) {
        return m_uiHandlers.erase(name) > 0;
    };
    ui["call"] = [this](const std::string& name,
                        sol::optional<std::string> widgetId,
                        sol::optional<float> value,
                        sol::optional<bool> checked) {
        return CallUIHandler(name, widgetId.value_or(""), value.value_or(0.0f),
                             checked.value_or(false));
    };


}

} // namespace Galeshi
