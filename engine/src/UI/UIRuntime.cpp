#include "GaleshiEngine/UI/UIRuntime.hpp"
#include "GaleshiEngine/UI/UIWidgets.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numeric>

namespace Galeshi {

namespace {

bool EffectivelyVisible(const UICanvas& canvas, size_t index) {
    const UIWidget* widget = &canvas.widgets[index];
    int guard = 0;
    while (widget) {
        if (!widget->visible) return false;
        if (widget->parent < 0 ||
            widget->parent >= static_cast<int>(canvas.widgets.size()) ||
            ++guard > 64) {
            break;
        }
        widget = &canvas.widgets[static_cast<size_t>(widget->parent)];
    }
    return true;
}

ImU32 WidgetColor(const UIWidget& widget, float alpha) {
    const glm::vec4& c = widget.color;
    return ImGui::ColorConvertFloat4ToU32(
        ImVec4(c.r, c.g, c.b, std::clamp(c.a * alpha, 0.0f, 1.0f)));
}

} // namespace

std::string& UIRuntime::TextBuffer(const std::string& key) {
    for (auto& entry : m_textBuffers) {
        if (entry.first == key) return entry.second;
    }
    m_textBuffers.emplace_back(key, std::string{});
    return m_textBuffers.back().second;
}

void UIRuntime::Emit(UIEvent event) {
    if (m_handler) m_handler(event);
    m_events.push_back(std::move(event));
}

void UIRuntime::Render(Scene& scene) {
    const ImGuiIO& io = ImGui::GetIO();
    RenderInRect(scene, 0.0f, 0.0f, io.DisplaySize.x, io.DisplaySize.y);
}

void UIRuntime::RenderInRect(Scene& scene, float x, float y, float width,
                             float height) {
    m_events.clear();

    auto& registry = scene.Registry();
    auto view = registry.view<UICanvasComponent>();

    std::vector<entt::entity> canvases(view.begin(), view.end());
    std::stable_sort(canvases.begin(), canvases.end(),
                     [&](entt::entity a, entt::entity b) {
                         return registry.get<UICanvasComponent>(a).canvas.sortOrder <
                                registry.get<UICanvasComponent>(b).canvas.sortOrder;
                     });

    for (auto handle : canvases) {
        auto& component = registry.get<UICanvasComponent>(handle);
        auto& canvas = component.canvas;
        const std::string visibilityKey =
            std::to_string(static_cast<int>(handle));
        const bool wasVisible = m_canvasVisible[visibilityKey];

        if (!canvas.visible) {
            m_canvasVisible[visibilityKey] = false;
            continue;
        }

        if (!wasVisible) {
            Emit({UIEvent::Type::Shown, static_cast<int>(handle),
                  canvas.canvasId, std::string{}, 0.0f, false});
            for (const auto& widget : canvas.widgets) {
                if (!widget.onShow.empty()) {
                    Emit({UIEvent::Type::Shown, static_cast<int>(handle),
                          canvas.canvasId, widget.id, 0.0f, false});
                }
            }
        }
        m_canvasVisible[visibilityKey] = true;

        RenderCanvasInRect(canvas, static_cast<int>(handle), x, y, width, height,
                           canvas.canvasId.c_str());
    }
}

void UIRuntime::RenderCanvasInRect(UICanvas& canvas, int canvasEntity,
                                   float x, float y, float width, float height,
                                   const char* windowSuffix) {
    if (!canvas.visible || width < 2.0f || height < 2.0f) return;

    const glm::vec2 reference(std::max(1.0f, static_cast<float>(canvas.referenceResolution.x)),
                              std::max(1.0f, static_cast<float>(canvas.referenceResolution.y)));

    float scaleX = width / reference.x;
    float scaleY = height / reference.y;
    // The UI canvas must fit completely inside the destination rectangle.
    // (matchWidthOrHeight is kept for future full-screen crop modes; using the
    // fit scale here prevents authored UI from spilling over editor panels or
    // outside a game viewport with a non-reference aspect ratio.)
    float scale = std::min(scaleX, scaleY);
    (void)canvas.matchWidthOrHeight;
    if (canvas.scaleMode == UICanvasScaleMode::ConstantPixelSize) scale = 1.0f;
    scale = std::max(scale, 0.01f);

    const ImVec2 pixelSize(reference.x * scale, reference.y * scale);
    ImVec2 windowPos(x + (width - pixelSize.x) * 0.5f,
                     y + (height - pixelSize.y) * 0.5f);
    if (canvas.pixelPerfect) {
        windowPos.x = std::floor(windowPos.x + 0.5f);
        windowPos.y = std::floor(windowPos.y + 0.5f);
    }

    ImGui::SetNextWindowPos(windowPos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(pixelSize, ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBackground |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoScrollWithMouse;

    const std::string id = std::string("##") + (windowSuffix ? windowSuffix : "canvas2d");
    if (!ImGui::Begin(id.c_str(), nullptr, flags)) {
        ImGui::End();
        ImGui::PopStyleVar(3);
        return;
    }

    ImGui::SetWindowFontScale(std::clamp(scale, 0.35f, 4.0f));
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    const ImGuiStyle& style = ImGui::GetStyle();

    if (canvas.backgroundAlpha > 0.0f) {
        drawList->AddRectFilled(
            origin, ImVec2(origin.x + pixelSize.x, origin.y + pixelSize.y),
            IM_COL32(8, 18, 32, static_cast<int>(
                std::clamp(canvas.backgroundAlpha, 0.0f, 1.0f) * 255.0f)));
    }

    std::vector<size_t> drawOrder(canvas.widgets.size());
    std::iota(drawOrder.begin(), drawOrder.end(), size_t{0});
    std::stable_sort(drawOrder.begin(), drawOrder.end(),
                     [&](size_t a, size_t b) {
                         return canvas.widgets[a].zOrder <
                                canvas.widgets[b].zOrder;
                     });

    for (size_t i : drawOrder) {
        UIWidget& widget = canvas.widgets[i];
        if (!EffectivelyVisible(canvas, i)) continue;

        ImGui::PushID(static_cast<int>(i));
        const ImVec2 pos(origin.x + widget.position.x * scale,
                         origin.y + widget.position.y * scale);
        ImGui::SetCursorScreenPos(pos);

        const float widthPx = widget.size.x > 0.0f
                                  ? widget.size.x * scale
                                  : std::max(80.0f, ImGui::GetContentRegionAvail().x -
                                                        widget.position.x * scale - style.WindowPadding.x);
        const float alpha = std::clamp(widget.opacity, 0.0f, 1.0f);
        const float radiusPx = widget.radius * scale;
        const float borderPx = widget.border * scale;

        ImGui::BeginDisabled(!widget.enabled);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha * ImGui::GetStyle().Alpha);
        if (widget.useColor) {
            const ImVec4 tint(widget.color.r, widget.color.g, widget.color.b,
                              widget.color.a);
            ImGui::PushStyleColor(ImGuiCol_Button, tint);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(tint.x * 1.15f, tint.y * 1.15f,
                                         tint.z * 1.15f, tint.w));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(tint.x * 0.85f, tint.y * 0.85f,
                                         tint.z * 0.85f, tint.w));
        }

        switch (widget.type) {
        case UIWidgetType::Panel: {
            const ImVec2 size(widthPx, widget.size.y > 0.0f
                                          ? widget.size.y * scale
                                          : 120.0f * scale);
            const ImU32 fill = widget.useColor ? WidgetColor(widget, alpha)
                                               : IM_COL32(18, 30, 48, 222);
            drawList->AddRectFilled(pos,
                                    ImVec2(pos.x + size.x, pos.y + size.y),
                                    fill, radiusPx);
            if (widget.border > 0.0f) {
                drawList->AddRect(pos,
                                  ImVec2(pos.x + size.x, pos.y + size.y),
                                  widget.useColor
                                      ? WidgetColor(widget, alpha)
                                      : IM_COL32(90, 140, 200, 160),
                                  radiusPx, 0, borderPx);
            }
            ImGui::Dummy(size);
            break;
        }

        case UIWidgetType::Text: {
            const float heightPx = widget.size.y > 0.0f
                                       ? widget.size.y * scale
                                       : ImGui::GetTextLineHeight();
            const float fontSize =
                std::clamp(widget.size.y * scale * 0.62f, 9.0f, 48.0f);
            const ImU32 color =
                widget.useColor ? WidgetColor(widget, alpha)
                                : IM_COL32(225, 233, 245, 255);
            drawList->AddText(ImGui::GetFont(), fontSize, pos, color,
                              widget.label.c_str());
            ImGui::Dummy(ImVec2(widthPx, heightPx));
            break;
        }

        case UIWidgetType::Button: {
            const float height = (widget.buttonHeight > 0.0f ? widget.buttonHeight
                                                             : 30.0f) * scale;
            if (ImGui::Button(widget.label.c_str(), ImVec2(widthPx, height))) {
                Emit({UIEvent::Type::Clicked, canvasEntity, canvas.canvasId,
                      widget.id, 0.0f, false});
            }
            break;
        }

        case UIWidgetType::CheckBox:
        case UIWidgetType::Toggle:
            if (ImGui::Checkbox(widget.label.c_str(), &widget.checked)) {
                Emit({UIEvent::Type::Toggled, canvasEntity, canvas.canvasId,
                      widget.id, 0.0f, widget.checked});
            }
            break;

        case UIWidgetType::Slider: {
            if (!widget.label.empty()) {
                const float fontSize = std::max(9.0f, ImGui::GetFontSize() * 0.9f);
                drawList->AddText(ImGui::GetFont(), fontSize,
                                  ImVec2(pos.x, pos.y - fontSize - 2.0f),
                                  widget.useColor ? WidgetColor(widget, alpha)
                                                  : IM_COL32(220, 226, 236, 255),
                                  widget.label.c_str());
            }
            ImGui::SetNextItemWidth(widthPx);
            if (ImGui::SliderFloat("##value", &widget.sliderValue,
                                   widget.sliderMin, widget.sliderMax)) {
                Emit({UIEvent::Type::ValueChanged, canvasEntity, canvas.canvasId,
                      widget.id, widget.sliderValue, false});
            }
            break;
        }

        case UIWidgetType::Progress: {
            const ImVec2 size(widthPx, (widget.buttonHeight > 0.0f
                                            ? widget.buttonHeight
                                            : 18.0f) * scale);
            ImGui::ProgressBar(std::clamp(widget.sliderValue, 0.0f, 1.0f), size,
                               widget.label.empty() ? nullptr
                                                    : widget.label.c_str());
            break;
        }

        case UIWidgetType::Input: {
            std::string& buffer =
                TextBuffer(std::to_string(canvasEntity) + "/" + canvas.canvasId +
                           "/" + widget.id);
            if (buffer.size() > 511) buffer.resize(511);
            char temp[512] = {};
            std::snprintf(temp, sizeof(temp), "%s", buffer.c_str());
            if (!widget.label.empty()) {
                const float fontSize = std::max(9.0f, ImGui::GetFontSize() * 0.9f);
                drawList->AddText(ImGui::GetFont(), fontSize,
                                  ImVec2(pos.x, pos.y - fontSize - 2.0f),
                                  IM_COL32(220, 226, 236, 255),
                                  widget.label.c_str());
            }
            ImGui::SetNextItemWidth(widthPx);
            if (ImGui::InputText("##input", temp, sizeof(temp))) {
                if (widget.maxChars > 0 && widget.maxChars < 511) {
                    temp[widget.maxChars] = '\0';
                }
                buffer = temp;
                Emit({UIEvent::Type::ValueChanged, canvasEntity, canvas.canvasId,
                      widget.id, 0.0f, false});
            }
            break;
        }

        case UIWidgetType::Dropdown: {
            if (!widget.label.empty()) {
                const float fontSize = std::max(9.0f, ImGui::GetFontSize() * 0.9f);
                drawList->AddText(ImGui::GetFont(), fontSize,
                                  ImVec2(pos.x, pos.y - fontSize - 2.0f),
                                  IM_COL32(220, 226, 236, 255),
                                  widget.label.c_str());
            }
            ImGui::SetNextItemWidth(widthPx);
            if (ImGui::BeginCombo("##combo", widget.label.c_str())) {
                ImGui::Selectable(widget.label.c_str(), true);
                ImGui::EndCombo();
            }
            break;
        }

        case UIWidgetType::ScrollList:
            ImGui::BeginChild("##list",
                              ImVec2(widthPx, (widget.size.y > 0.0f
                                                  ? widget.size.y
                                                  : 120.0f) * scale),
                              ImGuiChildFlags_Borders);
            ImGui::TextDisabled("%s", widget.label.c_str());
            ImGui::EndChild();
            break;

        case UIWidgetType::Image: {
            const ImVec2 size(widthPx, (widget.size.y > 0.0f ? widget.size.y
                                                             : 96.0f) * scale);
            drawList->AddRectFilled(pos,
                                    ImVec2(pos.x + size.x, pos.y + size.y),
                                    IM_COL32(30, 30, 30, 200), radiusPx);
            if (widget.border > 0.0f) {
                drawList->AddRect(pos,
                                  ImVec2(pos.x + size.x, pos.y + size.y),
                                  IM_COL32(80, 80, 80, 220), radiusPx, 0,
                                  borderPx);
            }
            const char* caption =
                !widget.imagePath.empty()
                    ? widget.imagePath.c_str()
                    : (!widget.label.empty() ? widget.label.c_str() : "[Image]");
            const ImVec2 textSize = ImGui::CalcTextSize(caption);
            drawList->AddText(
                ImVec2(pos.x + (size.x - textSize.x) * 0.5f,
                       pos.y + (size.y - textSize.y) * 0.5f),
                IM_COL32(150, 150, 150, 255), caption);
            ImGui::Dummy(size);
            break;
        }

        case UIWidgetType::LayoutGroup:
            ImGui::Dummy(ImVec2(widthPx, (widget.size.y > 0.0f ? widget.size.y
                                                               : 20.0f) * scale));
            break;

        case UIWidgetType::None:
        default:
            break;
        }

        const bool interactive =
            widget.type == UIWidgetType::Button ||
            widget.type == UIWidgetType::CheckBox ||
            widget.type == UIWidgetType::Toggle ||
            widget.type == UIWidgetType::Slider ||
            widget.type == UIWidgetType::Input ||
            widget.type == UIWidgetType::Dropdown ||
            widget.type == UIWidgetType::ScrollList;
        if (interactive) {
            const std::string hoverKey = canvas.canvasId + "/" + widget.id;
            if (ImGui::IsItemHovered()) {
                if (m_hoveredWidgets.insert(hoverKey).second) {
                    Emit({UIEvent::Type::Hover, canvasEntity, canvas.canvasId,
                          widget.id, 0.0f, false});
                }
            } else {
                m_hoveredWidgets.erase(hoverKey);
            }
        }

        if (widget.useColor) ImGui::PopStyleColor(3);
        ImGui::PopStyleVar();
        ImGui::EndDisabled();
        ImGui::PopID();
    }

    ImGui::SetWindowFontScale(1.0f);
    ImGui::End();
    ImGui::PopStyleVar(3);
}

} // namespace Galeshi
