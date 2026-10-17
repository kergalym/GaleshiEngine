#include "GaleshiEngine/Scene/SceneSerializer.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"

#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <fstream>
#include <glm/gtc/quaternion.hpp>

using json = nlohmann::json;

namespace Galeshi {

// -----------------------------------------------------------------
// Утилиты для glm
// -----------------------------------------------------------------
static json Vec3ToJson(const glm::vec3& v) {
    return json::array({v.x, v.y, v.z});
}
static glm::vec3 Vec3FromJson(const json& j) {
    return { j.at(0).get<float>(), j.at(1).get<float>(), j.at(2).get<float>() };
}
static json Vec4ToJson(const glm::vec4& v) {
    return json::array({v.x, v.y, v.z, v.w});
}
static glm::vec4 Vec4FromJson(const json& j) {
    return { j.at(0).get<float>(), j.at(1).get<float>(),
             j.at(2).get<float>(), j.at(3).get<float>() };
}
static json QuatToJson(const glm::quat& q) {
    return json::array({q.w, q.x, q.y, q.z});
}
static glm::quat QuatFromJson(const json& j) {
    return glm::quat(j.at(0).get<float>(), j.at(1).get<float>(),
                     j.at(2).get<float>(), j.at(3).get<float>());
}

// -----------------------------------------------------------------
// SAVE
// -----------------------------------------------------------------
bool SceneSerializer::Save(const std::string& path) const {
    json root;
    root["version"] = 1;
    root["entities"] = json::array();

    auto view = m_scene.Registry().view<NameComponent>();
    for (auto h : view) {
        json je;
        const auto& nc = view.get<NameComponent>(h);
        je["name"] = nc.name;

        if (m_scene.Registry().all_of<TransformComponent>(h)) {
            const auto& tc = m_scene.Registry().get<TransformComponent>(h);
            je["transform"] = {
                {"position", Vec3ToJson(tc.position)},
                {"rotation", QuatToJson(tc.rotation)},
                {"scale",    Vec3ToJson(tc.scale)},
            };
        }

        if (m_scene.Registry().all_of<MeshComponent>(h)) {
            const auto& mc = m_scene.Registry().get<MeshComponent>(h);
            je["mesh"] = {
                {"sourceAssetPath", mc.sourceAssetPath},
                {"primitiveType",   mc.primitiveType},
                {"meshIndex",       mc.meshIndex},
            };
        }

        if (m_scene.Registry().all_of<MaterialComponent>(h)) {
            const auto& matc = m_scene.Registry().get<MaterialComponent>(h);
            if (matc.material) {
                const auto& m = *matc.material;
                je["material"] = {
                    {"name",             m.name},
                    {"albedoFactor",     Vec4ToJson(m.albedoFactor)},
                    {"metallicFactor",   m.metallicFactor},
                    {"roughnessFactor",  m.roughnessFactor},
                    {"emissiveFactor",   Vec3ToJson(m.emissiveFactor)},
                    {"albedoMapPath",    m.albedoMap            ? m.albedoMap->Path() : std::string{}},
                    {"normalMapPath",    m.normalMap            ? m.normalMap->Path() : std::string{}},
                    {"mrMapPath",        m.metallicRoughnessMap ? m.metallicRoughnessMap->Path() : std::string{}},
                };
            }
        }

        if (m_scene.Registry().all_of<DirectionalLightComponent>(h)) {
            const auto& dl = m_scene.Registry().get<DirectionalLightComponent>(h);
            je["directionalLight"] = {
                {"direction", Vec3ToJson(dl.direction)},
                {"color",     Vec3ToJson(dl.color)},
                {"intensity", dl.intensity},
            };
        }

        root["entities"].push_back(std::move(je));
    }

    std::ofstream out(path);
    if (!out.is_open()) {
        spdlog::error("[SceneSerializer] Cannot open for write: {}", path);
        return false;
    }
    out << root.dump(2);
    spdlog::info("[SceneSerializer] Saved {} entities to {}",
                 root["entities"].size(), path);
    return true;
}

// -----------------------------------------------------------------
// LOAD
// -----------------------------------------------------------------
bool SceneSerializer::Load(const std::string& path) {
    std::ifstream in(path);
    if (!in.is_open()) {
        spdlog::error("[SceneSerializer] Cannot open: {}", path);
        return false;
    }

    json root;
    try {
        in >> root;
    } catch (const std::exception& e) {
        spdlog::error("[SceneSerializer] JSON parse: {}", e.what());
        return false;
    }

    // Очищаем сцену
    m_scene.Registry().clear();

    int loaded = 0;
    for (const auto& je : root.at("entities")) {
        auto e = m_scene.CreateEntity(je.value("name", "Entity"));

        if (je.contains("transform")) {
            auto& tc = e.GetComponent<TransformComponent>();
            const auto& jt = je["transform"];
            tc.position = Vec3FromJson(jt.at("position"));
            tc.rotation = QuatFromJson(jt.at("rotation"));
            tc.scale    = Vec3FromJson(jt.at("scale"));
        }

        if (je.contains("mesh")) {
            const auto& jm = je["mesh"];
            const std::string prim = jm.value("primitiveType", "");
            const std::string src  = jm.value("sourceAssetPath", "");

            const int mi = jm.value("meshIndex", 0);

            std::shared_ptr<Mesh> mesh;
            if (prim == "cube") {
                mesh = m_assets.CreateCubeMesh();
            } else if (!src.empty()) {
                auto model = m_assets.LoadModel(src);
                if (mi >= 0 && mi < static_cast<int>(model.meshes.size()))
                    mesh = model.meshes[mi];
            }

            if (mesh) {
                e.AddComponent<MeshComponent>(
                    MeshComponent{ mesh, src, prim, mi });
            }
        }

        if (je.contains("material")) {
            const auto& jm = je["material"];
            auto mat = std::make_shared<Material>();
            mat->name            = jm.value("name", "Material");
            mat->albedoFactor    = Vec4FromJson(jm.at("albedoFactor"));
            mat->metallicFactor  = jm.value("metallicFactor",  0.0f);
            mat->roughnessFactor = jm.value("roughnessFactor", 0.5f);
            mat->emissiveFactor  = Vec3FromJson(jm.at("emissiveFactor"));

            const std::string aPath = jm.value("albedoMapPath", "");
            if (!aPath.empty()) mat->albedoMap = m_assets.LoadTexture(aPath);

            const std::string nPath = jm.value("normalMapPath", "");
            if (!nPath.empty()) mat->normalMap = m_assets.LoadTexture(nPath);

            const std::string mPath = jm.value("mrMapPath", "");
            if (!mPath.empty()) mat->metallicRoughnessMap = m_assets.LoadTexture(mPath);

            e.AddComponent<MaterialComponent>(MaterialComponent{ mat });
        }

        if (je.contains("directionalLight")) {
            const auto& jl = je["directionalLight"];
            auto& dl = e.AddComponent<DirectionalLightComponent>();
            dl.direction = Vec3FromJson(jl.at("direction"));
            dl.color     = Vec3FromJson(jl.at("color"));
            dl.intensity = jl.value("intensity", 1.0f);
        }

        ++loaded;
    }

    spdlog::info("[SceneSerializer] Loaded {} entities from {}", loaded, path);
    return true;
}

} // namespace Galeshi
