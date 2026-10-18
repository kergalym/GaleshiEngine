#include "GaleshiEngine/Renderer/RenderSystem.hpp"
#include "GaleshiEngine/Renderer/ShaderManager.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/Texture.hpp"
#include <glm/gtc/type_ptr.hpp>
#include <spdlog/spdlog.h>

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
}

RenderSystem::RenderSystem() = default;
RenderSystem::~RenderSystem() { Shutdown(); }

void RenderSystem::Init(AssetManager* assets) {
    m_assets = assets;
    m_program = ShaderManager::LoadProgram("shaders/vs_pbr.bin", "shaders/fs_pbr.bin");
    m_shadowProg = ShaderManager::LoadProgram("shaders/vs_shadow.bin", "shaders/fs_shadow.bin");
    if (!bgfx::isValid(m_program)) return;

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
    
    s_boneMatrices = bgfx::createUniform("s_boneMatrices", bgfx::UniformType::Sampler);
    m_boneTexture = bgfx::createTexture2D(4, kMaxBones, false, 1, bgfx::TextureFormat::RGBA32F);

    s_albedoMap = bgfx::createUniform("s_albedoMap", bgfx::UniformType::Sampler);
    s_metallicRoughnessMap = bgfx::createUniform("s_metallicRoughnessMap", bgfx::UniformType::Sampler);
    s_normalMap = bgfx::createUniform("s_normalMap", bgfx::UniformType::Sampler);
}

void RenderSystem::Shutdown() {
    if (bgfx::isValid(m_program)) bgfx::destroy(m_program);
    if (bgfx::isValid(m_shadowProg)) bgfx::destroy(m_shadowProg);
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
    if (bgfx::isValid(s_boneMatrices)) bgfx::destroy(s_boneMatrices);
    if (bgfx::isValid(m_boneTexture)) bgfx::destroy(m_boneTexture);
    if (bgfx::isValid(s_albedoMap)) bgfx::destroy(s_albedoMap);
    if (bgfx::isValid(s_metallicRoughnessMap)) bgfx::destroy(s_metallicRoughnessMap);
    if (bgfx::isValid(s_normalMap)) bgfx::destroy(s_normalMap);
    m_program = BGFX_INVALID_HANDLE;
}

void RenderSystem::Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_program)) return;

    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()), glm::value_ptr(camera.Projection()));
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, 0x232629ff, 1.0f, 0);

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

        glm::mat4 model(1.0f);
        entt::entity curr = h;
        std::vector<entt::entity> chain;
        while (curr != entt::null) {
            chain.push_back(curr);
            if (scene.Registry().all_of<ParentComponent>(curr))
                curr = scene.Registry().get<ParentComponent>(curr).parent;
            else break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it))
                model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
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
                auto modelData = m_assets->LoadModel(skel.sourcePath);
                m_assets->UpdateAnimation(modelData, skel.currentTime, skel.boneMatrices);
                
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
        const Material& mat = *matc.material;
        const float alb[4] = {mat.albedoFactor.r, mat.albedoFactor.g, mat.albedoFactor.b, mat.albedoFactor.a};
        const float met[4] = {mat.metallicFactor, 0, 0, 0}, rgh[4] = {mat.roughnessFactor, 0, 0, 0}, emi[4] = {mat.emissiveFactor.r, mat.emissiveFactor.g, mat.emissiveFactor.b, 0};
        bgfx::setUniform(u_albedoFactor, alb); bgfx::setUniform(u_metallicFactor, met);
        bgfx::setUniform(u_roughnessFactor, rgh); bgfx::setUniform(u_emissiveFactor, emi);

        auto white = m_assets->WhiteTexture(), flatN = m_assets->FlatNormalTexture();
        auto albedo = mat.albedoMap ? mat.albedoMap : white, mr = mat.metallicRoughnessMap ? mat.metallicRoughnessMap : white;
        auto nrm = mat.normalMap ? mat.normalMap : flatN;
        bgfx::setTexture(0, s_albedoMap, albedo->Handle()); bgfx::setTexture(1, s_metallicRoughnessMap, mr->Handle()); bgfx::setTexture(2, s_normalMap, nrm->Handle());

        bgfx::setVertexBuffer(0, mc.mesh->VB()); bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_MSAA);
        bgfx::submit(viewId, m_program);
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

        bgfx::setTransform(glm::value_ptr(model));
        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_Z | BGFX_STATE_DEPTH_TEST_LESS);
        bgfx::submit(viewId, m_shadowProg);
    }
}

void RenderSystem::RenderGlass(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height) {
    if (!bgfx::isValid(m_glassProg)) return;
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()), glm::value_ptr(camera.Projection()));
    bgfx::setViewRect(viewId, 0, 0, width, height);

    auto view = scene.Registry().view<TransformComponent, MeshComponent, MaterialComponent>();
    for (auto h : view) {
        const auto& mc = view.get<MeshComponent>(h);
        const auto& matc = view.get<MaterialComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid() || !matc.material || !matc.material->isGlass) continue;

        glm::mat4 model(1.0f);
        entt::entity curr = h;
        std::vector<entt::entity> chain;
        while (curr != entt::null) {
            chain.push_back(curr);
            if (scene.Registry().all_of<ParentComponent>(curr))
                curr = scene.Registry().get<ParentComponent>(curr).parent;
            else break;
        }
        for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
            if (scene.Registry().all_of<TransformComponent>(*it))
                model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }

        bgfx::setTransform(glm::value_ptr(model));
        
        glm::vec3 cp = camera.Position();
        float camPos[4] = {cp.x, cp.y, cp.z, 1.0f};
        bgfx::setUniform(u_cameraPosition, camPos);

        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_DEPTH_TEST_LESS | BGFX_STATE_BLEND_ALPHA);
        bgfx::submit(viewId, m_glassProg);
    }
}
} // namespace Galeshi
