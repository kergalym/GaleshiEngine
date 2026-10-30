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

    root["audioMixer"] = json::array();
    for (int i = 0; i < kAudioBusCount; ++i) {
        const auto& bus = m_scene.AudioBuses()[static_cast<size_t>(i)];
        root["audioMixer"].push_back({
            {"name", AudioBusName(static_cast<AudioBus>(i))},
            {"volume", bus.volume},
            {"muted", bus.muted}
        });
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
        if (reg.all_of<ParticleEmitterComponent>(handle)) {
            const auto& pe = reg.get<ParticleEmitterComponent>(handle);
            je["particleEmitter"] = {
                {"enabled", pe.enabled},
                {"emitRate", pe.emitRate},
                {"lifetime", pe.lifetime},
                {"lifetimeVariance", pe.lifetimeVariance},
                {"startSize", pe.startSize},
                {"endSize", pe.endSize},
                {"startColor", Vec4ToJson(pe.startColor)},
                {"endColor", Vec4ToJson(pe.endColor)},
                {"velocity", Vec3ToJson(pe.velocity)},
                {"velocityVariance", pe.velocityVariance},
                {"spawnRadius", pe.spawnRadius},
                {"spawnBoxSize", Vec3ToJson(pe.spawnBoxSize)},
                {"gravity", Vec3ToJson(pe.gravity)},
                {"drag", pe.drag},
                {"maxParticles", pe.maxParticles},
                {"particleTexturePath", pe.texture
                    ? m_assets.MakeRelativePath(pe.texture->Path())
                    : std::string{}}
            };
        }
        if (reg.all_of<AudioSourceComponent>(handle)) {
            const auto& audio = reg.get<AudioSourceComponent>(handle);
            je["audioSource"] = {
                {"soundPath", audio.soundPath},
                {"loop", audio.loop},
                {"autoplay", audio.autoplay},
                {"spatial", audio.spatial},
                {"bus", static_cast<int>(audio.bus)},
                {"attenuation", static_cast<int>(audio.attenuation)},
                {"volume", audio.volume},
                {"pitch", audio.pitch},
                {"minDistance", audio.minDistance},
                {"maxDistance", audio.maxDistance},
                {"rolloff", audio.rolloff},
                {"dopplerFactor", audio.dopplerFactor},
                {"offset", Vec3ToJson(audio.offset)}
            };
        }
        if (reg.all_of<AudioListenerComponent>(handle)) {
            const auto& listener = reg.get<AudioListenerComponent>(handle);
            je["audioListener"] = { {"primary", listener.primary} };
        }
        if (reg.all_of<PostProcessVolumeComponent>(handle)) {
            const auto& volume = reg.get<PostProcessVolumeComponent>(handle);
            const auto& s = volume.settings;
            je["postProcessVolume"] = {
                {"enabled", volume.enabled},
                {"global", volume.global},
                {"priority", volume.priority},
                {"size", Vec3ToJson(volume.size)},
                {"visibleInEditor", volume.visibleInEditor},
                {"settings", {
                    {"bloomEnabled", s.bloomEnabled},
                    {"bloomThreshold", s.bloomThreshold},
                    {"bloomIntensity", s.bloomIntensity},
                    {"bloomRadius", s.bloomRadius},
                    {"dofEnabled", s.dofEnabled},
                    {"dofAutoFocus", s.dofAutoFocus},
                    {"dofFocusDistance", s.dofFocusDistance},
                    {"dofBlur", s.dofBlur},
                    {"motionBlurEnabled", s.motionBlurEnabled},
                    {"motionBlurAmount", s.motionBlurAmount},
                    {"motionBlurMaxVelocity", s.motionBlurMaxVelocity},
                    {"exposure", s.exposure},
                    {"contrast", s.contrast},
                    {"saturation", s.saturation},
                    {"colorExposure", s.colorExposure},
                    {"colorContrast", s.colorContrast},
                    {"colorSaturation", s.colorSaturation},
                    {"vignetteIntensity", s.vignetteIntensity},
                    {"vignetteSmoothness", s.vignetteSmoothness},
                    {"chromaticAberration", s.chromaticAberration},
                    {"ssaoEnabled", s.ssaoEnabled},
                    {"aoMode", static_cast<int>(s.aoMode)},
                    {"ssaoRadius", s.ssaoRadius},
                    {"ssaoBias", s.ssaoBias},
                    {"ssaoMaxDistance", s.ssaoMaxDistance},
                    {"ssaoStrength", s.ssaoStrength},
                    {"aoRadius", s.aoRadius},
                    {"aoBias", s.aoBias},
                    {"aoThickness", s.aoThickness},
                    {"aoMaxDistance", s.aoMaxDistance},
                    {"aoStrength", s.aoStrength},
                    {"aoDirections", s.aoDirections},
                    {"aoSteps", s.aoSteps},
                    {"ssrEnabled", s.ssrEnabled},
                    {"ssrIntensity", s.ssrIntensity},
                    {"fxaaEnabled", s.fxaaEnabled},
                    {"smaaEnabled", s.smaaEnabled},
                    {"taaEnabled", s.taaEnabled},
                    {"taaBlend", s.taaBlend},
                    {"godRaysEnabled", s.godRaysEnabled},
                    {"godRaysIntensity", s.godRaysIntensity},
                    {"godRaysDecay", s.godRaysDecay},
                    {"godRaysDensity", s.godRaysDensity},
                    {"godRaysWeight", s.godRaysWeight},
                    {"atmosphereEnabled", s.atmosphereEnabled},
                    {"atmosphereIntensity", s.atmosphereIntensity},
                    {"invertEnabled", s.invertEnabled}
                }}
            };
        }
        if (reg.all_of<WaterComponent>(handle)) {
            const auto& w = reg.get<WaterComponent>(handle);
            je["water"] = {
                {"enabled", w.enabled},
                {"visibleInEditor", w.visibleInEditor},
                {"width", w.width},
                {"depth", w.depth},
                {"waveHeight", w.waveHeight},
                {"waveScale", w.waveScale},
                {"waveSpeed", w.waveSpeed},
                {"choppiness", w.choppiness},
                {"detailStrength", w.detailStrength},
                {"flowDirection", {w.flowDirection.x, w.flowDirection.y}},
                {"refractionStrength", w.refractionStrength},
                {"reflectionStrength", w.reflectionStrength},
                {"fresnelPower", w.fresnelPower},
                {"depthFade", w.depthFade},
                {"absorption", w.absorption},
                {"clarity", w.clarity},
                {"transparency", w.transparency},
                {"foamDistance", w.foamDistance},
                {"foamIntensity", w.foamIntensity},
                {"sparkle", w.sparkle},
                {"shallowColor", Vec3ToJson(w.shallowColor)},
                {"deepColor", Vec3ToJson(w.deepColor)},
                {"foamColor", Vec3ToJson(w.foamColor)}
            };
        }
        if (reg.all_of<GroomComponent>(handle)) {
            const auto& g = reg.get<GroomComponent>(handle);
            je["groomHair"] = {
                {"enabled", g.enabled},
                {"simulate", g.simulate},
                {"visibleInEditor", g.visibleInEditor},
                {"targetMeshName", g.targetMeshName},
                {"guideCount", g.guideCount},
                {"strandsPerGuide", g.strandsPerGuide},
                {"segments", g.segments},
                {"length", g.length},
                {"width", g.width},
                {"rootWidth", g.rootWidth},
                {"tipWidth", g.tipWidth},
                {"clumpRadius", g.clumpRadius},
                {"clumping", g.clumping},
                {"frizz", g.frizz},
                {"curl", g.curl},
                {"volume", g.volume},
                {"scalpStart", g.scalpStart},
                {"scalpEnd", g.scalpEnd},
                {"normalThreshold", g.normalThreshold},
                {"groomDirection", Vec3ToJson(g.groomDirection)},
                {"gravity", g.gravity},
                {"stiffness", g.stiffness},
                {"damping", g.damping},
                {"windStrength", g.windStrength},
                {"windDirection", Vec3ToJson(g.windDirection)},
                {"windFrequency", g.windFrequency},
                {"specular", g.specular},
                {"shininess", g.shininess},
                {"ambient", g.ambient},
                {"opacity", g.opacity},
                {"randomTint", g.randomTint},
                {"rootColor", Vec3ToJson(g.rootColor)},
                {"tipColor", Vec3ToJson(g.tipColor)},
                {"seed", g.seed}
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
                {"useGravity", rb.useGravity},
                {"lockRotation", rb.lockRotation}
            };
        }
        if (reg.all_of<ColliderComponent>(handle)) {
            const auto& collider = reg.get<ColliderComponent>(handle);
            je["collider"] = {
                {"shapeType", static_cast<int>(collider.shapeType)},
                {"halfExtents", Vec3ToJson(collider.halfExtents)},
                {"radius", collider.radius},
                {"halfHeight", collider.halfHeight},
                {"offset", Vec3ToJson(collider.offset)},
                {"friction", collider.friction},
                {"restitution", collider.restitution},
                {"isTrigger", collider.isTrigger},
                {"visibleInEditor", collider.visibleInEditor}
            };
            // Keep the legacy key for older builds that only understand boxes.
            if (collider.shapeType == ColliderShapeType::Box) {
                je["boxCollider"] = { {"halfExtents", Vec3ToJson(collider.halfExtents)} };
            }
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
    if (!in.is_open()) {
        spdlog::error("[Scene] Could not open scene file: {}", path);
        return false;
    }
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

    // Start from defaults so scenes saved before the mixer existed do not
    // inherit bus state from the previously loaded scene.
    m_scene.AudioBuses().fill(AudioBusSettings{});

    if (root.contains("audioMixer") && root["audioMixer"].is_array()) {
        auto& buses = m_scene.AudioBuses();
        const auto& mixer = root["audioMixer"];
        for (size_t elementIndex = 0; elementIndex < mixer.size(); ++elementIndex) {
            const auto& jb = mixer[elementIndex];
            if (!jb.is_object()) continue;
            const std::string name = jb.value("name", std::string{});
            int index = -1;
            if (!name.empty()) {
                for (int i = 0; i < kAudioBusCount; ++i) {
                    if (name == AudioBusName(static_cast<AudioBus>(i))) {
                        index = i;
                        break;
                    }
                }
            }
            if (index < 0) {
                index = static_cast<int>(elementIndex);
            }
            if (index < 0 || index >= kAudioBusCount) continue;
            buses[static_cast<size_t>(index)].volume =
                std::clamp(jb.value("volume", 1.0f), 0.0f, 1.0f);
            buses[static_cast<size_t>(index)].muted =
                jb.value("muted", false);
        }
    }

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
        if (je.contains("particleEmitter") && je["particleEmitter"].is_object()) {
            const auto& jp = je["particleEmitter"];
            auto& pe = e.AddComponent<ParticleEmitterComponent>();
            pe.enabled = jp.value("enabled", true);
            pe.emitRate = jp.value("emitRate", 40.0f);
            pe.lifetime = jp.value("lifetime", 2.0f);
            pe.lifetimeVariance = jp.value("lifetimeVariance", 0.4f);
            pe.startSize = jp.value("startSize", 0.30f);
            pe.endSize = jp.value("endSize", 0.05f);
            if (jp.contains("startColor")) pe.startColor = Vec4FromJson(jp.at("startColor"));
            if (jp.contains("endColor")) pe.endColor = Vec4FromJson(jp.at("endColor"));
            if (jp.contains("velocity")) pe.velocity = Vec3FromJson(jp.at("velocity"));
            pe.velocityVariance = jp.value("velocityVariance", 1.2f);
            pe.spawnRadius = jp.value("spawnRadius", 0.2f);
            if (jp.contains("spawnBoxSize")) pe.spawnBoxSize = Vec3FromJson(jp.at("spawnBoxSize"));
            if (jp.contains("gravity")) pe.gravity = Vec3FromJson(jp.at("gravity"));
            pe.drag = jp.value("drag", 0.4f);
            pe.maxParticles = jp.value("maxParticles", 512u);
            const std::string texturePath = jp.value("particleTexturePath", std::string{});
            if (!texturePath.empty()) {
                pe.texture = m_assets.LoadTexture(texturePath, true);
            }
        }
        if (je.contains("audioSource") && je["audioSource"].is_object()) {
            const auto& ja = je["audioSource"];
            auto& audio = e.AddComponent<AudioSourceComponent>();
            audio.soundPath = ja.value("soundPath", std::string{});
            audio.loop = ja.value("loop", false);
            audio.autoplay = ja.value("autoplay", false);
            // Old scenes were 2D-only; keep them that way unless spatial was
            // saved explicitly.
            audio.spatial = ja.value("spatial", false);
            audio.bus = static_cast<AudioBus>(std::clamp(
                ja.value("bus", static_cast<int>(AudioBus::SFX)),
                0, kAudioBusCount - 1));
            audio.attenuation = static_cast<AudioAttenuationModel>(std::clamp(
                ja.value("attenuation", 0), 0, 2));
            audio.volume = ja.value("volume", 1.0f);
            audio.pitch = ja.value("pitch", 1.0f);
            audio.minDistance = ja.value("minDistance", 1.0f);
            audio.maxDistance = ja.value("maxDistance", 50.0f);
            audio.rolloff = ja.value("rolloff", 1.0f);
            audio.dopplerFactor = ja.value("dopplerFactor", 1.0f);
            if (ja.contains("offset")) audio.offset = Vec3FromJson(ja.at("offset"));
            audio.playing = false;
        }
        if (je.contains("audioListener") && je["audioListener"].is_object()) {
            auto& listener = e.AddComponent<AudioListenerComponent>();
            listener.primary = je["audioListener"].value("primary", true);
        }
        if (je.contains("postProcessVolume") &&
            je["postProcessVolume"].is_object()) {
            const auto& jv = je["postProcessVolume"];
            auto& volume = e.AddComponent<PostProcessVolumeComponent>();
            volume.enabled = jv.value("enabled", true);
            volume.global = jv.value("global", false);
            volume.priority = jv.value("priority", 0);
            if (jv.contains("size")) volume.size = Vec3FromJson(jv.at("size"));
            volume.visibleInEditor = jv.value("visibleInEditor", true);

            if (jv.contains("settings") && jv["settings"].is_object()) {
                const auto& js = jv["settings"];
                auto& s = volume.settings;
                s.bloomEnabled = js.value("bloomEnabled", s.bloomEnabled);
                s.bloomThreshold = js.value("bloomThreshold", s.bloomThreshold);
                s.bloomIntensity = js.value("bloomIntensity", s.bloomIntensity);
                s.bloomRadius = js.value("bloomRadius", s.bloomRadius);
                s.dofEnabled = js.value("dofEnabled", s.dofEnabled);
                s.dofAutoFocus = js.value("dofAutoFocus", s.dofAutoFocus);
                s.dofFocusDistance = js.value("dofFocusDistance", s.dofFocusDistance);
                s.dofBlur = js.value("dofBlur", s.dofBlur);
                s.motionBlurEnabled = js.value("motionBlurEnabled", s.motionBlurEnabled);
                s.motionBlurAmount = js.value("motionBlurAmount", s.motionBlurAmount);
                s.motionBlurMaxVelocity = js.value("motionBlurMaxVelocity", s.motionBlurMaxVelocity);
                s.exposure = js.value("exposure", s.exposure);
                s.contrast = js.value("contrast", s.contrast);
                s.saturation = js.value("saturation", s.saturation);
                s.colorExposure = js.value("colorExposure", s.colorExposure);
                s.colorContrast = js.value("colorContrast", s.colorContrast);
                s.colorSaturation = js.value("colorSaturation", s.colorSaturation);
                s.vignetteIntensity = js.value("vignetteIntensity", s.vignetteIntensity);
                s.vignetteSmoothness = js.value("vignetteSmoothness", s.vignetteSmoothness);
                s.chromaticAberration = js.value("chromaticAberration", s.chromaticAberration);
                s.ssaoEnabled = js.value("ssaoEnabled", s.ssaoEnabled);
                int aoMode = js.value("aoMode", static_cast<int>(s.aoMode));
                if (aoMode < 0) aoMode = 0;
                if (aoMode > 2) aoMode = 2;
                s.aoMode = static_cast<AmbientOcclusionMode>(aoMode);
                s.ssaoRadius = js.value("ssaoRadius", s.ssaoRadius);
                s.ssaoBias = js.value("ssaoBias", s.ssaoBias);
                s.ssaoMaxDistance = js.value("ssaoMaxDistance", s.ssaoMaxDistance);
                s.ssaoStrength = js.value("ssaoStrength", s.ssaoStrength);
                s.aoRadius = js.value("aoRadius", s.aoRadius);
                s.aoBias = js.value("aoBias", s.aoBias);
                s.aoThickness = js.value("aoThickness", s.aoThickness);
                s.aoMaxDistance = js.value("aoMaxDistance", s.aoMaxDistance);
                s.aoStrength = js.value("aoStrength", s.aoStrength);
                s.aoDirections = js.value("aoDirections", s.aoDirections);
                s.aoSteps = js.value("aoSteps", s.aoSteps);
                if (s.aoDirections < 1) s.aoDirections = 1;
                if (s.aoDirections > 16) s.aoDirections = 16;
                if (s.aoSteps < 1) s.aoSteps = 1;
                if (s.aoSteps > 16) s.aoSteps = 16;
                s.ssrEnabled = js.value("ssrEnabled", s.ssrEnabled);
                s.ssrIntensity = js.value("ssrIntensity", s.ssrIntensity);
                s.fxaaEnabled = js.value("fxaaEnabled", s.fxaaEnabled);
                s.smaaEnabled = js.value("smaaEnabled", s.smaaEnabled);
                s.taaEnabled = js.value("taaEnabled", s.taaEnabled);
                s.taaBlend = js.value("taaBlend", s.taaBlend);
                s.godRaysEnabled = js.value("godRaysEnabled", s.godRaysEnabled);
                s.godRaysIntensity = js.value("godRaysIntensity", s.godRaysIntensity);
                s.godRaysDecay = js.value("godRaysDecay", s.godRaysDecay);
                s.godRaysDensity = js.value("godRaysDensity", s.godRaysDensity);
                s.godRaysWeight = js.value("godRaysWeight", s.godRaysWeight);
                s.atmosphereEnabled = js.value("atmosphereEnabled", s.atmosphereEnabled);
                s.atmosphereIntensity = js.value("atmosphereIntensity", s.atmosphereIntensity);
                s.invertEnabled = js.value("invertEnabled", s.invertEnabled);
            }
        }
        if (je.contains("water") && je["water"].is_object()) {
            const auto& jw = je["water"];
            auto& water = e.AddComponent<WaterComponent>();
            water.enabled = jw.value("enabled", true);
            water.visibleInEditor = jw.value("visibleInEditor", true);
            water.width = jw.value("width", water.width);
            water.depth = jw.value("depth", water.depth);
            water.waveHeight = jw.value("waveHeight", water.waveHeight);
            water.waveScale = jw.value("waveScale", water.waveScale);
            water.waveSpeed = jw.value("waveSpeed", water.waveSpeed);
            water.choppiness = jw.value("choppiness", water.choppiness);
            water.detailStrength = jw.value("detailStrength", water.detailStrength);
            if (jw.contains("flowDirection") && jw["flowDirection"].is_array() &&
                jw["flowDirection"].size() >= 2) {
                water.flowDirection = {jw["flowDirection"][0].get<float>(),
                                       jw["flowDirection"][1].get<float>()};
            }
            water.refractionStrength = jw.value("refractionStrength", water.refractionStrength);
            water.reflectionStrength = jw.value("reflectionStrength", water.reflectionStrength);
            water.fresnelPower = jw.value("fresnelPower", water.fresnelPower);
            water.depthFade = jw.value("depthFade", water.depthFade);
            water.absorption = jw.value("absorption", water.absorption);
            water.clarity = jw.value("clarity", water.clarity);
            water.transparency = jw.value("transparency", water.transparency);
            water.foamDistance = jw.value("foamDistance", water.foamDistance);
            water.foamIntensity = jw.value("foamIntensity", water.foamIntensity);
            water.sparkle = jw.value("sparkle", water.sparkle);
            if (jw.contains("shallowColor")) water.shallowColor = Vec3FromJson(jw.at("shallowColor"));
            if (jw.contains("deepColor")) water.deepColor = Vec3FromJson(jw.at("deepColor"));
            if (jw.contains("foamColor")) water.foamColor = Vec3FromJson(jw.at("foamColor"));
        }
        if (je.contains("groomHair") && je["groomHair"].is_object()) {
            const auto& jg = je["groomHair"];
            auto& groom = e.AddComponent<GroomComponent>();
            groom.enabled = jg.value("enabled", true);
            groom.simulate = jg.value("simulate", true);
            groom.visibleInEditor = jg.value("visibleInEditor", true);
            groom.targetMeshName = jg.value("targetMeshName", std::string{});
            groom.guideCount = jg.value("guideCount", groom.guideCount);
            groom.strandsPerGuide = jg.value("strandsPerGuide", groom.strandsPerGuide);
            groom.segments = jg.value("segments", groom.segments);
            groom.length = jg.value("length", groom.length);
            groom.width = jg.value("width", groom.width);
            groom.rootWidth = jg.value("rootWidth", groom.rootWidth);
            groom.tipWidth = jg.value("tipWidth", groom.tipWidth);
            groom.clumpRadius = jg.value("clumpRadius", groom.clumpRadius);
            groom.clumping = jg.value("clumping", groom.clumping);
            groom.frizz = jg.value("frizz", groom.frizz);
            groom.curl = jg.value("curl", groom.curl);
            groom.volume = jg.value("volume", groom.volume);
            groom.scalpStart = jg.value("scalpStart", groom.scalpStart);
            groom.scalpEnd = jg.value("scalpEnd", groom.scalpEnd);
            groom.normalThreshold = jg.value("normalThreshold", groom.normalThreshold);
            if (jg.contains("groomDirection")) {
                groom.groomDirection = Vec3FromJson(jg.at("groomDirection"));
            }
            groom.gravity = jg.value("gravity", groom.gravity);
            groom.stiffness = jg.value("stiffness", groom.stiffness);
            groom.damping = jg.value("damping", groom.damping);
            groom.windStrength = jg.value("windStrength", groom.windStrength);
            if (jg.contains("windDirection")) {
                groom.windDirection = Vec3FromJson(jg.at("windDirection"));
            }
            groom.windFrequency = jg.value("windFrequency", groom.windFrequency);
            groom.specular = jg.value("specular", groom.specular);
            groom.shininess = jg.value("shininess", groom.shininess);
            groom.ambient = jg.value("ambient", groom.ambient);
            groom.opacity = jg.value("opacity", groom.opacity);
            groom.randomTint = jg.value("randomTint", groom.randomTint);
            if (jg.contains("rootColor")) groom.rootColor = Vec3FromJson(jg.at("rootColor"));
            if (jg.contains("tipColor")) groom.tipColor = Vec3FromJson(jg.at("tipColor"));
            groom.seed = jg.value("seed", groom.seed);
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
            rb.lockRotation = jr.value("lockRotation", false);
            rb.bodyId = 0xFFFFFFFFu;
        }
        if (je.contains("collider")) {
            const auto& jc = je["collider"];
            auto& collider = e.AddComponent<ColliderComponent>();
            collider.shapeType = static_cast<ColliderShapeType>(
                std::clamp(jc.value("shapeType", 0), 0, 2));
            if (jc.contains("halfExtents")) collider.halfExtents = Vec3FromJson(jc.at("halfExtents"));
            collider.radius = jc.value("radius", 0.5f);
            collider.halfHeight = jc.value("halfHeight", 0.5f);
            if (jc.contains("offset")) collider.offset = Vec3FromJson(jc.at("offset"));
            collider.friction = jc.value("friction", 0.5f);
            collider.restitution = jc.value("restitution", 0.0f);
            collider.isTrigger = jc.value("isTrigger", false);
            collider.visibleInEditor = jc.value("visibleInEditor", true);
            // Older scenes did not store a rotation lock; capsules were prone
            // to tipping over, so keep them upright by default.
            if (collider.shapeType == ColliderShapeType::Capsule &&
                e.HasComponent<RigidBodyComponent>() &&
                (!je.contains("rigidBody") ||
                 !je["rigidBody"].contains("lockRotation"))) {
                e.GetComponent<RigidBodyComponent>().lockRotation = true;
            }
        } else if (je.contains("boxCollider")) {
            // Legacy scenes stored only a box collider.
            const auto& jc = je["boxCollider"];
            auto& collider = e.AddComponent<ColliderComponent>();
            collider.shapeType = ColliderShapeType::Box;
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
