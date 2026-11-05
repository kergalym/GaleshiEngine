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

namespace Galeshi {

using namespace ScriptEngineInternal;

bool ScriptEngine::MakeWorkerUIResponse(
    const CppWorkerPacket& request, CppWorkerPacket& response) {
    const auto kind = static_cast<CppWorkerRequest>(request.kind);
    switch (kind) {
    case CppWorkerRequest::SetPostProcessFlag:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessBools, key)) {
                settings.*(field->member) = request.valid != 0;
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessFlag:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessBools, key)) {
                response.entity =
                    settings.*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessFloat:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessFloats, key)) {
                settings.*(field->member) = request.x;
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessFloat:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessFloats, key)) {
                response.x = settings.*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessInt:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            const int value = static_cast<int>(request.valid);
            if (key == "ao_mode") {
                settings.aoMode = static_cast<AmbientOcclusionMode>(
                    std::clamp(value, 0, 2));
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            } else if (const auto* field = FindPostProcessField(
                           kPostProcessInts, key)) {
                settings.*(field->member) = value;
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessInt:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (key == "ao_mode") {
                response.entity = static_cast<uint32_t>(
                    settings.aoMode);
                response.valid = 1;
            } else if (const auto* field = FindPostProcessField(
                           kPostProcessInts, key)) {
                response.entity = static_cast<uint32_t>(
                    settings.*(field->member));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessVec3:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessVec3s, key)) {
                settings.*(field->member) =
                    glm::vec3(request.x, request.y, request.z);
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessVec3:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessVec3s, key)) {
                const glm::vec3 value = settings.*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessEnabled:
        if (m_scene) {
            m_scene->SetHasGlobalPostProcess(request.valid != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetPostProcessEnabled:
        if (m_scene) {
            response.valid =
                m_scene->HasGlobalPostProcess() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::FindUICanvas:
        if (m_scene) {
            auto view = m_scene->Registry().view<UICanvasComponent>();
            for (entt::entity entity : view) {
                if (view.get<UICanvasComponent>(entity).canvas.canvasId ==
                    request.text) {
                    response.entity = static_cast<uint32_t>(entity);
                    response.valid = 1;
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::CreateUICanvas:
        if (m_scene) {
            UICanvas* existing = FindCanvasById(*m_scene, request.text);
            if (existing) {
                auto view =
                    m_scene->Registry().view<UICanvasComponent>();
                for (entt::entity entity : view) {
                    if (&view.get<UICanvasComponent>(entity).canvas ==
                        existing) {
                        response.entity =
                            static_cast<uint32_t>(entity);
                        response.valid = 1;
                        break;
                    }
                }
            } else {
                const std::string canvasId = request.text;
                const std::string title =
                    request.text2[0] ? request.text2 : canvasId;
                Entity entity = m_scene->CreateEntity(title);
                auto& component =
                    entity.AddComponent<UICanvasComponent>();
                component.canvas.canvasId = canvasId;
                component.canvas.title = title;
                response.entity =
                    static_cast<uint32_t>(entity.Handle());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::RemoveUICanvas:
        if (m_scene) {
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (m_scene->Registry().valid(entity) &&
                m_scene->Registry().all_of<UICanvasComponent>(entity)) {
                DestroyEntity(entity);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasFlag:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasBools, key)) {
                canvas->*(field->member) = request.valid != 0;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasFlag:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasBools, key)) {
                response.entity =
                    canvas->*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasFloat:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasFloats, key)) {
                canvas->*(field->member) = request.x;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasFloat:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasFloats, key)) {
                response.x = canvas->*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasString:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasStrings, key)) {
                canvas->*(field->member) = request.text2;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasString:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasStrings, key)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    (canvas->*(field->member)).c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasVec2:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasVec2s, key)) {
                canvas->*(field->member) =
                    glm::vec2(request.x, request.y);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasVec2:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasVec2s, key)) {
                const glm::vec2 value =
                    canvas->*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::FindUIWidget:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            UICanvas* canvas = FindCanvasByEntity(request.entity);
            response.entity = static_cast<uint32_t>(
                widget - canvas->widgets.data());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AddUIWidget:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            if (!FindWidgetById(*canvas, request.text)) {
                UIWidget widget;
                widget.id = request.text;
                widget.label = request.text;
                widget.type = UIWidgetTypeFromName(request.text2);
                widget.zOrder =
                    static_cast<int>(canvas->widgets.size());
                widget.size = {160.0f, 32.0f};
                canvas->widgets.push_back(std::move(widget));
                response.entity = static_cast<uint32_t>(
                    canvas->widgets.size() - 1);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::RemoveUIWidget:
        if (UICanvas* canvas = FindCanvasByEntity(request.entity)) {
            for (size_t i = 0; i < canvas->widgets.size(); ++i) {
                if (canvas->widgets[i].id != request.text) continue;
                canvas->widgets.erase(
                    canvas->widgets.begin() +
                    static_cast<std::ptrdiff_t>(i));
                for (UIWidget& widget : canvas->widgets) {
                    if (widget.parent == static_cast<int>(i)) {
                        widget.parent = -1;
                    } else if (widget.parent >
                               static_cast<int>(i)) {
                        --widget.parent;
                    }
                }
                response.valid = 1;
                break;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetFlag:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIBools, key)) {
                widget->*(field->member) = request.valid != 0;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetFlag:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIBools, key)) {
                response.entity =
                    widget->*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetFloat:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIFloats, key)) {
                widget->*(field->member) = request.x;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetFloat:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIFloats, key)) {
                response.x = widget->*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetInt:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            const int value = static_cast<int>(request.valid);
            if (key == "text_align") {
                widget->textAlign = static_cast<UITextAlign>(
                    std::clamp(value, 0, 2));
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIInts, key)) {
                widget->*(field->member) = value;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetInt:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (key == "text_align") {
                response.entity = static_cast<uint32_t>(
                    widget->textAlign);
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIInts, key)) {
                response.entity = static_cast<uint32_t>(
                    widget->*(field->member));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetString:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string combined = request.text2;
            const std::size_t split = combined.find('\n');
            const std::string key = NormalizeKey(
                combined.substr(0, split));
            const std::string value = split == std::string::npos
                ? std::string{}
                : combined.substr(split + 1);
            if (key == "text_align") {
                const std::string align = NormalizeKey(value);
                widget->textAlign = align == "center"
                    ? UITextAlign::Center
                    : align == "right" ? UITextAlign::Right
                                       : UITextAlign::Left;
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIStrings, key)) {
                widget->*(field->member) = value;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetString:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (key == "text_align") {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    widget->textAlign == UITextAlign::Center
                        ? "center"
                        : widget->textAlign == UITextAlign::Right
                              ? "right"
                              : "left");
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIStrings, key)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    (widget->*(field->member)).c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetVec2:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec2s, key)) {
                widget->*(field->member) =
                    glm::vec2(request.x, request.y);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetVec2:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec2s, key)) {
                const glm::vec2 value =
                    widget->*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetVec4:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec4s, key)) {
                widget->*(field->member) = glm::vec4(
                    request.x, request.y, request.z, request.w);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetVec4:
        if (UIWidget* widget =
                FindWidgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec4s, key)) {
                const glm::vec4 value =
                    widget->*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.w = value.w;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::PollUIEvent:
        if (!m_cppUIEvents.empty()) {
            const UIEvent event = std::move(m_cppUIEvents.front());
            m_cppUIEvents.pop_front();
            response.entity = static_cast<uint32_t>(
                event.canvasEntity);
            response.other =
                static_cast<uint32_t>(event.type);
            response.x = event.value;
            response.w = event.checked ? 1.0f : 0.0f;
            std::snprintf(
                response.text, sizeof(response.text), "%s",
                event.canvasId.c_str());
            std::snprintf(
                response.text2, sizeof(response.text2), "%s",
                event.widgetId.c_str());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::IsPointerOverUI:
        if (m_isPointerOverUI) {
            response.valid =
                m_isPointerOverUI() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::GetUIText:
        if (m_uiText) {
            const std::string text = m_uiText(
                request.entity, request.text, request.text2);
            std::snprintf(response.text, sizeof(response.text), "%s",
                          text.c_str());
            response.valid = 1;
        }
        break;
    default:
        return false;
    }
    return true;
}

} // namespace Galeshi
