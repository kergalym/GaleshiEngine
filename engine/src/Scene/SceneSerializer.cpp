#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <sstream>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <glm/gtc/quaternion.hpp>

using json = nlohmann::json;
namespace Galeshi {

namespace {

constexpr int kSceneFormatVersion = 2;

json Vec3ToJson(const glm::vec3& v) { return json::array({v.x, v.y, v.z}); }
glm::vec3 Vec3FromJson(const json& j) { return { j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>() }; }
json Vec4ToJson(const glm::vec4& v) { return json::array({v.x, v.y, v.z, v.w}); }
glm::vec4 Vec4FromJson(const json& j) { return { j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>() }; }
json QuatToJson(const glm::quat& q) { return json::array({q.w, q.x, q.y, q.z}); }
glm::quat QuatFromJson(const json& j) { return glm::quat(j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>(), j.at(3).get<float>()); }

std::string TexturePath(const std::shared_ptr<Texture>& texture) {
    return texture ? texture->Path() : std::string{};
}

json MaterialToJson(const Material& m, const AssetManager& assets) {
    auto relPath = [&](const std::shared_ptr<Texture>& tex) {
        if (!tex) return std::string{};
        return assets.MakeRelativePath(tex->Path());
    };
    auto relStr = [&](const std::string& s) {
        return assets.MakeRelativePath(s);
    };
    return {
        {"name", m.name},
        {"sourcePath", relStr(m.sourcePath)},
        {"albedoFactor", Vec4ToJson(m.albedoFactor)},
        {"metallicFactor", m.metallicFactor},
        {"roughnessFactor", m.roughnessFactor},
        {"emissiveFactor", Vec3ToJson(m.emissiveFactor)},
        {"isGlass", m.isGlass},
        {"twoSided", m.twoSided},
        {"glassOpacity", m.glassOpacity},
        {"normalStrength", m.normalStrength},
        {"occlusionStrength", m.occlusionStrength},
        {"alphaCutoff", m.alphaCutoff},
        {"albedoMapPath", relPath(m.albedoMap)},
        {"normalMapPath", relPath(m.normalMap)},
        {"mrMapPath", relPath(m.metallicRoughnessMap)},
        {"metallicMapPath", relPath(m.metallicMap)},
        {"roughnessMapPath", relPath(m.roughnessMap)},
        {"emissiveMapPath", relPath(m.emissiveMap)},
        {"occlusionMapPath", relPath(m.occlusionMap)},
        {"opacityMapPath", relPath(m.opacityMap)},
        {"specularMapPath", relPath(m.specularMap)}
    };
}

std::shared_ptr<Material> MaterialFromJson(const json& jm,
                                           AssetManager& assets,
                                           const std::string& textureRoot) {
    auto material = std::make_shared<Material>();
    material->name = jm.value("name", std::string("Material"));
    material->sourcePath = jm.value("sourcePath", std::string{});
    material->albedoFactor = jm.contains("albedoFactor")
        ? Vec4FromJson(jm.at("albedoFactor")) : glm::vec4(1.0f);
    material->metallicFactor = jm.value("metallicFactor", 0.0f);
    material->roughnessFactor = jm.value("roughnessFactor", 0.5f);
    material->emissiveFactor = jm.contains("emissiveFactor")
        ? Vec3FromJson(jm.at("emissiveFactor")) : glm::vec3(0.0f);
    material->isGlass = jm.value("isGlass", false);
    // Older scenes had no two-sided switch and the renderer mirrored
    // backface normals unconditionally. Default missing values to true so
    // loaded scenes keep their previous appearance.
    material->twoSided = jm.value("twoSided", true);
    material->glassOpacity = jm.value("glassOpacity", 0.35f);
    material->normalStrength = jm.value("normalStrength", 1.0f);
    material->occlusionStrength = jm.value("occlusionStrength", 1.0f);
    material->alphaCutoff = jm.value("alphaCutoff", 0.5f);

    std::string modelSource = textureRoot;
    if (modelSource.empty()) modelSource = material->sourcePath;
    if (!modelSource.empty()) {
        // Register embedded model textures before resolving material maps.
        assets.LoadModel(modelSource);
    }

    auto loadMap = [&](const char* key, bool srgb) -> std::shared_ptr<Texture> {
        const std::string path = jm.value(key, std::string{});
        if (path.empty()) return nullptr;

        auto load = [&](const std::string& candidate) -> std::shared_ptr<Texture> {
            if (candidate.empty()) return nullptr;
            auto texture = assets.LoadTexture(candidate, srgb);
            return (texture && texture != assets.WhiteTexture()) ? texture : nullptr;
        };

        auto texture = load(path);
        if (!texture && !modelSource.empty()) {
            const auto modelDir = std::filesystem::path(modelSource).parent_path();
            texture = load((modelDir / path).string());
            if (!texture) {
                texture = load((modelDir / std::filesystem::path(path).filename()).string());
            }
        }
        return texture;
    };

    material->albedoMap = loadMap("albedoMapPath", true);
    material->normalMap = loadMap("normalMapPath", false);
    material->metallicRoughnessMap = loadMap("mrMapPath", false);
    material->metallicMap = loadMap("metallicMapPath", false);
    material->roughnessMap = loadMap("roughnessMapPath", false);
    material->emissiveMap = loadMap("emissiveMapPath", true);
    material->occlusionMap = loadMap("occlusionMapPath", false);
    material->opacityMap = loadMap("opacityMapPath", false);
    material->specularMap = loadMap("specularMapPath", false);
    return material;
}

// Character sub-meshes are named "<root>_<number>".  Old scene files stored
// parent handles instead of stable ids, and those handles do not survive a
// registry clear.  The name is the only reliable link left in those files.
bool LooksLikeChildName(const std::string& child, const std::string& parent) {
    if (parent.empty() || child.size() <= parent.size() + 1) return false;
    if (child.compare(0, parent.size(), parent) != 0) return false;
    if (child[parent.size()] != '_') return false;
    for (size_t i = parent.size() + 1; i < child.size(); ++i) {
        if (std::isdigit(static_cast<unsigned char>(child[i])) == 0) return false;
    }
    return true;
}

std::shared_ptr<Mesh> BuildClothMesh(const SoftBodyComponent& softBody) {
    auto mesh = std::make_shared<Mesh>();
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    if (!softBody.vertices.empty() && !softBody.indices.empty()) {
        vertices.reserve(softBody.vertices.size());
        for (const auto& position : softBody.vertices) {
            Vertex vertex{};
            vertex.position = position;
            vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertex.boneIDs = glm::vec4(0.0f);
            vertex.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            vertices.push_back(vertex);
        }
        indices = softBody.indices;
    } else if (softBody.gridX > 1 && softBody.gridY > 1) {
        vertices.reserve(static_cast<size_t>(softBody.gridX) * static_cast<size_t>(softBody.gridY));
        for (int y = 0; y < softBody.gridY; ++y) {
            for (int x = 0; x < softBody.gridX; ++x) {
                Vertex vertex{};
                vertex.position = softBody.origin +
                    glm::vec3(static_cast<float>(x) * softBody.spacing, 0.0f,
                              static_cast<float>(y) * softBody.spacing);
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f);
                vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.uv = glm::vec2(static_cast<float>(x) / static_cast<float>(softBody.gridX),
                                      static_cast<float>(y) / static_cast<float>(softBody.gridY));
                vertex.boneIDs = glm::vec4(0.0f);
                vertex.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
                vertices.push_back(vertex);
            }
        }
        for (int y = 0; y < softBody.gridY - 1; ++y) {
            for (int x = 0; x < softBody.gridX - 1; ++x) {
                const uint32_t topLeft = static_cast<uint32_t>(y * softBody.gridX + x);
                const uint32_t topRight = topLeft + 1;
                const uint32_t bottomLeft = topLeft + static_cast<uint32_t>(softBody.gridX);
                const uint32_t bottomRight = bottomLeft + 1;
                indices.push_back(topLeft); indices.push_back(bottomLeft); indices.push_back(topRight);
                indices.push_back(topRight); indices.push_back(bottomLeft); indices.push_back(bottomRight);
            }
        }
    }

    if (!vertices.empty() && !indices.empty()) mesh->Upload(vertices, indices);
    return mesh;
}

} // namespace

std::string SceneSerializer::SaveToString() const {
    json root;
    root["version"] = kSceneFormatVersion;
    root["entities"] = json::array();

    // Iterate once and assign stable ids.  ParentComponent stores an entt
    // handle which changes after registry.clear(), so the file must never use
    // that handle as a reference.
    std::vector<entt::entity> entities;
    auto view = m_scene.Registry().view<NameComponent>();
    entities.reserve(view.size());
    for (auto handle : view) entities.push_back(handle);

    std::unordered_map<entt::entity, int> idByEntity;
    idByEntity.reserve(entities.size());
    for (size_t i = 0; i < entities.size(); ++i) {
        idByEntity.emplace(entities[i], static_cast<int>(i));
    }

    // Collect the unique materials used by the scene.  Materials are shared
    // between entities by shared_ptr, so storing them once in a top-level
    // table keeps that sharing (and the base/instance relationship) intact
    // across save/load.  A material's base is included even if no entity
    // references it directly.
    struct MaterialRecord {
        std::shared_ptr<Material> material;
        std::string textureRoot;
    };
    std::vector<MaterialRecord> materialRecords;
    std::unordered_map<const Material*, int> materialIds;
    std::unordered_set<const Material*> visitingMaterials;

    std::function<void(const std::shared_ptr<Material>&, const std::string&)> addMaterial;
    addMaterial = [&](const std::shared_ptr<Material>& material, const std::string& textureRoot) {
        if (!material || !visitingMaterials.insert(material.get()).second) return;

        if (material->base) {
            const std::string baseRoot = !textureRoot.empty() ? textureRoot : material->sourcePath;
            addMaterial(material->base, baseRoot);
        }

        auto [it, inserted] = materialIds.emplace(material.get(),
                                                   static_cast<int>(materialRecords.size()));
        if (inserted) {
            materialRecords.push_back({material, textureRoot});
        } else if (materialRecords[it->second].textureRoot.empty() && !textureRoot.empty()) {
            materialRecords[it->second].textureRoot = textureRoot;
        }
        visitingMaterials.erase(material.get());
    };

    const auto& registry = m_scene.Registry();
    for (const entt::entity handle : entities) {
        if (!registry.all_of<MaterialComponent>(handle)) continue;
        const auto& material = registry.get<MaterialComponent>(handle).material;
        if (!material) continue;

        std::string textureRoot;
        if (registry.all_of<MeshComponent>(handle)) {
            textureRoot = registry.get<MeshComponent>(handle).sourceAssetPath;
        }
        if (textureRoot.empty()) textureRoot = material->sourcePath;
        addMaterial(material, textureRoot);
    }

    if (!materialRecords.empty()) {
        root["materials"] = json::array();
        for (const auto& record : materialRecords) {
            const auto& material = *record.material;
            json jm = MaterialToJson(material, m_assets);
            jm["textureRoot"] = m_assets.MakeRelativePath(record.textureRoot);
            if (material.base) {
                const auto baseIt = materialIds.find(material.base.get());
                if (baseIt != materialIds.end()) jm["baseId"] = baseIt->second;
            }
            root["materials"].push_back(std::move(jm));
        }
    }

    for (size_t i = 0; i < entities.size(); ++i) {
        const entt::entity handle = entities[i];
        const auto& reg = m_scene.Registry();
        json je;
        je["id"] = static_cast<int>(i);
        je["name"] = view.get<NameComponent>(handle).name;

        if (reg.all_of<ParentComponent>(handle)) {
            const entt::entity parent = reg.get<ParentComponent>(handle).parent;
            const auto parentIt = idByEntity.find(parent);
            if (parentIt != idByEntity.end()) je["parent"] = parentIt->second;
        }
        if (reg.all_of<TransformComponent>(handle)) {
            const auto& tc = reg.get<TransformComponent>(handle);
            je["transform"] = { {"position", Vec3ToJson(tc.position)}, {"rotation", QuatToJson(tc.rotation)}, {"scale", Vec3ToJson(tc.scale)} };
        }
        if (reg.all_of<LandscapeComponent>(handle)) {
            const auto& lc = reg.get<LandscapeComponent>(handle);
            // Sanitize NaN/Infinity for JSON.
            std::vector<float> safeHeights = lc.heights;
            for (auto& value : safeHeights) {
                if (!std::isfinite(value)) value = 0.0f;
            }
            je["landscape"] = {
                {"sizeX", lc.sizeX},
                {"sizeZ", lc.sizeZ},
                {"scale", lc.scale},
                {"heights", safeHeights}
            };
        }
        if (reg.all_of<VegetationComponent>(handle)) {
            const auto& veg = reg.get<VegetationComponent>(handle);
            je["vegetation"] = {
                {"enabled", veg.enabled},
                {"castShadows", veg.castShadows},
                {"grassCastShadows", veg.grassCastShadows},
                {"treeCount", veg.treeCount},
                {"grassCount", veg.grassCount},
                {"treeMinScale", veg.treeMinScale},
                {"treeMaxScale", veg.treeMaxScale},
                {"grassMinScale", veg.grassMinScale},
                {"grassMaxScale", veg.grassMaxScale},
                {"slopeLimit", veg.slopeLimit},
                {"seed", veg.seed},
                {"trunkColor", Vec4ToJson(veg.trunkColor)},
                {"canopyColor", Vec4ToJson(veg.canopyColor)},
                {"grassColor", Vec4ToJson(veg.grassColor)}
            };
        }

        if (reg.all_of<MeshComponent>(handle)) {
            const auto& mc = reg.get<MeshComponent>(handle);
            je["mesh"] = { {"sourceAssetPath", m_assets.MakeRelativePath(mc.sourceAssetPath)}, {"primitiveType", mc.primitiveType}, {"meshIndex", mc.meshIndex} };
        }
        if (reg.all_of<MaterialComponent>(handle)) {
            const auto& matc = reg.get<MaterialComponent>(handle);
            if (matc.material) {
                const auto materialIt = materialIds.find(matc.material.get());
                if (materialIt != materialIds.end()) {
                    je["materialId"] = materialIt->second;
                }
                // Keep the inline material for backward compatibility with
                // scene files and tools that predate the material table.
                je["material"] = MaterialToJson(*matc.material, m_assets);
            }
        }
        if (reg.all_of<SkeletalComponent>(handle)) {
            const auto& skel = reg.get<SkeletalComponent>(handle);
            je["skeletal"] = {
                {"isAnimated", skel.isAnimated},
                {"sourcePath", skel.sourcePath},
                {"currentTime", skel.currentTime},
                {"animationSpeed", skel.animationSpeed},
                {"activeAnimation", skel.activeAnimation}
            };
        }
        if (reg.all_of<AnimationStateMachineComponent>(handle)) {
            const auto& machine = reg.get<AnimationStateMachineComponent>(handle);
            json states = json::array();
            for (const auto& state : machine.states) {
                states.push_back({
                    {"name", state.name},
                    {"clip", state.clip},
                    {"loop", state.loop},
                    {"speed", state.speed}
                });
            }
            json transitions = json::array();
            for (const auto& transition : machine.transitions) {
                transitions.push_back({
                    {"fromState", transition.fromState},
                    {"toState", transition.toState},
                    {"parameter", transition.parameter},
                    {"condition", static_cast<int>(transition.condition)},
                    {"threshold", transition.threshold},
                    {"duration", transition.duration},
                    {"enabled", transition.enabled}
                });
            }
            json parameters = json::array();
            for (const auto& parameter : machine.parameters) {
                parameters.push_back({
                    {"name", parameter.name},
                    {"value", parameter.value}
                });
            }
            je["animationStateMachine"] = {
                {"enabled", machine.enabled},
                {"currentState", machine.currentState},
                {"playbackSpeed", machine.playbackSpeed},
                {"blendDuration", machine.blendDuration},
                {"states", std::move(states)},
                {"transitions", std::move(transitions)},
                {"parameters", std::move(parameters)}
            };
        }
        if (reg.all_of<CameraComponent>(handle)) {
            const auto& camera = reg.get<CameraComponent>(handle);
            je["camera"] = {
                {"fovY", camera.fovY},
                {"nearZ", camera.nearZ},
                {"farZ", camera.farZ},
                {"primary", camera.primary}
            };
        }
        if (reg.all_of<DirectionalLightComponent>(handle)) {
            const auto& dl = reg.get<DirectionalLightComponent>(handle);
            je["directionalLight"] = { {"direction", Vec3ToJson(dl.direction)}, {"color", Vec3ToJson(dl.color)}, {"intensity", dl.intensity} };
        }
        if (reg.all_of<PointLightComponent>(handle)) {
            const auto& pl = reg.get<PointLightComponent>(handle);
            je["pointLight"] = { {"color", Vec3ToJson(pl.color)}, {"intensity", pl.intensity}, {"radius", pl.radius} };
        }
        if (reg.all_of<SpotLightComponent>(handle)) {
            const auto& sl = reg.get<SpotLightComponent>(handle);
            je["spotLight"] = {
                {"direction", Vec3ToJson(sl.direction)},
                {"innerConeAngle", sl.innerConeAngle},
                {"outerConeAngle", sl.outerConeAngle},
                {"radius", sl.radius},
                {"color", Vec3ToJson(sl.color)},
                {"intensity", sl.intensity}
            };
        }
        if (reg.all_of<AudioSourceComponent>(handle)) {
            const auto& audio = reg.get<AudioSourceComponent>(handle);
            je["audioSource"] = {
                {"soundPath", audio.soundPath},
                {"loop", audio.loop},
                {"autoplay", audio.autoplay}
            };
        }
        if (reg.all_of<UICanvasComponent>(handle)) {
            const auto& uiCanvas = reg.get<UICanvasComponent>(handle).canvas;
            json widgets = json::array();
            for (const auto& widget : uiCanvas.widgets) {
                widgets.push_back({
                    {"id", widget.id},
                    {"label", widget.label},
                    {"type", static_cast<int>(widget.type)},
                    {"parent", widget.parent},
                    {"zOrder", widget.zOrder},
                    {"position", {widget.position.x, widget.position.y}},
                    {"size", {widget.size.x, widget.size.y}},
                    {"anchorPreset", static_cast<int>(widget.anchorPreset)},
                    {"anchorMin", {widget.anchorMin.x, widget.anchorMin.y}},
                    {"anchorMax", {widget.anchorMax.x, widget.anchorMax.y}},
                    {"pivot", {widget.pivot.x, widget.pivot.y}},
                    {"visible", widget.visible},
                    {"enabled", widget.enabled},
                    {"interactable", widget.interactable},
                    {"blockInput", widget.blockInput},
                    {"locked", widget.locked},
                    {"opacity", widget.opacity},
                    {"buttonHeight", widget.buttonHeight},
                    {"sliderMin", widget.sliderMin},
                    {"sliderMax", widget.sliderMax},
                    {"sliderValue", widget.sliderValue},
                    {"checked", widget.checked},
                    {"maxChars", widget.maxChars},
                    {"imagePath", widget.imagePath},
                    {"useColor", widget.useColor},
                    {"color", Vec4ToJson(widget.color)},
                    {"radius", widget.radius},
                    {"border", widget.border},
                    {"onShow", widget.onShow},
                    {"onHover", widget.onHover},
                    {"onClick", widget.onClick}
                });
            }
            je["uiCanvas"] = {
                {"canvasId", uiCanvas.canvasId},
                {"title", uiCanvas.title},
                {"screenPosition", {uiCanvas.screenPosition.x, uiCanvas.screenPosition.y}},
                {"screenSize", {uiCanvas.screenSize.x, uiCanvas.screenSize.y}},
                {"resizable", uiCanvas.resizable},
                {"movable", uiCanvas.movable},
                {"closeable", uiCanvas.closeable},
                {"visible", uiCanvas.visible},
                {"backgroundAlpha", uiCanvas.backgroundAlpha},
                {"renderMode", static_cast<int>(uiCanvas.renderMode)},
                {"referenceResolution", {uiCanvas.referenceResolution.x, uiCanvas.referenceResolution.y}},
                {"scaleMode", static_cast<int>(uiCanvas.scaleMode)},
                {"matchWidthOrHeight", uiCanvas.matchWidthOrHeight},
                {"pixelPerfect", uiCanvas.pixelPerfect},
                {"sortOrder", uiCanvas.sortOrder},
                {"onMenuOpen", uiCanvas.onMenuOpen},
                {"onMenuHover", uiCanvas.onMenuHover},
                {"onMenuClick", uiCanvas.onMenuClick},
                {"widgets", std::move(widgets)}
            };
        }
        if (reg.all_of<RigidBodyComponent>(handle)) {
            const auto& rb = reg.get<RigidBodyComponent>(handle);
            const char* motionType = "Dynamic";
            if (rb.motionType == RigidBodyComponent::MotionType::Static) motionType = "Static";
            else if (rb.motionType == RigidBodyComponent::MotionType::Kinematic) motionType = "Kinematic";
            je["rigidBody"] = {
                {"motionType", motionType},
                {"mass", rb.mass},
                {"friction", rb.friction},
                {"restitution", rb.restitution},
                {"useGravity", rb.useGravity}
            };
        }
        if (reg.all_of<BoxColliderComponent>(handle)) {
            const auto& collider = reg.get<BoxColliderComponent>(handle);
            je["boxCollider"] = { {"halfExtents", Vec3ToJson(collider.halfExtents)} };
        }
        if (reg.all_of<NavAgentComponent>(handle)) {
            const auto& agent = reg.get<NavAgentComponent>(handle);
            json path = json::array();
            for (const auto& point : agent.path) path.push_back(Vec3ToJson(point));
            je["navAgent"] = {
                {"path", std::move(path)},
                {"currentWaypoint", agent.currentWaypoint},
                {"speed", agent.speed},
                {"stoppingDistance", agent.stoppingDistance},
                {"targetPosition", Vec3ToJson(agent.targetPosition)},
                {"hasTarget", agent.hasTarget}
            };
        }
        if (reg.all_of<SoftBodyComponent>(handle)) {
            const auto& softBody = reg.get<SoftBodyComponent>(handle);
            json vertices = json::array();
            for (const auto& vertex : softBody.vertices) vertices.push_back(Vec3ToJson(vertex));
            je["softBody"] = {
                {"gridX", softBody.gridX},
                {"gridY", softBody.gridY},
                {"spacing", softBody.spacing},
                {"origin", Vec3ToJson(softBody.origin)},
                {"stiffness", softBody.stiffness},
                {"vertices", std::move(vertices)},
                {"indices", softBody.indices}
            };
        }
        if (reg.all_of<AIControllerComponent>(handle)) {
            const auto& controller = reg.get<AIControllerComponent>(handle);
            je["aiController"] = { {"scriptPath", controller.scriptPath} };
        }
        if (reg.all_of<PerceptionComponent>(handle)) {
            const auto& perception = reg.get<PerceptionComponent>(handle);
            je["perception"] = {
                {"sightRadius", perception.sightRadius},
                {"fieldOfViewDegrees", perception.fieldOfViewDegrees},
                {"hearingRadius", perception.hearingRadius},
                {"eyeHeight", perception.eyeHeight},
                {"detectionInterval", perception.detectionInterval},
                {"memoryDuration", perception.memoryDuration},
                {"requireLineOfSight", perception.requireLineOfSight},
                {"detectPlayerOnly", perception.detectPlayerOnly},
                {"targetName", perception.targetName}
            };
        }
        if (reg.all_of<BehaviorTreeComponent>(handle)) {
            const auto& tree = reg.get<BehaviorTreeComponent>(handle);
            json nodes = json::array();
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
                    {"parameter", node.parameter},
                    {"value", node.value}
                });
            }
            je["behaviorTree"] = {
                {"enabled", tree.enabled},
                {"root", tree.root},
                {"tickInterval", tree.tickInterval},
                {"nodes", std::move(nodes)}
            };
        }
        if (reg.all_of<PlayerTagComponent>(handle)) {
            je["playerTag"] = true;
        }
        if (reg.all_of<LuaEntityComponent>(handle)) {
            je["luaEntity"] = true;
        }

        root["entities"].push_back(std::move(je));
    }

    return root.dump(2);
}

bool SceneSerializer::Save(const std::string& path) const {
    std::ofstream out(path);
    if (!out.is_open()) return false;
    out << SaveToString();
    return out.good();
}

bool SceneSerializer::Load(const std::string& path,
                           const std::atomic<bool>* cancel) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return LoadFromString(ss.str(), cancel);
}

bool SceneSerializer::LoadFromString(const std::string& jsonStr,
                                     const std::atomic<bool>* cancel) {
    const auto cancelled = [cancel] {
        return cancel && cancel->load(std::memory_order_relaxed);
    };

    json root;
    try {
        root = json::parse(jsonStr);
    } catch (const std::exception& e) {
        spdlog::error("[Scene] Failed to parse scene JSON: {}", e.what());
        return false;
    }

    if (!root.is_object() || !root.contains("entities") || !root["entities"].is_array()) {
        spdlog::error("[Scene] Invalid scene JSON: missing entities array");
        return false;
    }

    const int formatVersion = root.value("version", 1);
    const auto& jsonEntities = root["entities"];

    struct LoadedEntity {
        Entity entity;
        const json* source = nullptr;
        std::string name;
        int savedId = -1;
        bool hasParent = false;
        uint32_t parentValue = 0;
    };

    m_scene.Registry().clear();

    // Material instances are stored once in a top-level table.  Build them
    // before the entities so every component referencing the same materialId
    // receives the same shared_ptr; resolve base links in a second pass to
    // allow instances to appear before their bases in malformed/hand-edited
    // files.
    std::vector<std::shared_ptr<Material>> materialLibrary;
    std::vector<int> materialBaseIds;
    if (root.contains("materials") && root["materials"].is_array()) {
        const auto& jsonMaterials = root["materials"];
        materialLibrary.reserve(jsonMaterials.size());
        materialBaseIds.reserve(jsonMaterials.size());
        for (const auto& jm : jsonMaterials) {
            if (cancelled()) {
                m_scene.Registry().clear();
                spdlog::info("[Scene] Load cancelled while preparing materials");
                return false;
            }
            if (!jm.is_object()) {
                materialLibrary.push_back(nullptr);
                materialBaseIds.push_back(-1);
                continue;
            }
            const std::string textureRoot = jm.value("textureRoot", std::string{});
            materialLibrary.push_back(MaterialFromJson(jm, m_assets, textureRoot));
            materialBaseIds.push_back(jm.value("baseId", -1));
        }
        for (size_t i = 0; i < materialLibrary.size(); ++i) {
            if (!materialLibrary[i]) continue;
            const int baseId = materialBaseIds[i];
            if (baseId < 0 || baseId >= static_cast<int>(materialLibrary.size()) ||
                baseId == static_cast<int>(i) || !materialLibrary[static_cast<size_t>(baseId)]) {
                continue;
            }
            materialLibrary[i]->base = materialLibrary[static_cast<size_t>(baseId)];
            // Reject malformed cycles so Reset to Base can never loop.
            const Material* ancestor = materialLibrary[i]->base.get();
            size_t guard = 0;
            while (ancestor && guard++ < materialLibrary.size()) {
                if (ancestor == materialLibrary[i].get()) {
                    materialLibrary[i]->base.reset();
                    break;
                }
                ancestor = ancestor->base.get();
            }
        }
    }

    std::vector<LoadedEntity> loaded;
    loaded.reserve(jsonEntities.size());

    // SaveToString writes entities in EnTT view order, which is the reverse
    // of creation order.  Create them in reverse here so that after loading a
    // view visits entities in exactly the same order as before saving.  Some
    // systems (for example the directional-light selection) rely on the first
    // entity returned by a view being stable.
    for (auto it = jsonEntities.rbegin(); it != jsonEntities.rend(); ++it) {
        if (cancelled()) {
            m_scene.Registry().clear();
            spdlog::info("[Scene] Load cancelled while creating entities");
            return false;
        }
        const auto& je = *it;
        if (!je.is_object()) continue;
        const std::string name = je.value("name", std::string("Entity"));
        Entity entity = m_scene.CreateEntity(name);
        LoadedEntity entry;
        entry.entity = entity;
        entry.source = &je;
        entry.name = name;
        entry.savedId = je.value("id", -1);
        if (je.contains("transform")) {
            auto& tc = entity.GetComponent<TransformComponent>();
            const auto& jt = je["transform"];
            tc.position = Vec3FromJson(jt.at("position"));
            tc.rotation = QuatFromJson(jt.at("rotation"));
            tc.scale = Vec3FromJson(jt.at("scale"));
        }
        if (je.contains("parent") && je["parent"].is_number_integer()) {
            entry.hasParent = true;
            entry.parentValue = je["parent"].get<uint32_t>();
        }
        loaded.push_back(std::move(entry));
    }
    std::reverse(loaded.begin(), loaded.end());

    // Create a mapping for the old format.  SceneSerializer used to write the
    // raw entt handle, and EnTT's component view visits entities in reverse
    // creation order.  With contiguous handles the saved array index i has
    // old handle (N - 1 - i), which lets us recover the intended parent.
    std::unordered_map<uint32_t, entt::entity> legacyHandleToEntity;
    if (formatVersion < kSceneFormatVersion) {
        const size_t count = loaded.size();
        for (size_t i = 0; i < count; ++i) {
            if (count - 1 - i <= 0xFFFFFu) {
                legacyHandleToEntity[static_cast<uint32_t>(count - 1 - i)] = loaded[i].entity.Handle();
            }
        }
    }

    std::unordered_map<int, entt::entity> savedIdToEntity;
    for (const auto& entry : loaded) {
        if (entry.savedId >= 0) savedIdToEntity.emplace(entry.savedId, entry.entity.Handle());
    }

    for (auto entryIt = loaded.rbegin(); entryIt != loaded.rend(); ++entryIt) {
        if (cancelled()) {
            m_scene.Registry().clear();
            spdlog::info("[Scene] Load cancelled before the next entity");
            return false;
        }
        auto& entry = *entryIt;
        if (!entry.source) continue;
        const auto& je = *entry.source;
        auto& e = entry.entity;
        const auto& reg = m_scene.Registry();

        // Landscape data has to exist before its generated mesh is rebuilt.
        if (je.contains("landscape")) {
            const auto& jl = je["landscape"];
            auto& landscape = e.AddComponent<LandscapeComponent>();
            landscape.sizeX = std::max(1, jl.value("sizeX", 100));
            landscape.sizeZ = std::max(1, jl.value("sizeZ", 100));
            landscape.scale = jl.value("scale", 1.0f);
            if (jl.contains("heights") && jl["heights"].is_array()) {
                landscape.heights.reserve(jl["heights"].size());
                for (const auto& height : jl["heights"]) {
                    const float value = height.get<float>();
                    landscape.heights.push_back(std::isfinite(value) ? value : 0.0f);
                }
            }
            const size_t expected = static_cast<size_t>(landscape.sizeX) * static_cast<size_t>(landscape.sizeZ);
            if (landscape.heights.size() != expected) landscape.heights.resize(expected, 0.0f);
        }
        if (je.contains("vegetation")) {
            const auto& jv = je["vegetation"];
            auto& vegetation = e.AddComponent<VegetationComponent>();
            vegetation.enabled = jv.value("enabled", true);
            vegetation.castShadows = jv.value("castShadows", true);
            vegetation.grassCastShadows = jv.value("grassCastShadows", false);
            vegetation.treeCount = std::clamp(jv.value("treeCount", 120), 0, 20000);
            vegetation.grassCount = std::clamp(jv.value("grassCount", 2000), 0, 50000);
            vegetation.treeMinScale = jv.value("treeMinScale", 0.75f);
            vegetation.treeMaxScale = jv.value("treeMaxScale", 1.45f);
            vegetation.grassMinScale = jv.value("grassMinScale", 0.65f);
            vegetation.grassMaxScale = jv.value("grassMaxScale", 1.25f);
            vegetation.slopeLimit = jv.value("slopeLimit", 0.65f);
            vegetation.seed = jv.value("seed", 1337u);
            if (jv.contains("trunkColor")) vegetation.trunkColor = Vec4FromJson(jv.at("trunkColor"));
            if (jv.contains("canopyColor")) vegetation.canopyColor = Vec4FromJson(jv.at("canopyColor"));
            if (jv.contains("grassColor")) vegetation.grassColor = Vec4FromJson(jv.at("grassColor"));
            if (const auto* landscape = reg.try_get<LandscapeComponent>(e.Handle())) {
                VegetationGenerator::Generate(*landscape, vegetation);
            }
        }

        // Cloth meshes are generated from this data, so parse it before the
        // mesh component is rebuilt.
        if (je.contains("softBody")) {
            const auto& js = je["softBody"];
            auto& softBody = e.AddComponent<SoftBodyComponent>();
            softBody.gridX = std::max(2, js.value("gridX", 10));
            softBody.gridY = std::max(2, js.value("gridY", 10));
            softBody.spacing = js.value("spacing", 0.2f);
            softBody.origin = js.contains("origin") ? Vec3FromJson(js.at("origin")) : glm::vec3(0.0f, 3.0f, 0.0f);
            softBody.stiffness = js.value("stiffness", 0.8f);
            if (js.contains("vertices") && js["vertices"].is_array()) {
                softBody.vertices.clear();
                for (const auto& vertex : js["vertices"]) softBody.vertices.push_back(Vec3FromJson(vertex));
            }
            if (js.contains("indices") && js["indices"].is_array()) {
                softBody.indices.clear();
                for (const auto& index : js["indices"]) softBody.indices.push_back(index.get<uint32_t>());
            }
            softBody.softBodyId = 0xFFFFFFFFu;
        }

        if (je.contains("mesh")) {
            const auto& jm = je["mesh"];
            const std::string primitive = jm.value("primitiveType", std::string{});
            const std::string source = jm.value("sourceAssetPath", std::string{});
            const int meshIndex = jm.value("meshIndex", 0);
            std::shared_ptr<Mesh> mesh;
            if (primitive == "cube") {
                mesh = m_assets.CreateCubeMesh();
            } else if (primitive == "sphere") {
                mesh = m_assets.CreateSphereMesh();
            } else if (primitive == "capsule") {
                // Capsule dimensions are baked into the mesh at creation time;
                // keep a sensible default for legacy files without parameters.
                mesh = m_assets.CreateCapsuleMesh(0.5f, 0.5f);
            } else if (primitive == "landscape") {
                if (const auto* landscape = reg.try_get<LandscapeComponent>(e.Handle())) {
                    mesh = std::make_shared<Mesh>();
                    auto copy = *landscape;
                    LandscapeMesh::Rebuild(copy, *mesh);
                }
            } else if (primitive == "cloth") {
                if (const auto* softBody = reg.try_get<SoftBodyComponent>(e.Handle())) {
                    mesh = BuildClothMesh(*softBody);
                }
            } else if (!source.empty()) {
                auto model = m_assets.LoadModel(m_assets.MakeRelativePath(source));
                if (meshIndex >= 0 && meshIndex < static_cast<int>(model.meshes.size())) {
                    mesh = model.meshes[static_cast<size_t>(meshIndex)];
                }
            }
            if (mesh && mesh->Valid()) {
                e.AddComponent<MeshComponent>(MeshComponent{ mesh, source, primitive, meshIndex });
            }
        }

        std::shared_ptr<Material> material;
        if (je.contains("materialId") && je["materialId"].is_number_integer()) {
            const int materialId = je["materialId"].get<int>();
            if (materialId >= 0 && materialId < static_cast<int>(materialLibrary.size())) {
                material = materialLibrary[static_cast<size_t>(materialId)];
            }
        }

        // Fall back to the per-entity material block for older scene files
        // (or if the material table was stripped by an external tool).
        if (!material && je.contains("material") && je["material"].is_object()) {
            std::string meshSource;
            if (je.contains("mesh")) {
                meshSource = je["mesh"].value("sourceAssetPath", std::string{});
            }
            material = MaterialFromJson(je["material"], m_assets, meshSource);
        }

        if (material) {
            auto& materialComponent = e.AddComponent<MaterialComponent>(MaterialComponent{ material });
            materialComponent.isGlass = material->isGlass;
        }

        if (je.contains("skeletal")) {
            const auto& js = je["skeletal"];
            const std::string source = js.value("sourcePath", std::string{});
            auto& skel = e.AddComponent<SkeletalComponent>();
            skel.isAnimated = js.value("isAnimated", true);
            skel.sourcePath = source;
            skel.currentTime = js.value("currentTime", 0.0f);
            skel.animationSpeed = js.value("animationSpeed", 1.0f);
            skel.activeAnimation = js.value("activeAnimation", 0);
            skel.boneMatrices.assign(256, glm::mat4(1.0f));
            if (!source.empty() && skel.isAnimated) {
                auto model = m_assets.LoadModel(source);
                if (model.hasAnimation) {
                    m_assets.UpdateAnimation(model, skel.currentTime, skel.boneMatrices);
                }
            }
        }
        if (je.contains("animationStateMachine")) {
            const auto& jm = je["animationStateMachine"];
            auto& machine = e.AddComponent<AnimationStateMachineComponent>();
            machine.enabled = jm.value("enabled", true);
            machine.currentState = jm.value("currentState", 0);
            machine.playbackSpeed = jm.value("playbackSpeed", 1.0f);
            machine.blendDuration = jm.value("blendDuration", 0.25f);
            if (jm.contains("states") && jm["states"].is_array()) {
                for (const auto& js : jm["states"]) {
                    AnimationState state;
                    state.name = js.value("name", std::string("State"));
                    state.clip = js.value("clip", 0);
                    state.loop = js.value("loop", true);
                    state.speed = js.value("speed", 1.0f);
                    machine.states.push_back(std::move(state));
                }
            }
            if (jm.contains("transitions") && jm["transitions"].is_array()) {
                for (const auto& jt : jm["transitions"]) {
                    AnimationTransition transition;
                    transition.fromState = jt.value("fromState", -1);
                    transition.toState = jt.value("toState", 0);
                    transition.parameter = jt.value("parameter", std::string{});
                    transition.condition = static_cast<AnimationCondition>(
                        std::clamp(jt.value("condition", 0), 0, 5));
                    transition.threshold = jt.value("threshold", 0.5f);
                    transition.duration = jt.value("duration", 0.25f);
                    transition.enabled = jt.value("enabled", true);
                    machine.transitions.push_back(std::move(transition));
                }
            }
            if (jm.contains("parameters") && jm["parameters"].is_array()) {
                for (const auto& jp : jm["parameters"]) {
                    machine.parameters.push_back({
                        jp.value("name", std::string{}),
                        jp.value("value", 0.0f)
                    });
                }
            }
            machine.previousState = -1;
            machine.blendElapsed = machine.blendDuration;
            machine.initialized = false;
        }

        if (je.contains("camera")) {
            const auto& jc = je["camera"];
            auto& camera = e.AddComponent<CameraComponent>();
            camera.fovY = jc.value("fovY", 60.0f);
            camera.nearZ = jc.value("nearZ", 0.1f);
            camera.farZ = jc.value("farZ", 1000.0f);
            camera.primary = jc.value("primary", true);
        }

        if (je.contains("directionalLight")) {
            const auto& jl = je["directionalLight"];
            auto& dl = e.AddComponent<DirectionalLightComponent>();
            dl.direction = Vec3FromJson(jl.at("direction"));
            dl.color = Vec3FromJson(jl.at("color"));
            dl.intensity = jl.value("intensity", 1.0f);
        }
        if (je.contains("pointLight")) {
            const auto& jl = je["pointLight"];
            auto& pl = e.AddComponent<PointLightComponent>();
            pl.color = Vec3FromJson(jl.at("color"));
            pl.intensity = jl.value("intensity", 1.0f);
            pl.radius = jl.value("radius", 10.0f);
        }
        if (je.contains("spotLight")) {
            const auto& jl = je["spotLight"];
            auto& sl = e.AddComponent<SpotLightComponent>();
            sl.direction = jl.contains("direction") ? Vec3FromJson(jl.at("direction")) : glm::vec3(0.0f, -1.0f, 0.0f);
            sl.innerConeAngle = jl.value("innerConeAngle", 15.0f);
            sl.outerConeAngle = jl.value("outerConeAngle", 30.0f);
            sl.radius = jl.value("radius", 10.0f);
            sl.color = jl.contains("color") ? Vec3FromJson(jl.at("color")) : glm::vec3(1.0f);
            sl.intensity = jl.value("intensity", 1.0f);
        }
        if (je.contains("audioSource")) {
            const auto& ja = je["audioSource"];
            auto& audio = e.AddComponent<AudioSourceComponent>();
            audio.soundPath = ja.value("soundPath", std::string{});
            audio.loop = ja.value("loop", false);
            audio.autoplay = ja.value("autoplay", false);
            audio.playing = false;
        }
        if (je.contains("uiCanvas") && je["uiCanvas"].is_object()) {
            const auto& jc = je["uiCanvas"];
            auto& uiCanvas = e.AddComponent<UICanvasComponent>().canvas;
            uiCanvas.canvasId = jc.value("canvasId", std::string("Canvas"));
            uiCanvas.title = jc.value("title", std::string("UI"));
            if (jc.contains("screenPosition") && jc["screenPosition"].is_array() &&
                jc["screenPosition"].size() >= 2) {
                uiCanvas.screenPosition = {jc["screenPosition"][0].get<float>(),
                                           jc["screenPosition"][1].get<float>()};
            }
            if (jc.contains("screenSize") && jc["screenSize"].is_array() &&
                jc["screenSize"].size() >= 2) {
                uiCanvas.screenSize = {jc["screenSize"][0].get<float>(),
                                       jc["screenSize"][1].get<float>()};
            }
            uiCanvas.resizable = jc.value("resizable", true);
            uiCanvas.movable = jc.value("movable", true);
            uiCanvas.closeable = jc.value("closeable", true);
            uiCanvas.visible = jc.value("visible", true);
            uiCanvas.backgroundAlpha = jc.value("backgroundAlpha", 1.0f);
            uiCanvas.renderMode = static_cast<UICanvasRenderMode>(
                std::clamp(jc.value("renderMode", 0), 0, 2));
            if (jc.contains("referenceResolution") &&
                jc["referenceResolution"].is_array() &&
                jc["referenceResolution"].size() >= 2) {
                uiCanvas.referenceResolution = {
                    jc["referenceResolution"][0].get<int>(),
                    jc["referenceResolution"][1].get<int>()};
            }
            uiCanvas.scaleMode = static_cast<UICanvasScaleMode>(
                std::clamp(jc.value("scaleMode", 0), 0, 1));
            uiCanvas.matchWidthOrHeight = jc.value("matchWidthOrHeight", 0.5f);
            uiCanvas.pixelPerfect = jc.value("pixelPerfect", false);
            uiCanvas.sortOrder = jc.value("sortOrder", 0);
            uiCanvas.onMenuOpen = jc.value("onMenuOpen", std::string{});
            uiCanvas.onMenuHover = jc.value("onMenuHover", std::string{});
            uiCanvas.onMenuClick = jc.value("onMenuClick", std::string{});
            if (jc.contains("widgets") && jc["widgets"].is_array()) {
                for (const auto& jw : jc["widgets"]) {
                    UIWidget widget;
                    widget.id = jw.value("id", std::string("widget"));
                    widget.label = jw.value("label", std::string("Widget"));
                    widget.type = static_cast<UIWidgetType>(
                        std::clamp(jw.value("type", 0), 0, 12));
                    widget.parent = jw.value("parent", -1);
                    widget.zOrder = jw.value("zOrder", 0);
                    if (jw.contains("position") && jw["position"].is_array() &&
                        jw["position"].size() >= 2) {
                        widget.position = {jw["position"][0].get<float>(),
                                           jw["position"][1].get<float>()};
                    }
                    if (jw.contains("size") && jw["size"].is_array() &&
                        jw["size"].size() >= 2) {
                        widget.size = {jw["size"][0].get<float>(),
                                       jw["size"][1].get<float>()};
                    }
                    widget.anchorPreset = static_cast<UIAnchorPreset>(
                        std::clamp(jw.value("anchorPreset", 0), 0, 11));
                    if (jw.contains("anchorMin") && jw["anchorMin"].is_array() &&
                        jw["anchorMin"].size() >= 2) {
                        widget.anchorMin = {jw["anchorMin"][0].get<float>(),
                                            jw["anchorMin"][1].get<float>()};
                    }
                    if (jw.contains("anchorMax") && jw["anchorMax"].is_array() &&
                        jw["anchorMax"].size() >= 2) {
                        widget.anchorMax = {jw["anchorMax"][0].get<float>(),
                                            jw["anchorMax"][1].get<float>()};
                    }
                    if (jw.contains("pivot") && jw["pivot"].is_array() &&
                        jw["pivot"].size() >= 2) {
                        widget.pivot = {jw["pivot"][0].get<float>(),
                                        jw["pivot"][1].get<float>()};
                    }
                    widget.visible = jw.value("visible", true);
                    widget.enabled = jw.value("enabled", true);
                    widget.interactable = jw.value("interactable", true);
                    widget.blockInput = jw.value("blockInput", true);
                    widget.locked = jw.value("locked", false);
                    widget.opacity = jw.value("opacity", 1.0f);
                    widget.buttonHeight = jw.value("buttonHeight", 30.0f);
                    widget.sliderMin = jw.value("sliderMin", 0.0f);
                    widget.sliderMax = jw.value("sliderMax", 1.0f);
                    widget.sliderValue = jw.value("sliderValue", 0.5f);
                    widget.checked = jw.value("checked", false);
                    widget.maxChars = jw.value("maxChars", 128);
                    widget.imagePath = jw.value("imagePath", std::string{});
                    widget.useColor = jw.value("useColor", false);
                    if (jw.contains("color")) widget.color = Vec4FromJson(jw.at("color"));
                    widget.radius = jw.value("radius", 0.0f);
                    widget.border = jw.value("border", 0.0f);
                    widget.onShow = jw.value("onShow", std::string{});
                    widget.onHover = jw.value("onHover", std::string{});
                    widget.onClick = jw.value("onClick", std::string{});
                    uiCanvas.widgets.push_back(std::move(widget));
                }
            }
        }

        if (je.contains("rigidBody")) {
            const auto& jr = je["rigidBody"];
            auto& rb = e.AddComponent<RigidBodyComponent>();
            std::string motionType = jr.value("motionType", std::string("Dynamic"));
            if (motionType == "Static") rb.motionType = RigidBodyComponent::MotionType::Static;
            else if (motionType == "Kinematic") rb.motionType = RigidBodyComponent::MotionType::Kinematic;
            else rb.motionType = RigidBodyComponent::MotionType::Dynamic;
            rb.mass = jr.value("mass", 1.0f);
            rb.friction = jr.value("friction", 0.5f);
            rb.restitution = jr.value("restitution", 0.0f);
            rb.useGravity = jr.value("useGravity", true);
            rb.bodyId = 0xFFFFFFFFu;
        }
        if (je.contains("boxCollider")) {
            const auto& jc = je["boxCollider"];
            auto& collider = e.AddComponent<BoxColliderComponent>();
            if (jc.contains("halfExtents")) collider.halfExtents = Vec3FromJson(jc.at("halfExtents"));
        }
        if (je.contains("navAgent")) {
            const auto& ja = je["navAgent"];
            auto& agent = e.AddComponent<NavAgentComponent>();
            if (ja.contains("path") && ja["path"].is_array()) {
                agent.path.clear();
                for (const auto& point : ja["path"]) agent.path.push_back(Vec3FromJson(point));
            }
            agent.currentWaypoint = ja.value("currentWaypoint", 0ull);
            agent.speed = ja.value("speed", 3.0f);
            agent.stoppingDistance = ja.value("stoppingDistance", 0.5f);
            if (ja.contains("targetPosition")) agent.targetPosition = Vec3FromJson(ja.at("targetPosition"));
            agent.hasTarget = ja.value("hasTarget", false);
        }
        if (je.contains("aiController")) {
            const auto& jc = je["aiController"];
            auto& controller = e.AddComponent<AIControllerComponent>();
            controller.scriptPath = jc.value("scriptPath", std::string("assets/scripts/ai_controller.lua"));
        }
        if (je.contains("perception")) {
            const auto& jp = je["perception"];
            auto& perception = e.AddComponent<PerceptionComponent>();
            perception.sightRadius = jp.value("sightRadius", 20.0f);
            perception.fieldOfViewDegrees = jp.value("fieldOfViewDegrees", 110.0f);
            perception.hearingRadius = jp.value("hearingRadius", 8.0f);
            perception.eyeHeight = jp.value("eyeHeight", 1.6f);
            perception.detectionInterval = jp.value("detectionInterval", 0.15f);
            perception.memoryDuration = jp.value("memoryDuration", 4.0f);
            perception.requireLineOfSight = jp.value("requireLineOfSight", true);
            perception.detectPlayerOnly = jp.value("detectPlayerOnly", true);
            perception.targetName = jp.value("targetName", std::string("Player"));
            perception.canSeeTarget = false;
            perception.targetEntity = entt::null;
            perception.timeSinceSeen = 1000.0f;
        }
        if (je.contains("behaviorTree")) {
            const auto& jb = je["behaviorTree"];
            auto& tree = e.AddComponent<BehaviorTreeComponent>();
            tree.enabled = jb.value("enabled", true);
            tree.root = jb.value("root", 0);
            tree.tickInterval = jb.value("tickInterval", 0.0f);
            if (jb.contains("nodes") && jb["nodes"].is_array()) {
                for (const auto& jn : jb["nodes"]) {
                    BehaviorTreeNode node;
                    node.name = jn.value("name", std::string("Node"));
                    node.type = static_cast<BehaviorNodeType>(
                        std::clamp(jn.value("type", 0), 0, 3));
                    node.condition = static_cast<BehaviorCondition>(
                        std::clamp(jn.value("condition", 0), 0, 5));
                    node.action = static_cast<BehaviorAction>(
                        std::clamp(jn.value("action", 0), 0, 8));
                    if (jn.contains("children") && jn["children"].is_array()) {
                        for (const auto& child : jn["children"]) {
                            node.children.push_back(child.get<int>());
                        }
                    }
                    node.range = jn.value("range", 2.0f);
                    node.duration = jn.value("duration", 1.0f);
                    node.radius = jn.value("radius", 8.0f);
                    node.parameter = jn.value("parameter", std::string{});
                    node.value = jn.value("value", std::string{});
                    tree.nodes.push_back(std::move(node));
                }
            }
            tree.statusText = "Idle";
        }
        if (je.value("playerTag", false)) {
            e.AddComponent<PlayerTagComponent>();
        }
        if (je.value("luaEntity", false)) {
            e.AddComponent<LuaEntityComponent>();
        }
    }

    auto findParentByName = [&](const std::string& childName) -> entt::entity {
        entt::entity best = entt::null;
        size_t bestLength = 0;
        for (const auto& candidate : loaded) {
            if (candidate.name == childName || !LooksLikeChildName(childName, candidate.name)) continue;
            if (candidate.name.size() > bestLength) {
                best = candidate.entity.Handle();
                bestLength = candidate.name.size();
            }
        }
        return best;
    };

    for (auto entryIt = loaded.rbegin(); entryIt != loaded.rend(); ++entryIt) {
        auto& entry = *entryIt;
        if (!entry.hasParent) continue;

        entt::entity parent = entt::null;
        if (formatVersion >= kSceneFormatVersion) {
            const auto it = savedIdToEntity.find(static_cast<int>(entry.parentValue));
            if (it != savedIdToEntity.end()) parent = it->second;
        } else {
            const uint32_t oldIndex = entt::entt_traits<entt::entity>::to_entity(
                static_cast<entt::entity>(entry.parentValue));
            const auto it = legacyHandleToEntity.find(oldIndex);
            if (it != legacyHandleToEntity.end()) parent = it->second;
            else if (oldIndex < loaded.size()) parent = loaded[oldIndex].entity.Handle();
        }

        if (parent == entry.entity.Handle()) parent = entt::null;

        // Turn the broken legacy handle references of character sub-meshes
        // back into their named root.  This also repairs files that were
        // saved after an earlier broken load.
        if (formatVersion < kSceneFormatVersion) {
            const entt::entity namedParent = findParentByName(entry.name);
            if (namedParent != entt::null &&
                (parent == entt::null ||
                 (m_scene.Registry().all_of<NameComponent>(parent) &&
                  !LooksLikeChildName(entry.name, m_scene.Registry().get<NameComponent>(parent).name)))) {
                parent = namedParent;
            }
        }

        if (parent != entt::null && m_scene.Registry().valid(parent)) {
            m_scene.Registry().emplace_or_replace<ParentComponent>(
                entry.entity.Handle(), ParentComponent{ parent });
        }
    }

    // A malformed file must not create a parent cycle: several runtime
    // systems walk parent chains without a depth guard.
    for (auto& entry : loaded) {
        auto& reg = m_scene.Registry();
        if (!reg.all_of<ParentComponent>(entry.entity.Handle())) continue;
        std::unordered_set<entt::entity> visited;
        entt::entity current = entry.entity.Handle();
        bool invalid = false;
        while (reg.all_of<ParentComponent>(current)) {
            if (!visited.insert(current).second) {
                invalid = true;
                break;
            }
            current = reg.get<ParentComponent>(current).parent;
            if (!reg.valid(current)) {
                invalid = true;
                break;
            }
        }
        if (invalid) {
            reg.remove<ParentComponent>(entry.entity.Handle());
            spdlog::warn("[Scene] Removed invalid parent from '{}'", entry.name);
        }
    }

    spdlog::info("[Scene] Loaded {} entities (format {})", loaded.size(), formatVersion);
    return true;
}

} // namespace Galeshi
