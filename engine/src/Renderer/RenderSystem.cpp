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
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>
#include <algorithm>
#include <cstring>
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
    m_instancedProg = ShaderManager::LoadProgram(
        "shaders/vs_vegetation_instanced.bin", "shaders/fs_pbr.bin");
    m_instancedGbufferProg = ShaderManager::LoadProgram(
        "shaders/vs_vegetation_instanced.bin", "shaders/fs_gbuffer.bin");
    m_instancedShadowProg = ShaderManager::LoadProgram(
        "shaders/vs_vegetation_shadow_instanced.bin", "shaders/fs_shadow.bin");
    m_instancedShadowPointProg = ShaderManager::LoadProgram(
        "shaders/vs_vegetation_instanced.bin", "shaders/fs_shadow_point.bin");
    spdlog::info("[RenderSystem] programs: pbr={} gbuffer={} shadow={} shadowPoint={} glass={} vegetation={}",
        bgfx::isValid(m_program), bgfx::isValid(m_gbufferProg),
        bgfx::isValid(m_shadowProg), bgfx::isValid(m_shadowPointProg),
        bgfx::isValid(m_glassProg), bgfx::isValid(m_instancedProg));
    u_shadowLightPos = bgfx::createUniform("u_shadowLightPos", bgfx::UniformType::Vec4);
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
    
    s_boneMatrices = bgfx::createUniform("s_boneMatrices", bgfx::UniformType::Sampler);
    m_boneTexture = bgfx::createTexture2D(4, kMaxBones, false, 1, bgfx::TextureFormat::RGBA32F);

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
    if (bgfx::isValid(m_program)) bgfx::destroy(m_program);
    if (bgfx::isValid(m_gbufferProg)) bgfx::destroy(m_gbufferProg);
    if (bgfx::isValid(m_shadowProg)) bgfx::destroy(m_shadowProg);
    if (bgfx::isValid(m_shadowPointProg)) bgfx::destroy(m_shadowPointProg);
    if (bgfx::isValid(m_glassProg)) bgfx::destroy(m_glassProg);
    if (bgfx::isValid(m_instancedProg)) bgfx::destroy(m_instancedProg);
    if (bgfx::isValid(m_instancedGbufferProg)) bgfx::destroy(m_instancedGbufferProg);
    if (bgfx::isValid(m_instancedShadowProg)) bgfx::destroy(m_instancedShadowProg);
    if (bgfx::isValid(m_instancedShadowPointProg)) bgfx::destroy(m_instancedShadowPointProg);
    if (bgfx::isValid(u_shadowLightPos)) bgfx::destroy(u_shadowLightPos);
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
    if (bgfx::isValid(s_boneMatrices)) bgfx::destroy(s_boneMatrices);
    if (bgfx::isValid(m_boneTexture)) bgfx::destroy(m_boneTexture);
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

void RenderSystem::Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_program) && !bgfx::isValid(m_gbufferProg)) return;

    Galeshi::Frustum frustum(camera.Projection() * camera.View());

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

    static std::vector<glm::vec4> identityBoneData(kMaxBones * 4, glm::vec4(0.0f));
    static bool initIdentity = false;
    if (!initIdentity) {
        glm::mat4 id(1.0f);
        for (int i = 0; i < kMaxBones; ++i) {
            for (int j = 0; j < 4; ++j) identityBoneData[i * 4 + j] = id[j];
        }
        initIdentity = true;
    }

    auto view = scene.Registry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto h : view) {
        const auto& mc = view.get<MeshComponent>(h); const auto& matc = view.get<MaterialComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid() || !matc.material) continue;
        // Transparent glass is submitted later in RenderGlass, after the
        // post-processed image has been copied into the color attachment.
        if (matc.material->isGlass) continue;

        glm::mat4 model(1.0f);
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
                model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }

        // Frustum Culling (before any bindings!)
        {
            glm::vec3 localMin, localMax;
            mc.mesh->LocalAABB(localMin, localMax);
            glm::vec3 localCenter = (localMin + localMax) * 0.5f;
            float localRadius = glm::length(localMax - localMin) * 0.5f;
            glm::vec3 worldCenter = glm::vec3(model * glm::vec4(localCenter, 1.0f));
            float maxScale = std::max(std::max(glm::length(glm::vec3(model[0])), glm::length(glm::vec3(model[1]))), glm::length(glm::vec3(model[2])));
            if (!frustum.TestSphere(worldCenter, localRadius * maxScale)) {
                continue; // Skip culled object BEFORE any texture/uniform binding
            }
        }

        // Ищем SkeletalComponent вверх по иерархии (на корне)
        entt::entity root = h;
        while (root != entt::null) {
            if (scene.Registry().all_of<ParentComponent>(root)) 
                root = scene.Registry().get<ParentComponent>(root).parent;
            else break;
        }
        bool hasSkeleton = scene.Registry().all_of<SkeletalComponent>(root);
        if (hasSkeleton) {
            auto& skel = scene.Registry().get<SkeletalComponent>(root);
            if (skel.isAnimated && !skel.sourcePath.empty()) {
                if (skel.boneMatrices.size() != kMaxBones) {
                    skel.boneMatrices.resize(kMaxBones, glm::mat4(1.0f));
                }
                // Animation updated in OnUpdate (Parallel)
                
                std::vector<glm::vec4> boneData(kMaxBones * 4);
                for (int i = 0; i < kMaxBones; ++i) {
                    for (int j = 0; j < 4; ++j) boneData[i * 4 + j] = skel.boneMatrices[i][j];
                }
                bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones, bgfx::copy(boneData.data(), kMaxBones * 4 * sizeof(glm::vec4)));
            } else {
                bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones, bgfx::copy(identityBoneData.data(), kMaxBones * 4 * sizeof(glm::vec4)));
            }
        } else {
            bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones, bgfx::copy(identityBoneData.data(), kMaxBones * 4 * sizeof(glm::vec4)));
        }
        bgfx::setTexture(3, s_boneMatrices, m_boneTexture);
        // IBL textures (slots 3, 4, 5 in shader, but 3 is bones here. Need to shift slots in shader)
        // Actually, fs_pbr uses slots 0,1,2 for maps. Bones is 3.
        // I will shift IBL to 4,5,6 in shader
        // (In this quick fix, I'll rely on PostProcessManager for IBL since we have limited time)

        
        bgfx::setTransform(glm::value_ptr(model));
        const Material& mat = *matc.material;
        const float alb[4] = {mat.albedoFactor.r, mat.albedoFactor.g, mat.albedoFactor.b, mat.albedoFactor.a};
        const float met[4] = {mat.metallicFactor, 0, 0, 0}, rgh[4] = {mat.roughnessFactor, 0, 0, 0}, emi[4] = {mat.emissiveFactor.r, mat.emissiveFactor.g, mat.emissiveFactor.b, 0};
        const float params[4] = {mat.normalStrength, mat.occlusionStrength, mat.alphaCutoff,
                                 mat.twoSided ? 1.0f : 0.0f};
        const float flags[4] = {
            mat.metallicMap ? 1.0f : 0.0f,
            mat.roughnessMap ? 1.0f : 0.0f,
            mat.specularMap ? 1.0f : 0.0f,
            mat.opacityMap ? 1.0f : 0.0f
        };
        bgfx::setUniform(u_albedoFactor, alb); bgfx::setUniform(u_metallicFactor, met);
        bgfx::setUniform(u_roughnessFactor, rgh); bgfx::setUniform(u_emissiveFactor, emi);
        bgfx::setUniform(u_materialParams, params);
        bgfx::setUniform(u_materialFlags, flags);

        auto white = m_assets->WhiteTexture(), flatN = m_assets->FlatNormalTexture();
        auto albedo = mat.albedoMap ? mat.albedoMap : white;
        auto mr = mat.metallicRoughnessMap ? mat.metallicRoughnessMap : white;
        auto nrm = mat.normalMap ? mat.normalMap : flatN;
        // White fallback keeps a non-zero emissive factor usable even when the
        // material has no emissive texture.
        auto emissive = mat.emissiveMap ? mat.emissiveMap : white;
        auto occlusion = mat.occlusionMap ? mat.occlusionMap : white;
        auto opacity = mat.opacityMap ? mat.opacityMap : white;
        auto metallic = mat.metallicMap ? mat.metallicMap : white;
        auto roughness = mat.roughnessMap ? mat.roughnessMap : white;
        auto specular = mat.specularMap ? mat.specularMap : white;
        bgfx::setTexture(0, s_albedoMap, albedo->Handle());
        bgfx::setTexture(1, s_metallicRoughnessMap, mr->Handle());
        bgfx::setTexture(2, s_normalMap, nrm->Handle());
        bgfx::setTexture(7, s_emissiveMap, emissive->Handle());
        bgfx::setTexture(8, s_occlusionMap, occlusion->Handle());
        bgfx::setTexture(9, s_opacityMap, opacity->Handle());
        bgfx::setTexture(10, s_metallicMap, metallic->Handle());
        bgfx::setTexture(11, s_roughnessMap, roughness->Handle());
        bgfx::setTexture(12, s_specularMap, specular->Handle());
        if (bgfx::isValid(m_brdfLUT)) {
            bgfx::setTexture(6, s_brdfLUT, m_brdfLUT);
        }

        bgfx::setVertexBuffer(0, mc.mesh->VB()); bgfx::setIndexBuffer(mc.mesh->IB());
        // Imported meshes are CCW-front. Single-sided materials cull their
        // backfaces; two-sided materials keep them and the fragment shaders
        // flip the normals for those fragments.
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                       FaceState(mat.twoSided));
        if (m_deferred && bgfx::isValid(m_gbufferProg)) {
            bgfx::submit(viewId, m_gbufferProg);
        } else {
            bgfx::submit(viewId, m_program);
        }
    }

    RenderVegetation(scene, frustum, viewId);
}

namespace {

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

uint32_t UploadInstances(const std::vector<glm::mat4>& instances,
                         bgfx::InstanceDataBuffer& idb) {
    if (instances.empty()) return 0;
    const uint32_t requested = static_cast<uint32_t>(
        std::min<size_t>(instances.size(), 0xFFFFFFFFu));
    const uint32_t available = bgfx::getAvailInstanceDataBuffer(requested, 64);
    if (available == 0) return 0;
    bgfx::allocInstanceDataBuffer(&idb, available, 64);
    float* data = reinterpret_cast<float*>(idb.data);
    for (uint32_t i = 0; i < available; ++i) {
        std::memcpy(data + static_cast<size_t>(i) * 16,
                    glm::value_ptr(instances[i]), sizeof(float) * 16);
    }
    return available;
}

} // namespace

void RenderSystem::RenderVegetation(Scene& scene, const Frustum& frustum,
                                    bgfx::ViewId viewId) {
    if (!m_assets) return;
    const bool useGbuffer = m_deferred && bgfx::isValid(m_instancedGbufferProg);
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

    const float metallic[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float emissive[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    const float flags[4] = {0.0f, 0.0f, 0.0f, 0.0f};

    auto submitGroup = [&](Mesh& mesh, const glm::vec4& color, float roughness,
                           bool twoSided, bgfx::InstanceDataBuffer& idb,
                           uint32_t count) {
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
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
                       BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA |
                       FaceState(twoSided));
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
            const uint32_t count = UploadInstances(vegetation.treeInstances, idb);
            if (count > 0) {
                submitGroup(*trunkMesh, vegetation.trunkColor, 0.92f, false, idb, count);
                submitGroup(*canopyMesh, vegetation.canopyColor, 0.82f, false, idb, count);
            }
        }

        if (!vegetation.grassInstances.empty()) {
            bgfx::InstanceDataBuffer idb;
            const uint32_t count = UploadInstances(vegetation.grassInstances, idb);
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
            const uint32_t count = UploadInstances(vegetation.treeInstances, idb);
            if (count > 0) {
                submitShadow(*trunkMesh, false, idb, count);
                submitShadow(*canopyMesh, false, idb, count);
            }
        }
        if (vegetation.grassCastShadows && !vegetation.grassInstances.empty()) {
            bgfx::InstanceDataBuffer idb;
            const uint32_t count = UploadInstances(vegetation.grassInstances, idb);
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

        glm::mat4 model(1.0f);
        entt::entity curr = h;
        std::vector<entt::entity> chain;
        int __depth = 0; while (curr != entt::null && __depth++ < 128) {
            chain.push_back(curr);
            if (scene.Registry().all_of<ParentComponent>(curr)) curr = scene.Registry().get<ParentComponent>(curr).parent;
            else break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it)) model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }

        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       FaceState(twoSided));
        bgfx::submit(viewId, m_shadowProg);
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

    static std::vector<glm::vec4> identityBoneData(kMaxBones * 4, glm::vec4(0.0f));
    static bool initIdentity = false;
    if (!initIdentity) {
        glm::mat4 id(1.0f);
        for (int i = 0; i < kMaxBones; ++i) {
            for (int j = 0; j < 4; ++j) identityBoneData[i * 4 + j] = id[j];
        }
        initIdentity = true;
    }

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

        glm::mat4 model(1.0f);
        entt::entity curr = h;
        std::vector<entt::entity> chain;
        while (curr != entt::null) {
            chain.push_back(curr);
            if (scene.Registry().all_of<ParentComponent>(curr)) curr = scene.Registry().get<ParentComponent>(curr).parent;
            else break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it)) model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }

        // Skeletal animation support
        entt::entity root = h;
        while (root != entt::null) {
            if (scene.Registry().all_of<ParentComponent>(root)) root = scene.Registry().get<ParentComponent>(root).parent;
            else break;
        }
        bool hasSkeleton = scene.Registry().all_of<SkeletalComponent>(root);
        if (hasSkeleton) {
            auto& skel = scene.Registry().get<SkeletalComponent>(root);
            if (skel.isAnimated && !skel.sourcePath.empty()) {
                if (skel.boneMatrices.size() != kMaxBones) skel.boneMatrices.resize(kMaxBones, glm::mat4(1.0f)); // Should be handled in OnUpdate
                // Animation updated in OnUpdate (Parallel)
                std::vector<glm::vec4> boneData(kMaxBones * 4);
                for (int i = 0; i < kMaxBones; ++i) {
                    for (int j = 0; j < 4; ++j) boneData[i * 4 + j] = skel.boneMatrices[i][j];
                }
                bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones, bgfx::copy(boneData.data(), kMaxBones * 4 * sizeof(glm::vec4)));
            } else {
                bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones, bgfx::copy(identityBoneData.data(), kMaxBones * 4 * sizeof(glm::vec4)));
            }
        } else {
            bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones, bgfx::copy(identityBoneData.data(), kMaxBones * 4 * sizeof(glm::vec4)));
        }
        bgfx::setTexture(3, s_boneMatrices, m_boneTexture);

        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS |
                       FaceState(twoSided));
        bgfx::submit(viewId, m_shadowPointProg);
    }

    RenderVegetationShadows(scene, m_instancedShadowPointProg, viewId);
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
    static std::vector<glm::vec4> identityBoneData(kMaxBones * 4, glm::vec4(0.0f));
    static bool initIdentity = false;
    if (!initIdentity) {
        glm::mat4 id(1.0f);
        for (int i = 0; i < kMaxBones; ++i) {
            for (int j = 0; j < 4; ++j) identityBoneData[i * 4 + j] = id[j];
        }
        initIdentity = true;
    }
    std::vector<glm::vec4> boneData(kMaxBones * 4);
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
        const std::vector<glm::vec4>* bones = &identityBoneData;
        entt::entity root = item.entity;
        while (root != entt::null) {
            if (scene.Registry().all_of<ParentComponent>(root))
                root = scene.Registry().get<ParentComponent>(root).parent;
            else
                break;
        }
        if (scene.Registry().all_of<SkeletalComponent>(root)) {
            auto& skel = scene.Registry().get<SkeletalComponent>(root);
            if (skel.isAnimated && !skel.sourcePath.empty()) {
                if (skel.boneMatrices.size() != kMaxBones)
                    skel.boneMatrices.resize(kMaxBones, glm::mat4(1.0f));
                for (int i = 0; i < kMaxBones; ++i) {
                    for (int j = 0; j < 4; ++j) boneData[i * 4 + j] = skel.boneMatrices[i][j];
                }
                bones = &boneData;
            }
        }
        bgfx::updateTexture2D(m_boneTexture, 0, 0, 0, 0, 4, kMaxBones,
            bgfx::copy(bones->data(), kMaxBones * 4 * sizeof(glm::vec4)));
        bgfx::setTexture(3, s_boneMatrices, m_boneTexture);

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
