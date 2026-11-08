#include "GaleshiEditorApp.hpp"

#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/MaterialScript.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

bool GaleshiEditorApp::ApplyMaterialScript(
    Galeshi::MaterialComponent& slot, const std::string& path) {
    if (!m_assets || path.empty()) return false;

    Galeshi::MaterialScript script;
    if (!script.Load(m_assets->ResolvePath(path))) return false;

    std::shared_ptr<Galeshi::Material> source = slot.material;
    if (!source) source = std::make_shared<Galeshi::Material>();
    // Reapplying a master always starts from the material that was captured
    // when the slot was first instanced, so arithmetic never stacks twice.
    if (source->base && !source->masterScriptPath.empty()) {
        source = source->base;
    }

    auto instance = std::make_shared<Galeshi::Material>(*source);
    instance->base = source;
    instance->masterScriptPath = m_assets->MakeRelativePath(path);
    instance->name = source->name.empty()
        ? script.Name()
        : source->name + " (" + script.Name() + ")";
    script.Apply(*instance);
    instance->sceneOverride = true;

    slot.material = instance;
    slot.isGlass = instance->isGlass;
    return true;
}

int GaleshiEditorApp::ApplyMaterialScriptToHierarchy(entt::entity root,
                                                     const std::string& path) {
    if (root == entt::null || !m_scene.Registry().valid(root)) return 0;
    auto& registry = m_scene.Registry();
    int count = 0;
    for (entt::entity handle : registry.view<Galeshi::MaterialComponent>()) {
        entt::entity current = handle;
        bool belongs = false;
        for (int depth = 0; current != entt::null && depth < 128; ++depth) {
            if (current == root) {
                belongs = true;
                break;
            }
            if (!registry.all_of<Galeshi::ParentComponent>(current)) break;
            current = registry.get<Galeshi::ParentComponent>(current).parent;
        }
        if (!belongs) continue;
        auto& slot = registry.get<Galeshi::MaterialComponent>(handle);
        if (ApplyMaterialScript(slot, path)) ++count;
    }
    return count;
}

int GaleshiEditorApp::ReapplyMaterialScripts(const std::string& path) {
    if (!m_assets || path.empty()) return 0;
    const std::string relative = m_assets->MakeRelativePath(path);
    auto& registry = m_scene.Registry();
    int count = 0;
    for (entt::entity handle : registry.view<Galeshi::MaterialComponent>()) {
        auto& slot = registry.get<Galeshi::MaterialComponent>(handle);
        if (!slot.material || slot.material->masterScriptPath.empty()) continue;
        if (m_assets->MakeRelativePath(slot.material->masterScriptPath) !=
            relative) {
            continue;
        }
        if (ApplyMaterialScript(slot, path)) ++count;
    }
    return count;
}
