#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <fstream>
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

        if (reg.all_of<MeshComponent>(handle)) {
            const auto& mc = reg.get<MeshComponent>(handle);
            je["mesh"] = { {"sourceAssetPath", mc.sourceAssetPath}, {"primitiveType", mc.primitiveType}, {"meshIndex", mc.meshIndex} };
        }
        if (reg.all_of<MaterialComponent>(handle)) {
            const auto& matc = reg.get<MaterialComponent>(handle);
            if (matc.material) {
                const auto& m = *matc.material;
                je["material"] = {
                    {"name", m.name},
                    {"sourcePath", m.sourcePath},
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
                    {"albedoMapPath", TexturePath(m.albedoMap)},
                    {"normalMapPath", TexturePath(m.normalMap)},
                    {"mrMapPath", TexturePath(m.metallicRoughnessMap)},
                    {"metallicMapPath", TexturePath(m.metallicMap)},
                    {"roughnessMapPath", TexturePath(m.roughnessMap)},
                    {"emissiveMapPath", TexturePath(m.emissiveMap)},
                    {"occlusionMapPath", TexturePath(m.occlusionMap)},
                    {"opacityMapPath", TexturePath(m.opacityMap)},
                    {"specularMapPath", TexturePath(m.specularMap)}
                };
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

bool SceneSerializer::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) return false;
    std::stringstream ss;
    ss << in.rdbuf();
    return LoadFromString(ss.str());
}

bool SceneSerializer::LoadFromString(const std::string& jsonStr) {
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
    std::vector<LoadedEntity> loaded;
    loaded.reserve(jsonEntities.size());

    // SaveToString writes entities in EnTT view order, which is the reverse
    // of creation order.  Create them in reverse here so that after loading a
    // view visits entities in exactly the same order as before saving.  Some
    // systems (for example the directional-light selection) rely on the first
    // entity returned by a view being stable.
    for (auto it = jsonEntities.rbegin(); it != jsonEntities.rend(); ++it) {
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
                auto model = m_assets.LoadModel(source);
                if (meshIndex >= 0 && meshIndex < static_cast<int>(model.meshes.size())) {
                    mesh = model.meshes[static_cast<size_t>(meshIndex)];
                }
            }
            if (mesh && mesh->Valid()) {
                e.AddComponent<MeshComponent>(MeshComponent{ mesh, source, primitive, meshIndex });
            }
        }

        if (je.contains("material")) {
            const auto& jm = je["material"];
            auto material = std::make_shared<Material>();
            material->name = jm.value("name", std::string("Material"));
            material->sourcePath = jm.value("sourcePath", std::string{});
            material->albedoFactor = jm.contains("albedoFactor") ? Vec4FromJson(jm.at("albedoFactor")) : glm::vec4(1.0f);
            material->metallicFactor = jm.value("metallicFactor", 0.0f);
            material->roughnessFactor = jm.value("roughnessFactor", 0.5f);
            material->emissiveFactor = jm.contains("emissiveFactor") ? Vec3FromJson(jm.at("emissiveFactor")) : glm::vec3(0.0f);
            material->isGlass = jm.value("isGlass", false);
            // Older scenes had no two-sided switch and the renderer mirrored
            // backface normals unconditionally. Default missing values to
            // true so loaded scenes keep their previous appearance.
            material->twoSided = jm.value("twoSided", true);
            material->glassOpacity = jm.value("glassOpacity", 0.35f);
            material->normalStrength = jm.value("normalStrength", 1.0f);
            material->occlusionStrength = jm.value("occlusionStrength", 1.0f);
            material->alphaCutoff = jm.value("alphaCutoff", 0.5f);

            std::string meshSource;
            std::string materialSource = material->sourcePath;
            if (je.contains("mesh")) {
                meshSource = je["mesh"].value("sourceAssetPath", std::string{});
            }
            if (!meshSource.empty()) {
                // Ensure embedded model textures are registered even if this
                // entity has no mesh component of its own.
                m_assets.LoadModel(meshSource);
            }

            auto loadMap = [&](const char* key, bool srgb) -> std::shared_ptr<Texture> {
                const std::string path = jm.value(key, std::string{});
                if (path.empty()) return nullptr;

                auto load = [&](const std::string& candidate) -> std::shared_ptr<Texture> {
                    if (candidate.empty()) return nullptr;
                    auto texture = m_assets.LoadTexture(candidate, srgb);
                    return (texture && texture != m_assets.WhiteTexture()) ? texture : nullptr;
                };

                auto texture = load(path);
                if (!texture) {
                    const std::string& modelSource = !meshSource.empty() ? meshSource : materialSource;
                    if (!modelSource.empty()) {
                        const auto modelDir = std::filesystem::path(modelSource).parent_path();
                        texture = load((modelDir / path).string());
                        if (!texture) {
                            texture = load((modelDir / std::filesystem::path(path).filename()).string());
                        }
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
