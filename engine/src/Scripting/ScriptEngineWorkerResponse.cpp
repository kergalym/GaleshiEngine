#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Assets/Material.hpp"

namespace Galeshi {

using namespace ScriptEngineInternal;

UICanvas* ScriptEngine::FindCanvasByEntity(uint32_t handle) {
    if (!m_scene) return nullptr;
    auto& registry = m_scene->Registry();
    const entt::entity entity = static_cast<entt::entity>(handle);
    if (!registry.valid(entity) ||
        !registry.all_of<UICanvasComponent>(entity)) {
        return nullptr;
    }
    return &registry.get<UICanvasComponent>(entity).canvas;
}

UIWidget* ScriptEngine::FindWidgetByEntity(uint32_t handle,
                                           const char* widgetId) {
    UICanvas* canvas = FindCanvasByEntity(handle);
    if (!canvas || !widgetId) return nullptr;
    return FindWidgetById(*canvas, widgetId);
}

std::shared_ptr<Material> ScriptEngine::FindMaterialByEntity(uint32_t handle) {
    if (!m_scene) return nullptr;
    auto& registry = m_scene->Registry();
    const entt::entity entity = static_cast<entt::entity>(handle);
    if (!registry.valid(entity) ||
        !registry.all_of<MaterialComponent>(entity)) {
        return nullptr;
    }
    return registry.get<MaterialComponent>(entity).material;
}

CppWorkerPacket ScriptEngine::MakeWorkerResponse(
    const CppWorkerPacket& request) {
    CppWorkerPacket response;
    response.type = static_cast<uint32_t>(CppWorkerMessage::Response);
    response.id = request.id;
    response.entity = UINT32_MAX;

    if (MakeWorkerCoreResponse(request, response)) return response;
    if (MakeWorkerUIResponse(request, response)) return response;
    if (MakeWorkerWorldResponse(request, response)) return response;
    if (MakeWorkerAIResponse(request, response)) return response;
    return response;
}

} // namespace Galeshi
