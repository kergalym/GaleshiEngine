#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Renderer/Frustum.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include "GaleshiEngine/Renderer/GroomSystem.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <limits>
#include <unordered_map>
#include <vector>

namespace Galeshi {


namespace {
    bgfx::ShaderHandle LoadBinShader(const char* path) {
        FILE* f = fopen(path, "rb");
        if (!f) return BGFX_INVALID_HANDLE;
        fseek(f, 0, SEEK_END); long sz = ftell(f); fseek(f, 0, SEEK_SET);
        const bgfx::Memory* mem = bgfx::alloc((uint32_t)sz);
        fread(mem->data, 1, (size_t)sz, f); fclose(f);
        return bgfx::createShader(mem);
    }

    uint64_t FaceState(bool twoSided) {
        uint64_t state = BGFX_STATE_FRONT_CCW;
        if (!twoSided) state |= BGFX_STATE_CULL_CW;
        return state;
    }

    entt::entity FindRootEntity(Scene& scene, entt::entity handle) {
        entt::entity root = handle;
        int depth = 0;
        while (root != entt::null && depth++ < 128) {
            if (!scene.Registry().all_of<ParentComponent>(root)) break;
            root = scene.Registry().get<ParentComponent>(root).parent;
        }
        return root;
    }

    bool HasSkeletonRoot(Scene& scene, entt::entity handle) {
        return scene.Registry().all_of<SkeletalComponent>(
            FindRootEntity(scene, handle));
    }

    glm::mat4 EntityWorldMatrix(Scene& scene, entt::entity handle) {
        glm::mat4 model(1.0f);
        entt::entity current = handle;
        std::vector<entt::entity> chain;
        int depth = 0;
        while (current != entt::null && depth++ < 128) {
            chain.push_back(current);
            if (scene.Registry().all_of<ParentComponent>(current)) {
                current = scene.Registry().get<ParentComponent>(current).parent;
            } else {
                break;
            }
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it)) {
                model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
            }
        }
        return model;
    }

    bool MatricesNearlyEqual(const glm::mat4& a, const glm::mat4& b) {
        for (int column = 0; column < 4; ++column) {
            for (int row = 0; row < 4; ++row) {
                if (std::abs(a[column][row] - b[column][row]) > 1e-6f) {
                    return false;
                }
            }
        }
        return true;
    }

    float ProjectedSphereRadius(const glm::vec3& center, float radius,
                                const glm::vec3& cameraPosition,
                                const glm::mat4& projection) {
        const float distance = std::max(glm::length(center - cameraPosition), 1e-4f);
        const float tanHalfFov =
            std::max(1.0f / std::max(std::abs(projection[1][1]), 1e-6f), 1e-4f);
        return radius / (distance * tanHalfFov);
    }

    int SelectLod(const Mesh& mesh, float projectedRadius, float bias) {
        const uint32_t lodCount = mesh.LodCount();
        if (lodCount <= 1) return 0;

        // Thresholds are in NDC half-height units. They are intentionally
        // generous: even LOD 1 removes a large number of triangles while the
        // object is still small on screen.
        static constexpr float kThresholds[] = {0.18f, 0.07f, 0.025f};
        const float safeBias = std::clamp(bias, 0.1f, 8.0f);
        int lod = 0;
        for (uint32_t level = 1; level < lodCount; ++level) {
            const std::size_t thresholdIndex =
                std::min<std::size_t>(level - 1, std::size(kThresholds) - 1);
            if (projectedRadius < kThresholds[thresholdIndex] / safeBias) {
                lod = static_cast<int>(level);
            }
        }
        return lod;
    }

    // Every field that affects the material uniforms, texture bindings or
    // render state participates in the key. Two separately authored material
    // instances with the same appearance can therefore share one batch.
    struct MeshMaterialKey {
        const Mesh* mesh = nullptr;
        uint32_t lod = 0;
        glm::vec4 albedoFactor{1.0f};
        glm::vec3 emissiveFactor{0.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 0.5f;
        float normalStrength = 1.0f;
        float occlusionStrength = 1.0f;
        float alphaCutoff = 0.5f;
        bool twoSided = false;
        std::array<const Texture*, 9> textures{};

        bool operator==(const MeshMaterialKey& other) const {
            return mesh == other.mesh &&
                   lod == other.lod &&
                   albedoFactor.x == other.albedoFactor.x &&
                   albedoFactor.y == other.albedoFactor.y &&
                   albedoFactor.z == other.albedoFactor.z &&
                   albedoFactor.w == other.albedoFactor.w &&
                   emissiveFactor.x == other.emissiveFactor.x &&
                   emissiveFactor.y == other.emissiveFactor.y &&
                   emissiveFactor.z == other.emissiveFactor.z &&
                   metallicFactor == other.metallicFactor &&
                   roughnessFactor == other.roughnessFactor &&
                   normalStrength == other.normalStrength &&
                   occlusionStrength == other.occlusionStrength &&
                   alphaCutoff == other.alphaCutoff &&
                   twoSided == other.twoSided &&
                   textures == other.textures;
        }
    };

    inline void HashCombine(std::size_t& seed, std::size_t value) {
        seed ^= value + static_cast<std::size_t>(0x9e3779b97f4a7c15ULL) +
                (seed << 6) + (seed >> 2);
    }

    inline std::size_t HashFloat(float value) {
        // +0 and -0 compare equal, so they must hash equal as well.
        return std::hash<float>{}(value == 0.0f ? 0.0f : value);
    }

    struct MeshMaterialKeyHash {
        std::size_t operator()(const MeshMaterialKey& key) const {
            std::size_t seed = std::hash<const void*>{}(key.mesh);
            HashCombine(seed, std::hash<uint32_t>{}(key.lod));
            HashCombine(seed, HashFloat(key.albedoFactor.x));
            HashCombine(seed, HashFloat(key.albedoFactor.y));
            HashCombine(seed, HashFloat(key.albedoFactor.z));
            HashCombine(seed, HashFloat(key.albedoFactor.w));
            HashCombine(seed, HashFloat(key.emissiveFactor.x));
            HashCombine(seed, HashFloat(key.emissiveFactor.y));
            HashCombine(seed, HashFloat(key.emissiveFactor.z));
            HashCombine(seed, HashFloat(key.metallicFactor));
            HashCombine(seed, HashFloat(key.roughnessFactor));
            HashCombine(seed, HashFloat(key.normalStrength));
            HashCombine(seed, HashFloat(key.occlusionStrength));
            HashCombine(seed, HashFloat(key.alphaCutoff));
            HashCombine(seed, std::hash<bool>{}(key.twoSided));
            for (const Texture* texture : key.textures) {
                HashCombine(seed, std::hash<const void*>{}(texture));
            }
            return seed;
        }
    };

    MeshMaterialKey MakeMeshMaterialKey(const MeshComponent& mc,
                                        const Material& mat, uint32_t lod) {
        MeshMaterialKey key;
        key.mesh = mc.mesh.get();
        key.lod = lod;
        key.albedoFactor = mat.albedoFactor;
        key.emissiveFactor = mat.emissiveFactor;
        key.metallicFactor = mat.metallicFactor;
        key.roughnessFactor = mat.roughnessFactor;
        key.normalStrength = mat.normalStrength;
        key.occlusionStrength = mat.occlusionStrength;
        key.alphaCutoff = mat.alphaCutoff;
        key.twoSided = mat.twoSided;
        key.textures = {
            mat.albedoMap.get(),
            mat.metallicRoughnessMap.get(),
            mat.normalMap.get(),
            mat.emissiveMap.get(),
            mat.occlusionMap.get(),
            mat.opacityMap.get(),
            mat.metallicMap.get(),
            mat.roughnessMap.get(),
            mat.specularMap.get()
        };
        return key;
    }

    struct MeshInstanceBatch {
        std::shared_ptr<Mesh> mesh;
        const Material* material = nullptr;
        std::vector<glm::mat4> instances;
    };

    struct ShadowInstanceBatch {
        std::shared_ptr<Mesh> mesh;
        std::vector<glm::mat4> singleSided;
        std::vector<glm::mat4> twoSided;
    };

    uint32_t UploadInstances(const std::vector<glm::mat4>& instances,
                             std::size_t offset,
                             bgfx::InstanceDataBuffer& idb) {
        if (offset >= instances.size()) return 0;
        const std::size_t remaining = instances.size() - offset;
        const uint32_t requested = static_cast<uint32_t>(
            std::min<std::size_t>(remaining,
                                  static_cast<std::size_t>(0xFFFFFFFFu)));
        const uint32_t available =
            bgfx::getAvailInstanceDataBuffer(requested, 64);
        if (available == 0) return 0;

        bgfx::allocInstanceDataBuffer(&idb, available, 64);
        float* data = reinterpret_cast<float*>(idb.data);
        for (uint32_t i = 0; i < available; ++i) {
            std::memcpy(
                data + static_cast<std::size_t>(i) * 16,
                glm::value_ptr(instances[offset + static_cast<std::size_t>(i)]),
                sizeof(float) * 16);
        }
        return available;
    }

    // Camera-space world matrices are baked into the instance stream, so the
    // shared u_model[0] transform is identity. Chunking keeps batches valid
    // even when the transient instance buffer cannot hold the whole group.
    void SubmitInstancedMesh(bgfx::ViewId viewId, bgfx::ProgramHandle program,
                             const Mesh& mesh, uint32_t lod,
                             const std::vector<glm::mat4>& instances,
                             bool twoSided, uint64_t state) {
        if (!bgfx::isValid(program) || !mesh.LodValid(lod) || instances.empty()) {
            return;
        }
        static const glm::mat4 identity(1.0f);
        std::size_t offset = 0;
        while (offset < instances.size()) {
            bgfx::InstanceDataBuffer idb{};
            const uint32_t count = UploadInstances(instances, offset, idb);
            if (count == 0) break;

            bgfx::setTransform(glm::value_ptr(identity));
            bgfx::setInstanceDataBuffer(&idb, 0, count);
            bgfx::setVertexBuffer(0, mesh.LodVB(lod));
            bgfx::setIndexBuffer(mesh.LodIB(lod));
            bgfx::setState(state | FaceState(twoSided));
            bgfx::submit(viewId, program);
            offset += count;
        }
    }
}

RenderSystem::RenderSystem() = default;
RenderSystem::~RenderSystem() { Shutdown(); }

void RenderSystem::Init(AssetManager* assets) {
    m_assets = assets;
    m_program = ShaderManager::LoadProgram("shaders/vs_pbr.bin", "shaders/fs_pbr.bin");
    m_gbufferProg = ShaderManager::LoadProgram("shaders/vs_pbr.bin", "shaders/fs_gbuffer.bin");
    m_shadowProg = ShaderManager::LoadProgram("shaders/vs_shadow.bin", "shaders/fs_shadow.bin");
    m_shadowPointProg = ShaderManager::LoadProgram("shaders/vs_pbr.bin", "shaders/fs_shadow_point.bin");
    m_glassProg = ShaderManager::LoadProgram("shaders/vs_pbr.bin", "shaders/fs_glass.bin");
    m_waterProg = ShaderManager::LoadProgram("shaders/vs_water.bin", "shaders/fs_water.bin");
    m_hairProg = ShaderManager::LoadProgram("shaders/vs_hair.bin", "shaders/fs_hair.bin");
    m_instancedProg = ShaderManager::LoadProgram(
        "shaders/vs_mesh_instanced.bin", "shaders/fs_pbr.bin");
    m_instancedGbufferProg = ShaderManager::LoadProgram(
        "shaders/vs_mesh_instanced.bin", "shaders/fs_gbuffer.bin");
    m_instancedShadowProg = ShaderManager::LoadProgram(
        "shaders/vs_mesh_shadow_instanced.bin", "shaders/fs_shadow.bin");
    m_instancedShadowPointProg = ShaderManager::LoadProgram(
        "shaders/vs_mesh_instanced.bin", "shaders/fs_shadow_point.bin");
    m_occlusionSupported =
        (bgfx::getCaps()->supported & BGFX_CAPS_OCCLUSION_QUERY) != 0;
    m_occlusionEnabled = m_occlusionSupported;
    spdlog::info("[RenderSystem] programs: pbr={} gbuffer={} shadow={} shadowPoint={} glass={} water={} hair={} vegetation={} occlusion={}",
        bgfx::isValid(m_program), bgfx::isValid(m_gbufferProg),
        bgfx::isValid(m_shadowProg), bgfx::isValid(m_shadowPointProg),
        bgfx::isValid(m_glassProg), bgfx::isValid(m_waterProg),
        bgfx::isValid(m_hairProg), bgfx::isValid(m_instancedProg),
        m_occlusionSupported);
    u_shadowLightPos = bgfx::createUniform("u_shadowLightPos", bgfx::UniformType::Vec4);
    u_prevViewProj = bgfx::createUniform("u_prevViewProj", bgfx::UniformType::Mat4);
    u_prevModel = bgfx::createUniform("u_prevModel", bgfx::UniformType::Mat4);
    if (!bgfx::isValid(m_program) && !bgfx::isValid(m_gbufferProg)) return;

    u_albedoFactor = bgfx::createUniform("u_albedoFactor", bgfx::UniformType::Vec4);
    u_metallicFactor = bgfx::createUniform("u_metallicFactor", bgfx::UniformType::Vec4);
    u_roughnessFactor = bgfx::createUniform("u_roughnessFactor", bgfx::UniformType::Vec4);
    u_emissiveFactor = bgfx::createUniform("u_emissiveFactor", bgfx::UniformType::Vec4);
    u_cameraPosition = bgfx::createUniform("u_cameraPosition", bgfx::UniformType::Vec4);
    u_lightDirection = bgfx::createUniform("u_lightDirection", bgfx::UniformType::Vec4);
    u_lightColorIntensity = bgfx::createUniform("u_lightColorIntensity", bgfx::UniformType::Vec4);
    u_pointLightPosRadius = bgfx::createUniform("u_pointLightPosRadius", bgfx::UniformType::Vec4, kMaxPointLights);
    u_pointLightColorInt = bgfx::createUniform("u_pointLightColorInt", bgfx::UniformType::Vec4, kMaxPointLights);
    u_pointLightCount = bgfx::createUniform("u_pointLightCount", bgfx::UniformType::Vec4);
    u_materialParams = bgfx::createUniform("u_materialParams", bgfx::UniformType::Vec4);
    u_materialFlags = bgfx::createUniform("u_materialFlags", bgfx::UniformType::Vec4);
    u_glassParams = bgfx::createUniform("u_glassParams", bgfx::UniformType::Vec4);
    u_cameraForward = bgfx::createUniform("u_cameraForward", bgfx::UniformType::Vec4);
    u_waterParams0 = bgfx::createUniform("u_waterParams0", bgfx::UniformType::Vec4);
    u_waterParams1 = bgfx::createUniform("u_waterParams1", bgfx::UniformType::Vec4);
    u_waterParams2 = bgfx::createUniform("u_waterParams2", bgfx::UniformType::Vec4);
    u_waterParams3 = bgfx::createUniform("u_waterParams3", bgfx::UniformType::Vec4);
    u_waterFlow = bgfx::createUniform("u_waterFlow", bgfx::UniformType::Vec4);
    u_waterInvNearFar = bgfx::createUniform("u_waterInvNearFar", bgfx::UniformType::Vec4);
    u_waterShallow = bgfx::createUniform("u_waterShallow", bgfx::UniformType::Vec4);
    u_waterDeep = bgfx::createUniform("u_waterDeep", bgfx::UniformType::Vec4);
    u_waterFoam = bgfx::createUniform("u_waterFoam", bgfx::UniformType::Vec4);
    s_waterSceneColor = bgfx::createUniform("s_waterSceneColor", bgfx::UniformType::Sampler);
    s_waterDepth = bgfx::createUniform("s_waterDepth", bgfx::UniformType::Sampler);
    s_waterEnvCube = bgfx::createUniform("s_envCube", bgfx::UniformType::Sampler);
    s_waterPlanar = bgfx::createUniform("s_waterPlanar", bgfx::UniformType::Sampler);
    u_waterPlanar = bgfx::createUniform("u_waterPlanar", bgfx::UniformType::Vec4);
    u_reflectionClip = bgfx::createUniform("u_reflectionClip", bgfx::UniformType::Vec4);
    u_hairParams0 = bgfx::createUniform("u_hairParams0", bgfx::UniformType::Vec4);
    u_hairParams1 = bgfx::createUniform("u_hairParams1", bgfx::UniformType::Vec4);
    u_hairInvNearFar = bgfx::createUniform("u_hairInvNearFar", bgfx::UniformType::Vec4);
    u_hairRootColor = bgfx::createUniform("u_hairRootColor", bgfx::UniformType::Vec4);
    u_hairTipColor = bgfx::createUniform("u_hairTipColor", bgfx::UniformType::Vec4);
    s_hairDepth = bgfx::createUniform("s_hairDepth", bgfx::UniformType::Sampler);
    s_hairEnvCube = bgfx::createUniform("s_hairEnvCube", bgfx::UniformType::Sampler);
    
    s_boneMatrices = bgfx::createUniform("s_boneMatrices", bgfx::UniformType::Sampler);
    m_boneTexture = bgfx::createTexture2D(4, kMaxBones, false, 1, bgfx::TextureFormat::RGBA32F);
    m_identityBoneTexture = bgfx::createTexture2D(
        4, kMaxBones, false, 1, bgfx::TextureFormat::RGBA32F);
    if (bgfx::isValid(m_identityBoneTexture)) {
        std::vector<glm::vec4> identity(kMaxBones * 4, glm::vec4(0.0f));
        const glm::mat4 id(1.0f);
        for (int i = 0; i < kMaxBones; ++i) {
            for (int j = 0; j < 4; ++j) identity[i * 4 + j] = id[j];
        }
        bgfx::updateTexture2D(m_identityBoneTexture, 0, 0, 0, 0,
                              4, kMaxBones,
                              bgfx::copy(identity.data(),
                                         kMaxBones * 4 * sizeof(glm::vec4)));
    }

    s_albedoMap = bgfx::createUniform("s_albedoMap", bgfx::UniformType::Sampler);
    s_metallicRoughnessMap = bgfx::createUniform("s_metallicRoughnessMap", bgfx::UniformType::Sampler);
    s_normalMap = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);
    s_irradianceMap = bgfx::createUniform("s_irradianceMap", bgfx::UniformType::Sampler);
    s_envMap = bgfx::createUniform("s_envMap", bgfx::UniformType::Sampler);
    s_emissiveMap = bgfx::createUniform("s_emissiveMap", bgfx::UniformType::Sampler);
    s_occlusionMap = bgfx::createUniform("s_occlusionMap", bgfx::UniformType::Sampler);
    s_opacityMap = bgfx::createUniform("s_opacityMap", bgfx::UniformType::Sampler);
    s_metallicMap = bgfx::createUniform("s_metallicMap", bgfx::UniformType::Sampler);
    s_roughnessMap = bgfx::createUniform("s_roughnessMap", bgfx::UniformType::Sampler);
    s_specularMap = bgfx::createUniform("s_specularMap", bgfx::UniformType::Sampler);
    s_brdfLUT = bgfx::createUniform("s_brdfLUT", bgfx::UniformType::Sampler);
}

void RenderSystem::Shutdown() {
    ResetOcclusionState();
    if (bgfx::isValid(m_occlusionFbo)) bgfx::destroy(m_occlusionFbo);
    if (bgfx::isValid(m_occlusionColor)) bgfx::destroy(m_occlusionColor);
    if (bgfx::isValid(m_occlusionDepth)) bgfx::destroy(m_occlusionDepth);
    m_occlusionFbo = BGFX_INVALID_HANDLE;
    m_occlusionColor = BGFX_INVALID_HANDLE;
    m_occlusionDepth = BGFX_INVALID_HANDLE;
    m_occlusionWidth = 0;
    m_occlusionHeight = 0;

    if (bgfx::isValid(m_program)) bgfx::destroy(m_program);
    if (bgfx::isValid(m_gbufferProg)) bgfx::destroy(m_gbufferProg);
    if (bgfx::isValid(m_shadowProg)) bgfx::destroy(m_shadowProg);
    if (bgfx::isValid(m_shadowPointProg)) bgfx::destroy(m_shadowPointProg);
    if (bgfx::isValid(m_glassProg)) bgfx::destroy(m_glassProg);
    if (bgfx::isValid(m_waterProg)) bgfx::destroy(m_waterProg);
    if (bgfx::isValid(m_hairProg)) bgfx::destroy(m_hairProg);
    if (bgfx::isValid(m_instancedProg)) bgfx::destroy(m_instancedProg);
    if (bgfx::isValid(m_instancedGbufferProg)) bgfx::destroy(m_instancedGbufferProg);
    if (bgfx::isValid(m_instancedShadowProg)) bgfx::destroy(m_instancedShadowProg);
    if (bgfx::isValid(m_instancedShadowPointProg)) bgfx::destroy(m_instancedShadowPointProg);
    if (bgfx::isValid(u_shadowLightPos)) bgfx::destroy(u_shadowLightPos);
    if (bgfx::isValid(u_prevViewProj)) bgfx::destroy(u_prevViewProj);
    if (bgfx::isValid(u_prevModel)) bgfx::destroy(u_prevModel);
    if (bgfx::isValid(u_albedoFactor)) bgfx::destroy(u_albedoFactor);
    if (bgfx::isValid(u_metallicFactor)) bgfx::destroy(u_metallicFactor);
    if (bgfx::isValid(u_roughnessFactor)) bgfx::destroy(u_roughnessFactor);
    if (bgfx::isValid(u_emissiveFactor)) bgfx::destroy(u_emissiveFactor);
    if (bgfx::isValid(u_cameraPosition)) bgfx::destroy(u_cameraPosition);
    if (bgfx::isValid(u_lightDirection)) bgfx::destroy(u_lightDirection);
    if (bgfx::isValid(u_lightColorIntensity)) bgfx::destroy(u_lightColorIntensity);
    if (bgfx::isValid(u_pointLightPosRadius)) bgfx::destroy(u_pointLightPosRadius);
    if (bgfx::isValid(u_pointLightColorInt)) bgfx::destroy(u_pointLightColorInt);
    if (bgfx::isValid(u_pointLightCount)) bgfx::destroy(u_pointLightCount);
    if (bgfx::isValid(u_materialParams)) bgfx::destroy(u_materialParams);
    if (bgfx::isValid(u_materialFlags)) bgfx::destroy(u_materialFlags);
    if (bgfx::isValid(u_glassParams)) bgfx::destroy(u_glassParams);
    if (bgfx::isValid(u_cameraForward)) bgfx::destroy(u_cameraForward);
    if (bgfx::isValid(u_waterParams0)) bgfx::destroy(u_waterParams0);
    if (bgfx::isValid(u_waterParams1)) bgfx::destroy(u_waterParams1);
    if (bgfx::isValid(u_waterParams2)) bgfx::destroy(u_waterParams2);
    if (bgfx::isValid(u_waterParams3)) bgfx::destroy(u_waterParams3);
    if (bgfx::isValid(u_waterFlow)) bgfx::destroy(u_waterFlow);
    if (bgfx::isValid(u_waterInvNearFar)) bgfx::destroy(u_waterInvNearFar);
    if (bgfx::isValid(u_waterShallow)) bgfx::destroy(u_waterShallow);
    if (bgfx::isValid(u_waterDeep)) bgfx::destroy(u_waterDeep);
    if (bgfx::isValid(u_waterFoam)) bgfx::destroy(u_waterFoam);
    if (bgfx::isValid(s_waterSceneColor)) bgfx::destroy(s_waterSceneColor);
    if (bgfx::isValid(s_waterDepth)) bgfx::destroy(s_waterDepth);
    if (bgfx::isValid(s_waterEnvCube)) bgfx::destroy(s_waterEnvCube);
    if (bgfx::isValid(s_waterPlanar)) bgfx::destroy(s_waterPlanar);
    if (bgfx::isValid(u_waterPlanar)) bgfx::destroy(u_waterPlanar);
    if (bgfx::isValid(u_reflectionClip)) bgfx::destroy(u_reflectionClip);
    DestroyReflectionTarget();
    if (bgfx::isValid(u_hairParams0)) bgfx::destroy(u_hairParams0);
    if (bgfx::isValid(u_hairParams1)) bgfx::destroy(u_hairParams1);
    if (bgfx::isValid(u_hairInvNearFar)) bgfx::destroy(u_hairInvNearFar);
    if (bgfx::isValid(u_hairRootColor)) bgfx::destroy(u_hairRootColor);
    if (bgfx::isValid(u_hairTipColor)) bgfx::destroy(u_hairTipColor);
    if (bgfx::isValid(s_hairDepth)) bgfx::destroy(s_hairDepth);
    if (bgfx::isValid(s_hairEnvCube)) bgfx::destroy(s_hairEnvCube);
    if (bgfx::isValid(s_boneMatrices)) bgfx::destroy(s_boneMatrices);
    if (bgfx::isValid(m_boneTexture)) bgfx::destroy(m_boneTexture);
    if (bgfx::isValid(m_identityBoneTexture)) bgfx::destroy(m_identityBoneTexture);
    m_boneTexture = BGFX_INVALID_HANDLE;
    m_identityBoneTexture = BGFX_INVALID_HANDLE;
    if (bgfx::isValid(s_albedoMap)) bgfx::destroy(s_albedoMap);
    if (bgfx::isValid(s_metallicRoughnessMap)) bgfx::destroy(s_metallicRoughnessMap);
    if (bgfx::isValid(s_normalMap)) bgfx::destroy(s_normalMap);
    if (bgfx::isValid(s_irradianceMap)) bgfx::destroy(s_irradianceMap);
    if (bgfx::isValid(s_envMap)) bgfx::destroy(s_envMap);
    if (bgfx::isValid(s_emissiveMap)) bgfx::destroy(s_emissiveMap);
    if (bgfx::isValid(s_occlusionMap)) bgfx::destroy(s_occlusionMap);
    if (bgfx::isValid(s_opacityMap)) bgfx::destroy(s_opacityMap);
    if (bgfx::isValid(s_metallicMap)) bgfx::destroy(s_metallicMap);
    if (bgfx::isValid(s_roughnessMap)) bgfx::destroy(s_roughnessMap);
    if (bgfx::isValid(s_specularMap)) bgfx::destroy(s_specularMap);
    if (bgfx::isValid(s_brdfLUT)) bgfx::destroy(s_brdfLUT);
    m_program = BGFX_INVALID_HANDLE;
}

void RenderSystem::BeginBonePaletteCache() {
    ++m_boneCacheGeneration;
    m_boneCacheRoot = 0xFFFFFFFFu;
    m_boneCacheRootGeneration = 0;
    m_boneCacheUsesTexture = false;
}

void RenderSystem::BindBonePalette(Scene& scene, uint32_t rootId,
                                   bool hasSkeleton) {
    // Non-skinned and non-animated meshes share one immutable identity
    // texture. Previously every draw uploaded 256 identity matrices, which
    // dominated CPU submission on scenes made of many character submeshes.
    if (!hasSkeleton || rootId == 0xFFFFFFFFu) {
        if (bgfx::isValid(m_identityBoneTexture)) {
            bgfx::setTexture(3, s_boneMatrices, m_identityBoneTexture);
        } else {
            bgfx::setTexture(3, s_boneMatrices, m_boneTexture);
        }
        return;
    }

    if (m_boneCacheRoot == rootId &&
        m_boneCacheRootGeneration == m_boneCacheGeneration) {
        bgfx::setTexture(3, s_boneMatrices,
                         m_boneCacheUsesTexture ? m_boneTexture
                                                : m_identityBoneTexture);
        return;
    }

    auto& registry = scene.Registry();
    const entt::entity root = static_cast<entt::entity>(rootId);
    auto& skeleton = registry.get<SkeletalComponent>(root);
    const bool animated = skeleton.isAnimated && !skeleton.sourcePath.empty();
    if (animated && bgfx::isValid(m_boneTexture)) {
        if (skeleton.boneMatrices.size() != kMaxBones) {
            skeleton.boneMatrices.resize(kMaxBones, glm::mat4(1.0f));
        }
        m_boneScratch.resize(kMaxBones * 4);
        for (int i = 0; i < kMaxBones; ++i) {
            for (int j = 0; j < 4; ++j) {
                m_boneScratch[i * 4 + j] = skeleton.boneMatrices[i][j];
            }
        }
        bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones,
                              bgfx::copy(m_boneScratch.data(),
                                         kMaxBones * 4 * sizeof(glm::vec4)));
    }

    m_boneCacheRoot = rootId;
    m_boneCacheRootGeneration = m_boneCacheGeneration;
    m_boneCacheUsesTexture = animated && bgfx::isValid(m_boneTexture);
    bgfx::setTexture(3, s_boneMatrices,
                     m_boneCacheUsesTexture ? m_boneTexture
                                            : m_identityBoneTexture);
}

void RenderSystem::SetOcclusionCullingEnabled(bool enabled) {
    if (enabled && !m_occlusionSupported) enabled = false;
    if (m_occlusionEnabled == enabled) return;
    m_occlusionEnabled = enabled;
    ResetOcclusionState();
}

void RenderSystem::SetLodBias(float bias) {
    m_lodBias = std::clamp(bias, 0.1f, 8.0f);
}

void RenderSystem::ResetOcclusionState() {
    for (auto& entry : m_occlusionStates) {
        if (bgfx::isValid(entry.second.query)) {
            bgfx::destroy(entry.second.query);
        }
    }
    m_occlusionStates.clear();
    m_occludedObjectCount = 0;
    m_hasLastOcclusionCamera = false;
}

bool RenderSystem::IsOccluded(uint32_t entityId) const {
    if (!m_occlusionEnabled) return false;
    const auto found = m_occlusionStates.find(entityId);
    return found != m_occlusionStates.end() && !found->second.visible;
}

bool RenderSystem::EnsureOcclusionTarget(uint16_t width, uint16_t height) {
    if (!m_occlusionSupported || width == 0 || height == 0) return false;

    // Keep the occluder pass cheap while preserving aspect ratio. The query
    // result is only used for a conservative visible/invisible decision.
    const float scale = std::min(0.25f, 640.0f / static_cast<float>(width));
    const uint16_t targetWidth = static_cast<uint16_t>(
        std::max(160, static_cast<int>(static_cast<float>(width) * scale)));
    const uint16_t targetHeight = static_cast<uint16_t>(
        std::max(90, static_cast<int>(static_cast<float>(height) * scale)));

    if (bgfx::isValid(m_occlusionFbo) &&
        m_occlusionWidth == targetWidth &&
        m_occlusionHeight == targetHeight) {
        return true;
    }

    if (bgfx::isValid(m_occlusionFbo)) bgfx::destroy(m_occlusionFbo);
    if (bgfx::isValid(m_occlusionColor)) bgfx::destroy(m_occlusionColor);
    if (bgfx::isValid(m_occlusionDepth)) bgfx::destroy(m_occlusionDepth);
    m_occlusionFbo = BGFX_INVALID_HANDLE;
    m_occlusionColor = BGFX_INVALID_HANDLE;
    m_occlusionDepth = BGFX_INVALID_HANDLE;

    const uint64_t flags = BGFX_TEXTURE_RT |
                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_occlusionColor = bgfx::createTexture2D(
        targetWidth, targetHeight, false, 1, bgfx::TextureFormat::RGBA8, flags);
    m_occlusionDepth = bgfx::createTexture2D(
        targetWidth, targetHeight, false, 1, bgfx::TextureFormat::D32F, flags);
    if (!bgfx::isValid(m_occlusionColor) || !bgfx::isValid(m_occlusionDepth)) {
        if (bgfx::isValid(m_occlusionColor)) bgfx::destroy(m_occlusionColor);
        if (bgfx::isValid(m_occlusionDepth)) bgfx::destroy(m_occlusionDepth);
        m_occlusionColor = BGFX_INVALID_HANDLE;
        m_occlusionDepth = BGFX_INVALID_HANDLE;
        return false;
    }

    const bgfx::TextureHandle attachments[] = {m_occlusionColor, m_occlusionDepth};
    m_occlusionFbo = bgfx::createFrameBuffer(2, attachments, false);
    if (!bgfx::isValid(m_occlusionFbo)) {
        bgfx::destroy(m_occlusionColor);
        bgfx::destroy(m_occlusionDepth);
        m_occlusionColor = BGFX_INVALID_HANDLE;
        m_occlusionDepth = BGFX_INVALID_HANDLE;
        return false;
    }

    m_occlusionWidth = targetWidth;
    m_occlusionHeight = targetHeight;
    return true;
}

void RenderSystem::RenderOcclusion(Scene& scene, const Camera& camera,
                                   bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    m_occludedObjectCount = 0;
    if (!m_occlusionEnabled || !m_occlusionSupported || !m_assets ||
        !bgfx::isValid(m_shadowProg)) {
        return;
    }
    // Occlusion queries have a fixed CPU cost per candidate and are not a
    // win for small scenes. Below this threshold, frustum culling already
    // removes the few objects that can matter and the query pass only adds
    // submission overhead.
    static constexpr uint32_t kMinOcclusionMeshes = 64;
    uint32_t meshCount = 0;
    auto meshView = scene.Registry().view<TransformComponent, MeshComponent>();
    for (auto h : meshView) {
        const auto& mesh = meshView.get<MeshComponent>(h);
        if (mesh.mesh && mesh.mesh->Valid()) ++meshCount;
    }
    if (meshCount < kMinOcclusionMeshes) {
        if (!m_occlusionStates.empty()) ResetOcclusionState();
        m_occludedObjectCount = 0;
        return;
    }

    if (!EnsureOcclusionTarget(width, height)) {
        ResetOcclusionState();
        return;
    }

    ++m_occlusionFrame;

    // Query results are available one frame later. If an object was not
    // tested in the previous frame, force it visible rather than inheriting a
    // stale result from before it entered the view.
    for (auto& entry : m_occlusionStates) {
        OcclusionState& state = entry.second;
        if (state.lastTestedFrame + 1 == m_occlusionFrame &&
            bgfx::isValid(state.query)) {
            const bgfx::OcclusionQueryResult::Enum result =
                bgfx::getResult(state.query);
            if (result == bgfx::OcclusionQueryResult::Visible) {
                state.visible = true;
            } else if (result == bgfx::OcclusionQueryResult::Invisible) {
                state.visible = false;
            }
        } else {
            state.visible = true;
        }
    }

    const glm::vec3 cameraPosition = camera.Position();
    if (m_hasLastOcclusionCamera &&
        glm::distance(cameraPosition, m_lastOcclusionCameraPos) > 5.0f) {
        for (auto& entry : m_occlusionStates) {
            entry.second.visible = true;
        }
    }
    m_lastOcclusionCameraPos = cameraPosition;
    m_hasLastOcclusionCamera = true;

    bgfx::setViewFrameBuffer(viewId, m_occlusionFbo);
    bgfx::setViewRect(viewId, 0, 0, m_occlusionWidth, m_occlusionHeight);
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                           glm::value_ptr(camera.Projection()));
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    const Frustum frustum(camera.Projection() * camera.View());
    const float tanHalfFov =
        std::max(1.0f / std::max(std::abs(camera.Projection()[1][1]), 1e-6f), 1e-4f);

    struct OccluderItem {
        std::shared_ptr<Mesh> mesh;
        glm::mat4 model{1.0f};
        bool twoSided = false;
    };
    struct QueryItem {
        uint32_t entityId = 0;
        glm::mat4 proxy{1.0f};
    };

    std::vector<OccluderItem> occluders;
    std::vector<QueryItem> queries;
    occluders.reserve(32);
    queries.reserve(64);

    auto view = scene.Registry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto handle : view) {
        const auto& mc = view.get<MeshComponent>(handle);
        const auto& matc = view.get<MaterialComponent>(handle);
        if (!mc.mesh || !mc.mesh->Valid() || !matc.material ||
            matc.material->isGlass || HasSkeletonRoot(scene, handle) ||
            scene.Registry().all_of<SoftBodyComponent>(handle)) {
            continue;
        }

        const glm::mat4 model = EntityWorldMatrix(scene, handle);
        glm::vec3 localMin, localMax;
        mc.mesh->LocalAABB(localMin, localMax);
        const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
        const glm::vec3 localExtents = glm::max((localMax - localMin) * 0.5f,
                                                glm::vec3(1e-4f));
        const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
        const float maxScale = std::max(
            std::max(glm::length(glm::vec3(model[0])),
                     glm::length(glm::vec3(model[1]))),
            glm::length(glm::vec3(model[2])));
        const float worldRadius = glm::length(localMax - localMin) * 0.5f * maxScale;

        const uint32_t entityId = static_cast<uint32_t>(entt::to_integral(handle));
        OcclusionState& state = m_occlusionStates[entityId];
        state.lastSeenFrame = m_occlusionFrame;

        if (!frustum.TestSphere(worldCenter, worldRadius)) {
            state.visible = true;
            continue;
        }

        const float screenRadius = worldRadius /
            std::max(glm::length(worldCenter - cameraPosition) * tanHalfFov, 1e-4f);

        // Large opaque meshes form the occluder depth map. Alpha-cutout
        // materials are excluded because fs_shadow does not sample alpha.
        if (screenRadius >= 0.12f && !matc.material->opacityMap) {
            OccluderItem item;
            item.mesh = mc.mesh;
            item.model = model;
            item.twoSided = matc.material->twoSided;
            occluders.push_back(std::move(item));
            state.visible = true;
            continue;
        }

        // Small objects are not worth a query; they are drawn normally and
        // never inherit an old invisible result.
        if (screenRadius < 0.035f) {
            state.visible = true;
            continue;
        }

        if (!bgfx::isValid(state.query)) {
            state.query = bgfx::createOcclusionQuery();
        }
        if (!bgfx::isValid(state.query)) {
            state.visible = true;
            continue;
        }

        QueryItem item;
        item.entityId = entityId;
        item.proxy = model *
            glm::translate(glm::mat4(1.0f), localCenter) *
            glm::scale(glm::mat4(1.0f), localExtents * 2.0f);
        queries.push_back(item);
        state.lastTestedFrame = m_occlusionFrame;
    }

    for (const OccluderItem& item : occluders) {
        bgfx::setTransform(glm::value_ptr(item.model));
        bgfx::setVertexBuffer(0, item.mesh->LodVB(0));
        bgfx::setIndexBuffer(item.mesh->LodIB(0));
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       FaceState(item.twoSided));
        bgfx::submit(viewId, m_shadowProg);
    }

    const std::shared_ptr<Mesh> proxyMesh = m_assets->CreateCubeMesh();
    if (proxyMesh && proxyMesh->Valid()) {
        for (const QueryItem& item : queries) {
            const auto found = m_occlusionStates.find(item.entityId);
            if (found == m_occlusionStates.end() ||
                !bgfx::isValid(found->second.query)) {
                continue;
            }
            bgfx::setTransform(glm::value_ptr(item.proxy));
            bgfx::setVertexBuffer(0, proxyMesh->LodVB(0));
            bgfx::setIndexBuffer(proxyMesh->LodIB(0));
            // Only the depth test is needed; neither color nor depth is
            // written, so proxies cannot occlude one another.
            bgfx::setState(BGFX_STATE_DEPTH_TEST_LEQUAL | FaceState(true));
            bgfx::submit(viewId, m_shadowProg, found->second.query);
        }
    }

    for (auto it = m_occlusionStates.begin(); it != m_occlusionStates.end();) {
        OcclusionState& state = it->second;
        if (state.lastSeenFrame + 120 < m_occlusionFrame) {
            if (bgfx::isValid(state.query)) bgfx::destroy(state.query);
            it = m_occlusionStates.erase(it);
        } else {
            if (!state.visible) ++m_occludedObjectCount;
            ++it;
        }
    }
}

void RenderSystem::Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_program) && !bgfx::isValid(m_gbufferProg)) return;
    if (!m_assets) return;

    // Forward PBR is also used by the planar-reflection pass. The default
    // plane constant keeps all fragments during the normal scene pass; only
    // the reflection pass enables the mirrored half-space clip.
    const float noClip[4] = {0.0f, 0.0f, 0.0f, 1.0f};
    bgfx::setUniform(u_reflectionClip, noClip);

    Galeshi::Frustum frustum(camera.Projection() * camera.View());

    const glm::mat4 currentViewProj = camera.Projection() * camera.View();
    const glm::mat4 previousViewProj = m_hasPrevViewProj
        ? m_prevViewProj : currentViewProj;
    bgfx::setUniform(u_prevViewProj, glm::value_ptr(previousViewProj));

    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()), glm::value_ptr(camera.Projection()));
    bgfx::setViewRect(viewId, 0, 0, width, height);
    // In deferred mode the G-buffer world position must be exactly zero for
    // sky pixels; fs_lighting uses worldPos == 0 as the sky sentinel.
    const uint32_t clearColor = (m_deferred && bgfx::isValid(m_gbufferProg)) ? 0x00000000u : 0x232629ffu;
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clearColor, 1.0f, 0);

    glm::vec3 lightDir{-0.4f, -0.8f, -0.5f}, lightCol{1.0f}, p = camera.Position();
    float lightInt = 1.2f;
    auto lview = scene.Registry().view<DirectionalLightComponent>();
    if (!lview.empty()) {
        auto h = *lview.begin();
        lightDir = lview.get<DirectionalLightComponent>(h).direction;
        lightCol = lview.get<DirectionalLightComponent>(h).color;
        lightInt = lview.get<DirectionalLightComponent>(h).intensity;
    }

    glm::vec4 plPos[kMaxPointLights]{}, plCol[kMaxPointLights]{};
    int plCount = 0;
    auto plview = scene.Registry().view<TransformComponent, PointLightComponent>();
    for (auto h : plview) {
        if (plCount >= kMaxPointLights) break;
        const auto& tc = plview.get<TransformComponent>(h); const auto& pl = plview.get<PointLightComponent>(h);
        plPos[plCount] = { tc.position.x, tc.position.y, tc.position.z, pl.radius };
        plCol[plCount] = { pl.color.r, pl.color.g, pl.color.b, pl.intensity };
        ++plCount;
    }

    const float camPos[4] = {p.x, p.y, p.z, 1.0f}, lDir[4] = {lightDir.x, lightDir.y, lightDir.z, 0.0f};
    const float lColInt[4] = {lightCol.r, lightCol.g, lightCol.b, lightInt}, plCountArr[4] = {float(plCount), 0, 0, 0};
    bgfx::setUniform(u_cameraPosition, camPos); bgfx::setUniform(u_lightDirection, lDir);
    bgfx::setUniform(u_lightColorIntensity, lColInt); bgfx::setUniform(u_pointLightCount, plCountArr);
    if (plCount > 0) {
        bgfx::setUniform(u_pointLightPosRadius, plPos, plCount);
        bgfx::setUniform(u_pointLightColorInt, plCol, plCount);
    }

    const bool canInstanceStatic =
        bgfx::isValid(m_instancedProg) &&
        (!m_deferred || bgfx::isValid(m_instancedGbufferProg));
    std::unordered_map<MeshMaterialKey, MeshInstanceBatch, MeshMaterialKeyHash>
        instanceBatches;
    if (canInstanceStatic) instanceBatches.reserve(64);

    BeginBonePaletteCache();
    auto view = scene.Registry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto h : view) {
        const auto& mc = view.get<MeshComponent>(h);
        const auto& matc = view.get<MaterialComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid() || !matc.material) continue;
        // Transparent glass is submitted later in RenderGlass, after the
        // post-processed image has been copied into the color attachment.
        if (matc.material->isGlass) continue;

        const glm::mat4 model = EntityWorldMatrix(scene, h);
        const uint32_t entityId = static_cast<uint32_t>(entt::to_integral(h));
        const auto previousIt = m_prevModels.find(entityId);
        const glm::mat4 previousModel = previousIt != m_prevModels.end()
            ? previousIt->second : model;
        const bool modelMoved = !MatricesNearlyEqual(previousModel, model);

        // Frustum culling (before any bindings).
        glm::vec3 localMin, localMax;
        mc.mesh->LocalAABB(localMin, localMax);
        const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
        const float localRadius = glm::length(localMax - localMin) * 0.5f;
        const glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
        const float maxScale = std::max(
            std::max(glm::length(glm::vec3(model[0])),
                     glm::length(glm::vec3(model[1]))),
            glm::length(glm::vec3(model[2])));
        const float worldRadius = localRadius * maxScale;
        if (!frustum.TestSphere(worldCenter, worldRadius)) {
            continue; // Skip culled object BEFORE any texture/uniform binding
        }

        const entt::entity root = FindRootEntity(scene, h);
        const bool hasSkeleton =
            scene.Registry().all_of<SkeletalComponent>(root);

        // The occlusion pass tests static entities before this pass. Results
        // are one frame old, which keeps the query readback off the GPU
        // critical path.
        if (!hasSkeleton && IsOccluded(entityId)) {
            continue;
        }

        uint32_t lod = 0;
        if (m_lodEnabled && !hasSkeleton && !modelMoved &&
            !scene.Registry().all_of<SoftBodyComponent>(h)) {
            mc.mesh->EnsureLods();
            const float projectedRadius = ProjectedSphereRadius(
                worldCenter, worldRadius, camera.Position(), camera.Projection());
            lod = static_cast<uint32_t>(SelectLod(*mc.mesh, projectedRadius, m_lodBias));
        }

        // Static meshes sharing geometry, LOD and material state are
        // collected and submitted as one instanced draw after the per-object
        // pass.
        if (!hasSkeleton && canInstanceStatic && !modelMoved) {
            const MeshMaterialKey key = MakeMeshMaterialKey(mc, *matc.material, lod);
            const auto result = instanceBatches.try_emplace(key);
            MeshInstanceBatch& batch = result.first->second;
            if (result.second) {
                batch.mesh = mc.mesh;
                batch.material = matc.material.get();
            }
            batch.instances.push_back(model);
            continue;
        }

        BindBonePalette(scene,
                        static_cast<uint32_t>(entt::to_integral(root)),
                        hasSkeleton);

        bgfx::setUniform(u_prevModel, glm::value_ptr(previousModel));
        bgfx::setTransform(glm::value_ptr(model));
        BindMaterial(*matc.material);

        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        // Imported meshes are CCW-front. Single-sided materials cull their
        // backfaces; two-sided materials keep them and the fragment shaders
        // flip the normals for those fragments.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                       FaceState(matc.material->twoSided));
        if (m_deferred && bgfx::isValid(m_gbufferProg)) {
            bgfx::submit(viewId, m_gbufferProg);
        } else {
            bgfx::submit(viewId, m_program);
        }
    }

    if (canInstanceStatic && !instanceBatches.empty()) {
        const bgfx::ProgramHandle program = m_deferred && bgfx::isValid(m_instancedGbufferProg)
            ? m_instancedGbufferProg : m_instancedProg;
        const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                               BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                               BGFX_STATE_MSAA;
        for (const auto& entry : instanceBatches) {
            const MeshInstanceBatch& batch = entry.second;
            if (!batch.mesh || !batch.material) continue;
            BindMaterial(*batch.material);
            SubmitInstancedMesh(viewId, program, *batch.mesh, entry.first.lod,
                                batch.instances, entry.first.twoSided, state);
        }
    }

    RenderVegetation(scene, frustum, viewId);

    // Store the current transforms for the next frame's object motion vectors.
    for (auto handle : scene.Registry().view<TransformComponent, MeshComponent>()) {
        const uint32_t id = static_cast<uint32_t>(entt::to_integral(handle));
        m_prevModels[id] = EntityWorldMatrix(scene, handle);
    }
    m_prevViewProj = currentViewProj;
    m_hasPrevViewProj = true;
}

void RenderSystem::RenderVegetation(Scene& scene, const Frustum& frustum,
                                    bgfx::ViewId viewId, bool useDeferred,
                                    bool reflectionView) {
    if (!m_assets) return;
    const bool useGbuffer = useDeferred && m_deferred &&
                            bgfx::isValid(m_instancedGbufferProg);
    const bgfx::ProgramHandle program = useGbuffer
        ? m_instancedGbufferProg : m_instancedProg;
    if (!bgfx::isValid(program)) return;

    auto trunkMesh = m_assets->CreateTreeTrunkMesh();
    auto canopyMesh = m_assets->CreateTreeCanopyMesh();
    auto grassMesh = m_assets->CreateGrassMesh();
    if (!trunkMesh || !canopyMesh || !grassMesh) return;

    auto white = m_assets->WhiteTexture();
    auto flatNormal = m_assets->FlatNormalTexture();
    if (!white || !flatNormal) return;

    const float metallic[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float emissive[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float flags[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    auto submitGroup = [&](Mesh& mesh, const glm::vec4& color, float roughness,
                           bool twoSided, bgfx::InstanceDataBuffer& idb,
                           uint32_t count) {
        // Bind textures inside the submitted draw. Binding them before the
        // vegetation traversal left an unsubmitted uniform set behind when a
        // scene had no vegetation, which breaks the next PBR/reflection draw.
        bgfx::setTexture(0, s_albedoMap, white->Handle());
        bgfx::setTexture(1, s_metallicRoughnessMap, white->Handle());
        bgfx::setTexture(2, s_normalMap, flatNormal->Handle());
        bgfx::setTexture(4, s_irradianceMap, white->Handle());
        bgfx::setTexture(5, s_envMap, white->Handle());
        if (bgfx::isValid(m_brdfLUT)) {
            bgfx::setTexture(6, s_brdfLUT, m_brdfLUT);
        } else {
            bgfx::setTexture(6, s_brdfLUT, white->Handle());
        }
        bgfx::setTexture(7, s_emissiveMap, white->Handle());
        bgfx::setTexture(8, s_occlusionMap, white->Handle());
        bgfx::setTexture(9, s_opacityMap, white->Handle());
        bgfx::setTexture(10, s_metallicMap, white->Handle());
        bgfx::setTexture(11, s_roughnessMap, white->Handle());
        bgfx::setTexture(12, s_specularMap, white->Handle());

        const float albedo[4] = {color.r, color.g, color.b, color.a};
        const float rough[4] = {roughness, 0.0f, 0.0f, 0.0f};
        const float params[4] = {1.0f, 1.0f, 0.0f, twoSided ? 1.0f : 0.0f};
        bgfx::setUniform(u_albedoFactor, albedo);
        bgfx::setUniform(u_metallicFactor, metallic);
        bgfx::setUniform(u_roughnessFactor, rough);
        bgfx::setUniform(u_emissiveFactor, emissive);
        bgfx::setUniform(u_materialParams, params);
        bgfx::setUniform(u_materialFlags, flags);
        bgfx::setInstanceDataBuffer(&idb, 0, count);
        bgfx::setVertexBuffer(0, mesh.VB());
        bgfx::setIndexBuffer(mesh.IB());
        const uint64_t face = reflectionView
            ? (twoSided ? 0 : BGFX_STATE_CULL_CCW)
            : FaceState(twoSided);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       BGFX_STATE_MSAA | face);
        bgfx::submit(viewId, program);
    };

    auto view = scene.Registry().view<TransformComponent, LandscapeComponent,
                                      VegetationComponent>();
    for (auto handle : view) {
        auto& landscape = view.get<LandscapeComponent>(handle);
        auto& vegetation = view.get<VegetationComponent>(handle);
        if (!vegetation.enabled) continue;
        if (vegetation.dirty) {
            VegetationGenerator::Generate(landscape, vegetation);
        }
        if (vegetation.treeInstances.empty() && vegetation.grassInstances.empty()) {
            continue;
        }

        const glm::mat4 world = EntityWorldMatrix(scene, handle);
        float minHeight = 0.0f;
        float maxHeight = 0.0f;
        if (!landscape.heights.empty()) {
            minHeight = *std::min_element(landscape.heights.begin(), landscape.heights.end());
            maxHeight = *std::max_element(landscape.heights.begin(), landscape.heights.end());
        }
        const float halfX = landscape.sizeX * 0.5f * landscape.scale;
        const float halfZ = landscape.sizeZ * 0.5f * landscape.scale;
        const float maxTreeHeight = 5.0f * std::max(1.0f, vegetation.treeMaxScale);
        const glm::vec3 localCenter(0.0f, (minHeight + maxHeight) * 0.5f + maxTreeHeight * 0.5f, 0.0f);
        const glm::vec3 localExtents(halfX, (maxHeight - minHeight) * 0.5f + maxTreeHeight, halfZ);
        const glm::vec3 worldCenter = glm::vec3(world * glm::vec4(localCenter, 1.0f));
        const float maxScale = std::max(
            std::max(glm::length(glm::vec3(world[0])), glm::length(glm::vec3(world[1]))),
            glm::length(glm::vec3(world[2])));
        if (!frustum.TestSphere(worldCenter, glm::length(localExtents) * maxScale)) {
            continue;
        }

        bgfx::setTransform(glm::value_ptr(world));

        if (!vegetation.treeInstances.empty()) {
            bgfx::InstanceDataBuffer idb;
            const uint32_t count = UploadInstances(vegetation.treeInstances, 0, idb);
            if (count > 0) {
                submitGroup(*trunkMesh, vegetation.trunkColor, 0.92f, false, idb, count);
                submitGroup(*canopyMesh, vegetation.canopyColor, 0.82f, false, idb, count);
            }
        }

        if (!vegetation.grassInstances.empty()) {
            bgfx::InstanceDataBuffer idb;
            const uint32_t count = UploadInstances(vegetation.grassInstances, 0, idb);
            if (count > 0) {
                submitGroup(*grassMesh, vegetation.grassColor, 0.76f, true, idb, count);
            }
        }
    }
}

void RenderSystem::RenderVegetationShadows(Scene& scene, bgfx::ProgramHandle program,
                                           bgfx::ViewId viewId) {
    if (!m_assets || !bgfx::isValid(program)) return;

    auto trunkMesh = m_assets->CreateTreeTrunkMesh();
    auto canopyMesh = m_assets->CreateTreeCanopyMesh();
    auto grassMesh = m_assets->CreateGrassMesh();

    auto submitShadow = [&](Mesh& mesh, bool twoSided,
                            bgfx::InstanceDataBuffer& idb, uint32_t count) {
        bgfx::setInstanceDataBuffer(&idb, 0, count);
        bgfx::setVertexBuffer(0, mesh.VB());
        bgfx::setIndexBuffer(mesh.IB());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       FaceState(twoSided));
        bgfx::submit(viewId, program);
    };

    auto view = scene.Registry().view<TransformComponent, LandscapeComponent,
                                      VegetationComponent>();
    for (auto handle : view) {
        auto& vegetation = view.get<VegetationComponent>(handle);
        if (!vegetation.enabled) continue;
        if (vegetation.dirty) {
            VegetationGenerator::Generate(view.get<LandscapeComponent>(handle), vegetation);
        }
        const glm::mat4 world = EntityWorldMatrix(scene, handle);
        bgfx::setTransform(glm::value_ptr(world));

        if (vegetation.castShadows && !vegetation.treeInstances.empty()) {
            bgfx::InstanceDataBuffer idb;
            const uint32_t count = UploadInstances(vegetation.treeInstances, 0, idb);
            if (count > 0) {
                submitShadow(*trunkMesh, false, idb, count);
                submitShadow(*canopyMesh, false, idb, count);
            }
        }
        if (vegetation.grassCastShadows && !vegetation.grassInstances.empty()) {
            bgfx::InstanceDataBuffer idb;
            const uint32_t count = UploadInstances(vegetation.grassInstances, 0, idb);
            if (count > 0) {
                submitShadow(*grassMesh, true, idb, count);
            }
        }
    }
}

void RenderSystem::RenderShadowMap(Scene& scene, const glm::mat4& lightMtx, bgfx::ViewId viewId, bgfx::FrameBufferHandle shadowFbo) {
    if (!bgfx::isValid(m_shadowProg) || !bgfx::isValid(shadowFbo)) return;
    bgfx::setViewFrameBuffer(viewId, shadowFbo);
    bgfx::setViewRect(viewId, 0, 0, 2048, 2048);
    bgfx::setViewClear(viewId, BGFX_CLEAR_DEPTH, 0, 1.0f, 0);

    // Identity для View, lightMtx для Projection (u_viewProj)
    glm::mat4 view = glm::mat4(1.0f);
    bgfx::setViewTransform(viewId, glm::value_ptr(view), glm::value_ptr(lightMtx));

    const bool canInstanceStatic = bgfx::isValid(m_instancedShadowProg);
    std::unordered_map<const Mesh*, ShadowInstanceBatch> instanceBatches;
    if (canInstanceStatic) instanceBatches.reserve(64);

    auto viewIter = scene.Registry().view<TransformComponent, MeshComponent>();
    for (auto h : viewIter) {
        const auto& mc = viewIter.get<MeshComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid()) continue;
        bool twoSided = false;
        if (scene.Registry().all_of<MaterialComponent>(h)) {
            const auto& matc = scene.Registry().get<MaterialComponent>(h);
            if (matc.material) {
                if (matc.material->isGlass) continue;
                twoSided = matc.material->twoSided;
            }
        }

        const glm::mat4 model = EntityWorldMatrix(scene, h);

        // Static meshes can share the shadow draw. Skinned meshes stay on the
        // per-entity path so their existing palette handling is preserved.
        if (canInstanceStatic && !HasSkeletonRoot(scene, h)) {
            const auto result = instanceBatches.try_emplace(mc.mesh.get());
            ShadowInstanceBatch& batch = result.first->second;
            if (result.second) batch.mesh = mc.mesh;
            if (twoSided) {
                batch.twoSided.push_back(model);
            } else {
                batch.singleSided.push_back(model);
            }
            continue;
        }

        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       FaceState(twoSided));
        bgfx::submit(viewId, m_shadowProg);
    }

    if (canInstanceStatic) {
        const uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        for (const auto& entry : instanceBatches) {
            const ShadowInstanceBatch& batch = entry.second;
            if (!batch.mesh) continue;
            SubmitInstancedMesh(viewId, m_instancedShadowProg, *batch.mesh, 0,
                                batch.singleSided, false, state);
            SubmitInstancedMesh(viewId, m_instancedShadowProg, *batch.mesh, 0,
                                batch.twoSided, true, state);
        }
    }

    RenderVegetationShadows(scene, m_instancedShadowProg, viewId);
}


void RenderSystem::RenderPointShadowMap(Scene& scene, const glm::mat4& lightMtx, const glm::vec3& lightPos, bgfx::ViewId viewId, bgfx::FrameBufferHandle shadowFbo) {
    if (!bgfx::isValid(m_shadowPointProg) || !bgfx::isValid(shadowFbo)) return;
    bgfx::setViewFrameBuffer(viewId, shadowFbo);
    bgfx::setViewRect(viewId, 0, 0, 1024, 1024);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x00000000, 1.0f, 0);

    float lp[4] = { lightPos.x, lightPos.y, lightPos.z, 1.0f };
    bgfx::setUniform(u_shadowLightPos, lp);

    glm::mat4 view = glm::mat4(1.0f);
    bgfx::setViewTransform(viewId, glm::value_ptr(view), glm::value_ptr(lightMtx));

    const bool canInstanceStatic = bgfx::isValid(m_instancedShadowPointProg);
    std::unordered_map<const Mesh*, ShadowInstanceBatch> instanceBatches;
    if (canInstanceStatic) instanceBatches.reserve(64);

    BeginBonePaletteCache();
    auto viewIter = scene.Registry().view<TransformComponent, MeshComponent>();
    for (auto h : viewIter) {
        const auto& mc = viewIter.get<MeshComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid()) continue;
        bool twoSided = false;
        if (scene.Registry().all_of<MaterialComponent>(h)) {
            const auto& matc = scene.Registry().get<MaterialComponent>(h);
            if (matc.material) {
                if (matc.material->isGlass) continue;
                twoSided = matc.material->twoSided;
            }
        }

        const glm::mat4 model = EntityWorldMatrix(scene, h);

        if (canInstanceStatic && !HasSkeletonRoot(scene, h)) {
            const auto result = instanceBatches.try_emplace(mc.mesh.get());
            ShadowInstanceBatch& batch = result.first->second;
            if (result.second) batch.mesh = mc.mesh;
            if (twoSided) {
                batch.twoSided.push_back(model);
            } else {
                batch.singleSided.push_back(model);
            }
            continue;
        }

        // Skeletal animation support
        const entt::entity root = FindRootEntity(scene, h);
        const bool hasSkeleton =
            scene.Registry().all_of<SkeletalComponent>(root);
        BindBonePalette(scene,
                        static_cast<uint32_t>(entt::to_integral(root)),
                        hasSkeleton);

        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       FaceState(twoSided));
        bgfx::submit(viewId, m_shadowPointProg);
    }

    if (canInstanceStatic) {
        const uint64_t state = BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS;
        for (const auto& entry : instanceBatches) {
            const ShadowInstanceBatch& batch = entry.second;
            if (!batch.mesh) continue;
            SubmitInstancedMesh(viewId, m_instancedShadowPointProg, *batch.mesh, 0,
                                batch.singleSided, false, state);
            SubmitInstancedMesh(viewId, m_instancedShadowPointProg, *batch.mesh, 0,
                                batch.twoSided, true, state);
        }
    }

    RenderVegetationShadows(scene, m_instancedShadowPointProg, viewId);
}

void RenderSystem::BindMaterial(const Material& mat) {
    const float alb[4] = {mat.albedoFactor.r, mat.albedoFactor.g,
                          mat.albedoFactor.b, mat.albedoFactor.a};
    const float met[4] = {mat.metallicFactor, 0, 0, 0};
    const float rgh[4] = {mat.roughnessFactor, 0, 0, 0};
    const float emi[4] = {mat.emissiveFactor.r, mat.emissiveFactor.g,
                          mat.emissiveFactor.b, 0};
    const float params[4] = {mat.normalStrength, mat.occlusionStrength,
                             mat.alphaCutoff, mat.twoSided ? 1.0f : 0.0f};
    const float flags[4] = {
        mat.metallicMap ? 1.0f : 0.0f,
        mat.roughnessMap ? 1.0f : 0.0f,
        mat.specularMap ? 1.0f : 0.0f,
        mat.opacityMap ? 1.0f : 0.0f
    };
    bgfx::setUniform(u_albedoFactor, alb);
    bgfx::setUniform(u_metallicFactor, met);
    bgfx::setUniform(u_roughnessFactor, rgh);
    bgfx::setUniform(u_emissiveFactor, emi);
    bgfx::setUniform(u_materialParams, params);
    bgfx::setUniform(u_materialFlags, flags);

    auto white = m_assets->WhiteTexture();
    auto flatN = m_assets->FlatNormalTexture();
    auto albedo = mat.albedoMap ? mat.albedoMap : white;
    auto mr = mat.metallicRoughnessMap ? mat.metallicRoughnessMap : white;
    auto nrm = mat.normalMap ? mat.normalMap : flatN;
    auto emissive = mat.emissiveMap ? mat.emissiveMap : white;
    auto occlusion = mat.occlusionMap ? mat.occlusionMap : white;
    auto opacity = mat.opacityMap ? mat.opacityMap : white;
    auto metallic = mat.metallicMap ? mat.metallicMap : white;
    auto roughness = mat.roughnessMap ? mat.roughnessMap : white;
    auto specular = mat.specularMap ? mat.specularMap : white;
    bgfx::setTexture(0, s_albedoMap, albedo->Handle());
    bgfx::setTexture(1, s_metallicRoughnessMap, mr->Handle());
    bgfx::setTexture(2, s_normalMap, nrm->Handle());
    // Forward PBR needs valid samplers even when the reflection pass has no
    // scene IBL binding available. White fallbacks are neutral and avoid
    // sampling stale/unbound texture slots.
    bgfx::setTexture(4, s_irradianceMap, white->Handle());
    bgfx::setTexture(5, s_envMap, white->Handle());
    bgfx::setTexture(7, s_emissiveMap, emissive->Handle());
    bgfx::setTexture(8, s_occlusionMap, occlusion->Handle());
    bgfx::setTexture(9, s_opacityMap, opacity->Handle());
    bgfx::setTexture(10, s_metallicMap, metallic->Handle());
    bgfx::setTexture(11, s_roughnessMap, roughness->Handle());
    bgfx::setTexture(12, s_specularMap, specular->Handle());
    bgfx::setTexture(6, s_brdfLUT,
                     bgfx::isValid(m_brdfLUT) ? m_brdfLUT : white->Handle());
}

bool RenderSystem::EnsureReflectionTarget(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) return false;
    if (bgfx::isValid(m_reflectionFbo) &&
        m_reflectionWidth == width && m_reflectionHeight == height) {
        return true;
    }

    DestroyReflectionTarget();

    const uint64_t flags = BGFX_TEXTURE_RT |
                           BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;
    m_reflectionColor = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    m_reflectionDepth = bgfx::createTexture2D(
        width, height, false, 1, bgfx::TextureFormat::D32F, flags);
    if (!bgfx::isValid(m_reflectionColor) || !bgfx::isValid(m_reflectionDepth)) {
        DestroyReflectionTarget();
        return false;
    }

    const bgfx::TextureHandle attachments[] = {
        m_reflectionColor, m_reflectionDepth
    };
    m_reflectionFbo = bgfx::createFrameBuffer(2, attachments, false);
    if (!bgfx::isValid(m_reflectionFbo)) {
        DestroyReflectionTarget();
        return false;
    }

    m_reflectionWidth = width;
    m_reflectionHeight = height;
    return true;
}

void RenderSystem::DestroyReflectionTarget() {
    if (bgfx::isValid(m_reflectionFbo)) bgfx::destroy(m_reflectionFbo);
    if (bgfx::isValid(m_reflectionColor)) bgfx::destroy(m_reflectionColor);
    if (bgfx::isValid(m_reflectionDepth)) bgfx::destroy(m_reflectionDepth);
    m_reflectionFbo = BGFX_INVALID_HANDLE;
    m_reflectionColor = BGFX_INVALID_HANDLE;
    m_reflectionDepth = BGFX_INVALID_HANDLE;
    m_reflectionWidth = 0;
    m_reflectionHeight = 0;
    m_planarActive = false;
}

bool RenderSystem::RenderPlanarReflection(Scene& scene, const Camera& camera,
                                           bgfx::ViewId viewId,
                                           uint16_t width, uint16_t height) {
    const bool wasActive = m_planarActive;
    m_planarActive = false;
    if (!m_assets || !bgfx::isValid(m_program) || width == 0 || height == 0) {
        return false;
    }

    const glm::vec3 cameraPos = camera.Position();
    const Frustum mainFrustum(camera.Projection() * camera.View());
    struct Candidate {
        uint32_t entity = 0xFFFFFFFFu;
        float planeY = 0.0f;
        float distanceSq = std::numeric_limits<float>::max();
    };
    Candidate best;

    auto waterView = scene.Registry().view<TransformComponent, WaterComponent>();
    for (auto handle : waterView) {
        const auto& water = waterView.get<WaterComponent>(handle);
        if (!water.enabled || !water.planarReflections ||
            water.width <= 0.0f || water.depth <= 0.0f) {
            continue;
        }
        const glm::mat4 world = EntityWorldMatrix(scene, handle);
        const float planeY = world[3].y;

        // A reflection below the waterline is not meaningful for the normal
        // walk-on-water view and makes the mirrored camera cross the surface.
        if (cameraPos.y <= planeY + 0.01f) continue;

        const glm::vec3 center(world[3]);
        const float surfaceRadius = 0.5f * std::sqrt(
            water.width * water.width + water.depth * water.depth) +
            std::max(water.waveHeight, 0.1f);
        if (!mainFrustum.TestSphere(center, surfaceRadius)) continue;
        const float distanceSq = glm::dot(center - cameraPos, center - cameraPos);
        if (distanceSq < best.distanceSq) {
            best.entity = static_cast<uint32_t>(entt::to_integral(handle));
            best.planeY = planeY;
            best.distanceSq = distanceSq;
        }
    }

    if (best.entity == 0xFFFFFFFFu) {
        m_planarHasLastView = false;
        m_planarCadence = 0;
        return false;
    }

    const bool targetWasValid =
        bgfx::isValid(m_reflectionColor) &&
        m_reflectionWidth == width && m_reflectionHeight == height;
    if (!EnsureReflectionTarget(width, height)) {
        m_planarHasLastView = false;
        m_planarCadence = 0;
        return false;
    }

    // Reflect the scene across the horizontal water plane by mirroring the
    // main view. Keeping the main projection makes the reflected image line
    // up with the water pixel's screen UV, so the water shader can sample it
    // directly.
    glm::mat4 mirror(1.0f);
    mirror[1][1] = -1.0f;
    mirror[3][1] = 2.0f * best.planeY;
    const glm::mat4 reflectionView = camera.View() * mirror;
    const glm::mat4 reflectionVP = camera.Projection() * reflectionView;
    const glm::vec3 reflectionEye = glm::vec3(
        mirror * glm::vec4(cameraPos, 1.0f));

    const bool samePlane = wasActive &&
        std::abs(m_planarPlaneY - best.planeY) < 1e-4f;
    const bool sameView = m_planarHasLastView &&
        MatricesNearlyEqual(m_planarLastView, reflectionView) &&
        MatricesNearlyEqual(m_planarLastProj, camera.Projection());
    if (targetWasValid && samePlane && sameView) {
        ++m_planarCadence;
        if ((m_planarCadence & 1u) != 0u) {
            m_planarActive = true;
            return true;
        }
    } else {
        m_planarCadence = 0;
    }

    m_planarPlaneY = best.planeY;
    m_planarActive = true;

    bgfx::setViewFrameBuffer(viewId, m_reflectionFbo);
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
                       0x00000000u, 1.0f, 0);
    bgfx::setViewTransform(viewId, glm::value_ptr(reflectionView),
                           glm::value_ptr(camera.Projection()));
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    const Frustum frustum(reflectionVP);
    const float cam[4] = {reflectionEye.x, reflectionEye.y,
                          reflectionEye.z, 1.0f};
    glm::vec3 lightDir{-0.4f, -0.8f, -0.5f};
    glm::vec3 lightCol{1.0f};
    float lightInt = 1.2f;
    auto lightView = scene.Registry().view<DirectionalLightComponent>();
    if (!lightView.empty()) {
        auto h = *lightView.begin();
        lightDir = lightView.get<DirectionalLightComponent>(h).direction;
        lightCol = lightView.get<DirectionalLightComponent>(h).color;
        lightInt = lightView.get<DirectionalLightComponent>(h).intensity;
    }
    const float lDir[4] = {lightDir.x, lightDir.y, lightDir.z, 0.0f};
    const float lCol[4] = {lightCol.r, lightCol.g, lightCol.b, lightInt};
    const float clipPlane[4] = {0.0f, 1.0f, 0.0f, -m_planarPlaneY};
    bgfx::setUniform(u_cameraPosition, cam);
    bgfx::setUniform(u_lightDirection, lDir);
    bgfx::setUniform(u_lightColorIntensity, lCol);
    bgfx::setUniform(u_reflectionClip, clipPlane);
    bgfx::setUniform(u_prevViewProj, glm::value_ptr(reflectionVP));

    BeginBonePaletteCache();
    auto view = scene.Registry().view<TransformComponent, MeshComponent,
                                      MaterialComponent>();
    for (auto handle : view) {
        const auto& meshComponent = view.get<MeshComponent>(handle);
        const auto& materialComponent = view.get<MaterialComponent>(handle);
        if (!meshComponent.mesh || !meshComponent.mesh->Valid() ||
            !materialComponent.material ||
            materialComponent.material->isGlass) {
            continue;
        }

        const glm::mat4 model = EntityWorldMatrix(scene, handle);
        glm::vec3 localMin, localMax;
        meshComponent.mesh->LocalAABB(localMin, localMax);
        const glm::vec3 localCenter = (localMin + localMax) * 0.5f;
        const float localRadius = glm::length(localMax - localMin) * 0.5f;
        const glm::vec3 worldCenter = glm::vec3(
            model * glm::vec4(localCenter, 1.0f));
        const float maxScale = std::max(
            std::max(glm::length(glm::vec3(model[0])),
                     glm::length(glm::vec3(model[1]))),
            glm::length(glm::vec3(model[2])));
        if (!frustum.TestSphere(worldCenter, localRadius * maxScale)) continue;

        const entt::entity root = FindRootEntity(scene, handle);
        const bool hasSkeleton =
            scene.Registry().all_of<SkeletalComponent>(root);
        BindBonePalette(scene,
                        static_cast<uint32_t>(entt::to_integral(root)),
                        hasSkeleton);
        bgfx::setUniform(u_prevModel, glm::value_ptr(model));
        bgfx::setTransform(glm::value_ptr(model));
        BindMaterial(*materialComponent.material);
        bgfx::setVertexBuffer(0, meshComponent.mesh->VB());
        bgfx::setIndexBuffer(meshComponent.mesh->IB());

        // The mirrored view reverses triangle winding, so front faces are
        // clockwise in the reflection target. Two-sided materials still skip
        // culling entirely.
        const uint64_t state = BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                               BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                               BGFX_STATE_MSAA |
                               (materialComponent.material->twoSided
                                    ? 0 : BGFX_STATE_CULL_CCW);
        bgfx::setState(state);
        bgfx::submit(viewId, m_program);
    }

    RenderVegetation(scene, frustum, viewId, false, true);
    m_planarLastView = reflectionView;
    m_planarLastProj = camera.Projection();
    m_planarHasLastView = true;
    return true;
}

void RenderSystem::RenderWater(Scene& scene, const Camera& camera,
                               bgfx::TextureHandle sceneColor,
                               bgfx::TextureHandle sceneDepth,
                               bgfx::TextureHandle envCube,
                               bgfx::TextureHandle planarReflection,
                               float planarPlaneY,
                               const float lightDir[4],
                               const float lightColorIntensity[4],
                               bgfx::ViewId viewId, uint16_t width, uint16_t height,
                               float time, float nearZ, float farZ) {
    if (!bgfx::isValid(m_waterProg) || !m_assets) return;
    const std::shared_ptr<Mesh> waterMesh = m_assets->CreateWaterMesh();
    if (!waterMesh || !waterMesh->Valid()) return;

    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                           glm::value_ptr(camera.Projection()));
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    const glm::vec3 camPos = camera.Position();
    struct WaterItem {
        const WaterComponent* water = nullptr;
        glm::mat4 model{1.0f};
        float distanceSq = 0.0f;
    };
    std::vector<WaterItem> items;
    items.reserve(8);

    const glm::mat4 viewProj = camera.Projection() * camera.View();
    const Frustum frustum(viewProj);

    auto view = scene.Registry().view<TransformComponent, WaterComponent>();
    for (auto h : view) {
        const WaterComponent& water = view.get<WaterComponent>(h);
        if (!water.enabled || water.width <= 0.0f || water.depth <= 0.0f) {
            continue;
        }

        WaterItem item;
        item.water = &water;

        entt::entity curr = h;
        std::vector<entt::entity> chain;
        int depth = 0;
        while (curr != entt::null && depth++ < 128) {
            chain.push_back(curr);
            if (scene.Registry().all_of<ParentComponent>(curr))
                curr = scene.Registry().get<ParentComponent>(curr).parent;
            else
                break;
        }
        glm::mat4 world(1.0f);
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it)) {
                world = world * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
            }
        }

        const glm::vec3 center = glm::vec3(world[3]);
        const float radius = 0.5f * std::sqrt(water.width * water.width +
                                              water.depth * water.depth);
        if (!frustum.TestSphere(center, radius + std::max(water.waveHeight, 0.1f))) {
            continue;
        }

        item.model = world * glm::scale(glm::mat4(1.0f),
                                        glm::vec3(water.width, 1.0f, water.depth));
        item.distanceSq = glm::dot(center - camPos, center - camPos);
        items.push_back(item);
    }

    // Alpha blending is order-dependent; draw distant water first.
    std::sort(items.begin(), items.end(),
              [](const WaterItem& a, const WaterItem& b) {
                  return a.distanceSq > b.distanceSq;
              });
    if (items.empty()) return;

    glm::vec3 forward = -glm::vec3(camera.View()[0][2],
                                    camera.View()[1][2],
                                    camera.View()[2][2]);
    if (glm::length(forward) > 1e-5f) forward = glm::normalize(forward);
    const float cam[4] = {camPos.x, camPos.y, camPos.z, 1.0f};
    const float fwd[4] = {forward.x, forward.y, forward.z, 0.0f};
    const float nearFar[4] = {1.0f / std::max(nearZ, 0.0001f),
                              1.0f / std::max(farZ, 0.0001f), 0.0f, 0.0f};
    bgfx::setUniform(u_cameraPosition, cam);
    bgfx::setUniform(u_cameraForward, fwd);
    bgfx::setUniform(u_waterInvNearFar, nearFar);
    bgfx::setUniform(u_lightDirection, lightDir);
    bgfx::setUniform(u_lightColorIntensity, lightColorIntensity);
    bgfx::setTexture(0, s_waterSceneColor, sceneColor);
    bgfx::setTexture(1, s_waterDepth, sceneDepth);
    bgfx::setTexture(2, s_waterEnvCube, envCube);
    const bgfx::TextureHandle planarTexture =
        bgfx::isValid(planarReflection)
            ? planarReflection : m_assets->WhiteTexture()->Handle();
    bgfx::setTexture(3, s_waterPlanar, planarTexture);

    for (const WaterItem& item : items) {
        const WaterComponent& water = *item.water;
        const float p0[4] = {time, water.waveHeight,
                             std::max(water.waveScale, 0.01f), water.waveSpeed};
        const float p1[4] = {water.choppiness, water.detailStrength,
                             water.refractionStrength, water.reflectionStrength};
        const float p2[4] = {std::max(water.depthFade, 0.05f),
                             std::max(water.absorption, 0.0f),
                             std::max(water.foamDistance, 0.01f),
                             std::max(water.foamIntensity, 0.0f)};
        const float p3[4] = {water.clarity, water.transparency,
                             std::max(water.fresnelPower, 1.0f), water.sparkle};
        const float flow[4] = {water.flowDirection.x, water.flowDirection.y, 0.0f, 1.0f};
        const float shallow[4] = {water.shallowColor.r, water.shallowColor.g,
                                  water.shallowColor.b, 1.0f};
        const float deep[4] = {water.deepColor.r, water.deepColor.g,
                               water.deepColor.b, 1.0f};
        const float foam[4] = {water.foamColor.r, water.foamColor.g,
                               water.foamColor.b, 1.0f};
        // The reflection target is valid for one horizontal plane. Surfaces
        // at other heights keep the cubemap reflection instead of sampling
        // an image mirrored across the wrong plane.
        const float itemPlaneY = item.model[3].y;
        const bool usePlanar = bgfx::isValid(planarReflection) &&
            std::abs(itemPlaneY - planarPlaneY) < 0.05f;
        const float planar[4] = {usePlanar ? 1.0f : 0.0f,
                                 planarPlaneY, 1.0f, 0.0f};

        bgfx::setUniform(u_waterParams0, p0);
        bgfx::setUniform(u_waterParams1, p1);
        bgfx::setUniform(u_waterParams2, p2);
        bgfx::setUniform(u_waterParams3, p3);
        bgfx::setUniform(u_waterFlow, flow);
        bgfx::setUniform(u_waterShallow, shallow);
        bgfx::setUniform(u_waterDeep, deep);
        bgfx::setUniform(u_waterFoam, foam);
        bgfx::setUniform(u_waterPlanar, planar);

        bgfx::setTransform(glm::value_ptr(item.model));
        bgfx::setVertexBuffer(0, waterMesh->VB());
        bgfx::setIndexBuffer(waterMesh->IB());
        // Depth is checked manually in the shader against the copied opaque
        // depth; this target is color-only and has no usable hardware depth.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA);
        bgfx::submit(viewId, m_waterProg);
    }
}

void RenderSystem::RenderHair(Scene& scene, const Camera& camera,
                              bgfx::TextureHandle sceneDepth,
                              bgfx::TextureHandle envCube,
                              const float sunDir[4],
                              const float lightColorIntensity[4],
                              bgfx::ViewId viewId, uint16_t width, uint16_t height,
                              float nearZ, float farZ) {
    if (!bgfx::isValid(m_hairProg) || !bgfx::isValid(sceneDepth)) return;

    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                           glm::value_ptr(camera.Projection()));
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    const glm::vec3 camPos = camera.Position();
    glm::vec3 forward = -glm::vec3(camera.View()[0][2],
                                    camera.View()[1][2],
                                    camera.View()[2][2]);
    if (glm::length(forward) > 1e-5f) forward = glm::normalize(forward);
    const float cam[4] = {camPos.x, camPos.y, camPos.z, 1.0f};
    const float fwd[4] = {forward.x, forward.y, forward.z, 0.0f};
    const float nearFar[4] = {1.0f / std::max(nearZ, 0.0001f),
                              1.0f / std::max(farZ, 0.0001f), 0.0f, 0.0f};
    bgfx::setUniform(u_cameraPosition, cam);
    bgfx::setUniform(u_cameraForward, fwd);
    bgfx::setUniform(u_hairInvNearFar, nearFar);
    bgfx::setUniform(u_lightDirection, sunDir);
    bgfx::setUniform(u_lightColorIntensity, lightColorIntensity);
    bgfx::setTexture(1, s_hairDepth, sceneDepth);
    bgfx::setTexture(2, s_hairEnvCube, envCube);

    struct HairItem {
        GroomComponent* groom = nullptr;
        GroomData* data = nullptr;
        float distanceSq = 0.0f;
    };
    std::vector<HairItem> items;
    items.reserve(16);

    const glm::mat4 viewProj = camera.Projection() * camera.View();
    const Frustum frustum(viewProj);
    auto view = scene.Registry().view<GroomComponent>();
    for (auto h : view) {
        GroomComponent& groom = view.get<GroomComponent>(h);
        if (!groom.enabled || !groom.runtime || groom.runtime->strands.empty()) {
            continue;
        }
        GroomData& data = *groom.runtime;
        if (data.boundsRadius > 0.0f &&
            !frustum.TestSphere(data.boundsCenter, data.boundsRadius)) {
            continue;
        }
        HairItem item;
        item.groom = &groom;
        item.data = &data;
        item.distanceSq = glm::dot(data.boundsCenter - camPos,
                                   data.boundsCenter - camPos);
        items.push_back(item);
    }

    std::sort(items.begin(), items.end(), [](const HairItem& a, const HairItem& b) {
        return a.distanceSq > b.distanceSq;
    });
    const glm::mat4 identity(1.0f);
    for (const HairItem& item : items) {
        GroomComponent& groom = *item.groom;
        GroomData& data = *item.data;

        data.width = std::max(groom.width, 0.0001f);
        data.rootWidth = std::max(groom.rootWidth, 0.0f);
        data.tipWidth = std::max(groom.tipWidth, 0.0f);
        GroomSystem::BuildRenderMesh(data, camPos);
        if (!data.renderMesh || !data.renderMesh->Valid()) continue;

        const float params0[4] = {groom.specular, std::max(groom.shininess, 1.0f),
                                  groom.ambient, groom.opacity};
        const float params1[4] = {groom.randomTint, 0.35f, 0.75f, 0.0f};
        const float root[4] = {groom.rootColor.r, groom.rootColor.g,
                               groom.rootColor.b, 1.0f};
        const float tip[4] = {groom.tipColor.r, groom.tipColor.g,
                              groom.tipColor.b, 1.0f};
        bgfx::setUniform(u_hairParams0, params0);
        bgfx::setUniform(u_hairParams1, params1);
        bgfx::setUniform(u_hairRootColor, root);
        bgfx::setUniform(u_hairTipColor, tip);

        bgfx::setTransform(glm::value_ptr(identity));
        bgfx::setVertexBuffer(0, data.renderMesh->VB());
        bgfx::setIndexBuffer(data.renderMesh->IB());
        // The shader performs the depth test against the copied opaque depth;
        // this color-only pass never writes depth.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_BLEND_ALPHA | BGFX_STATE_MSAA);
        bgfx::submit(viewId, m_hairProg);
    }
}

void RenderSystem::RenderGlass(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_glassProg)) return;
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()), glm::value_ptr(camera.Projection()));
    bgfx::setViewRect(viewId, 0, 0, width, height);
    // The color attachment already contains the post-processed opaque image
    // and the depth attachment still contains the opaque scene depth. Do not
    // clear either one.
    bgfx::setViewClear(viewId, BGFX_CLEAR_NONE);
    // Sequential submission is required for back-to-front alpha blending.
    bgfx::setViewMode(viewId, bgfx::ViewMode::Sequential);

    struct GlassItem {
        entt::entity entity = entt::null;
        glm::mat4 model{1.0f};
        std::shared_ptr<Mesh> mesh;
        std::shared_ptr<Material> material;
        float distanceSq = 0.0f;
    };

    const glm::vec3 camPos = camera.Position();
    std::vector<GlassItem> items;
    items.reserve(64);

    auto view = scene.Registry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto h : view) {
        const auto& mc = view.get<MeshComponent>(h);
        const auto& matc = view.get<MaterialComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid() || !matc.material || !matc.material->isGlass) continue;

        GlassItem item;
        item.entity = h;
        item.mesh = mc.mesh;
        item.material = matc.material;

        entt::entity curr = h;
        std::vector<entt::entity> chain;
        int __depth = 0; while (curr != entt::null && __depth++ < 128) {
            chain.push_back(curr);
            if (scene.Registry().all_of<ParentComponent>(curr))
                curr = scene.Registry().get<ParentComponent>(curr).parent;
            else break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it))
                item.model = item.model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }

        const glm::vec3 worldPos = glm::vec3(item.model[3]);
        item.distanceSq = glm::dot(worldPos - camPos, worldPos - camPos);
        items.push_back(std::move(item));
    }

    // Painter's algorithm: furthest glass first.
    std::sort(items.begin(), items.end(), [](const GlassItem& a, const GlassItem& b) {
        return a.distanceSq > b.distanceSq;
    });

    const float cam[4] = {camPos.x, camPos.y, camPos.z, 1.0f};
    bgfx::setUniform(u_cameraPosition, cam);

    auto white = m_assets->WhiteTexture(), flatN = m_assets->FlatNormalTexture();
    BeginBonePaletteCache();
    for (const GlassItem& item : items) {
        const Material& mat = *item.material;
        const float alb[4] = {mat.albedoFactor.r, mat.albedoFactor.g, mat.albedoFactor.b, mat.albedoFactor.a};
        const float glass[4] = {mat.glassOpacity, mat.normalStrength, mat.roughnessFactor,
                                mat.twoSided ? 1.0f : 0.0f};
        bgfx::setUniform(u_albedoFactor, alb);
        bgfx::setUniform(u_glassParams, glass);

        auto albedo = mat.albedoMap ? mat.albedoMap : white;
        auto nrm = mat.normalMap ? mat.normalMap : flatN;
        auto opacity = mat.opacityMap ? mat.opacityMap : white;
        bgfx::setTexture(0, s_albedoMap, albedo->Handle());
        bgfx::setTexture(2, s_normalMap, nrm->Handle());
        bgfx::setTexture(9, s_opacityMap, opacity->Handle());

        // vs_pbr always samples the bone palette, so bind real skeletal data
        // when available and identity matrices otherwise.
        const entt::entity root = FindRootEntity(scene, item.entity);
        const bool hasSkeleton =
            scene.Registry().all_of<SkeletalComponent>(root);
        BindBonePalette(scene,
                        static_cast<uint32_t>(entt::to_integral(root)),
                        hasSkeleton);

        bgfx::setTransform(glm::value_ptr(item.model));
        bgfx::setVertexBuffer(0, item.mesh->VB());
        bgfx::setIndexBuffer(item.mesh->IB());
        // Depth test against opaque geometry, but never write depth so glass
        // fragments do not occlude each other incorrectly.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA |
                       BGFX_STATE_MSAA | FaceState(mat.twoSided));
        bgfx::submit(viewId, m_glassProg);
    }
}
} // namespace Galeshi
