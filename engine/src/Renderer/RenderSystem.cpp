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

RenderSystem::RenderSystem() = default;
RenderSystem::~RenderSystem() { Shutdown(); }

void RenderSystem::Init(AssetManager* assets) {
    m_assets  = assets;
    m_program = ShaderManager::LoadProgram("shaders/vs_pbr.bin",
                                            "shaders/fs_pbr.bin");
    if (!bgfx::isValid(m_program)) {
        spdlog::error("[RenderSystem] Failed to load PBR shaders");
        return;
    }

    u_albedoFactor         = bgfx::createUniform("u_albedoFactor",         bgfx::UniformType::Vec4);
    u_metallicFactor       = bgfx::createUniform("u_metallicFactor",       bgfx::UniformType::Vec4);
    u_roughnessFactor      = bgfx::createUniform("u_roughnessFactor",      bgfx::UniformType::Vec4);
    u_emissiveFactor       = bgfx::createUniform("u_emissiveFactor",       bgfx::UniformType::Vec4);
    u_cameraPosition       = bgfx::createUniform("u_cameraPosition",       bgfx::UniformType::Vec4);
    u_lightDirection       = bgfx::createUniform("u_lightDirection",       bgfx::UniformType::Vec4);
    u_lightColorIntensity  = bgfx::createUniform("u_lightColorIntensity",  bgfx::UniformType::Vec4);
    s_albedoMap            = bgfx::createUniform("s_albedoMap",            bgfx::UniformType::Sampler);
    s_metallicRoughnessMap = bgfx::createUniform("s_metallicRoughnessMap", bgfx::UniformType::Sampler);
    s_normalMap            = bgfx::createUniform("s_normalMap",            bgfx::UniformType::Sampler);
}

void RenderSystem::Shutdown() {
    if (bgfx::isValid(m_program)) bgfx::destroy(m_program);
    if (bgfx::isValid(u_albedoFactor))         bgfx::destroy(u_albedoFactor);
    if (bgfx::isValid(u_metallicFactor))       bgfx::destroy(u_metallicFactor);
    if (bgfx::isValid(u_roughnessFactor))      bgfx::destroy(u_roughnessFactor);
    if (bgfx::isValid(u_emissiveFactor))       bgfx::destroy(u_emissiveFactor);
    if (bgfx::isValid(u_cameraPosition))       bgfx::destroy(u_cameraPosition);
    if (bgfx::isValid(u_lightDirection))       bgfx::destroy(u_lightDirection);
    if (bgfx::isValid(u_lightColorIntensity))  bgfx::destroy(u_lightColorIntensity);
    if (bgfx::isValid(s_albedoMap))            bgfx::destroy(s_albedoMap);
    if (bgfx::isValid(s_metallicRoughnessMap)) bgfx::destroy(s_metallicRoughnessMap);
    if (bgfx::isValid(s_normalMap))            bgfx::destroy(s_normalMap);

    m_program = BGFX_INVALID_HANDLE;
}

void RenderSystem::ApplyEnvironment(const Camera& camera,
                                    const glm::vec3& lightDir,
                                    const glm::vec3& lightColor,
                                    float lightIntensity)
{
    const glm::vec3 p = camera.Position();
    const float camPos[4]  = {p.x, p.y, p.z, 1.0f};
    const float lDir[4]    = {lightDir.x, lightDir.y, lightDir.z, 0.0f};
    const float lColInt[4] = {lightColor.r, lightColor.g, lightColor.b, lightIntensity};

    bgfx::setUniform(u_cameraPosition,      camPos);
    bgfx::setUniform(u_lightDirection,      lDir);
    bgfx::setUniform(u_lightColorIntensity, lColInt);
}

void RenderSystem::Render(Scene& scene,
                          const Camera& camera,
                          bgfx::ViewId viewId,
                          uint16_t width,
                          uint16_t height)
{
    if (!bgfx::isValid(m_program)) return;

    // Настройка view
    const glm::mat4 vp = camera.ViewProj();
    bgfx::setViewTransform(viewId, glm::value_ptr(camera.View()),
                                    glm::value_ptr(camera.Projection()));
    (void)vp;
    bgfx::setViewRect(viewId, 0, 0, width, height);
    bgfx::setViewClear(viewId,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x232629ff, 1.0f, 0);

    // Находим directional light (первый в сцене)
    glm::vec3 lightDir{-0.4f, -0.8f, -0.5f};
    glm::vec3 lightCol{1.0f, 0.97f, 0.9f};
    float     lightInt = 1.2f;

    auto lview = scene.Registry().view<DirectionalLightComponent>();
    if (!lview.empty()) {
        auto h = *lview.begin();
        const auto& dl = lview.get<DirectionalLightComponent>(h);
        lightDir = dl.direction;
        lightCol = dl.color;
        lightInt = dl.intensity;
    }

    ApplyEnvironment(camera, lightDir, lightCol, lightInt);

    // Проходим по всем сущностям с mesh+material
    auto view = scene.Registry().view<TransformComponent,
                                       MeshComponent,
                                       MaterialComponent>();
    for (auto h : view) {
        const auto& tc = view.get<TransformComponent>(h);
        const auto& mc = view.get<MeshComponent>(h);
        const auto& matc = view.get<MaterialComponent>(h);

        if (!mc.mesh || !mc.mesh->Valid()) continue;
        if (!matc.material) continue;

        const Material& mat = *matc.material;

        bgfx::setTransform(glm::value_ptr(tc.Matrix()));

        // Материал
        const float alb[4] = {mat.albedoFactor.r, mat.albedoFactor.g,
                              mat.albedoFactor.b, mat.albedoFactor.a};
        const float met[4] = {mat.metallicFactor, 0.0f, 0.0f, 0.0f};
        const float rgh[4] = {mat.roughnessFactor, 0.0f, 0.0f, 0.0f};
        const float emi[4] = {mat.emissiveFactor.r, mat.emissiveFactor.g,
                              mat.emissiveFactor.b, 0.0f};

        bgfx::setUniform(u_albedoFactor,    alb);
        bgfx::setUniform(u_metallicFactor,  met);
        bgfx::setUniform(u_roughnessFactor, rgh);
        bgfx::setUniform(u_emissiveFactor,  emi);

        // Текстуры — с фоллбэками
        auto white  = m_assets->WhiteTexture();
        auto flatN  = m_assets->FlatNormalTexture();

        auto albedo = mat.albedoMap            ? mat.albedoMap            : white;
        auto mr     = mat.metallicRoughnessMap ? mat.metallicRoughnessMap : white;
        auto nrm    = mat.normalMap            ? mat.normalMap            : flatN;

        bgfx::setTexture(0, s_albedoMap,            albedo->Handle());
        bgfx::setTexture(1, s_metallicRoughnessMap, mr->Handle());
        bgfx::setTexture(2, s_normalMap,            nrm->Handle());

        // Геометрия
        bgfx::setVertexBuffer(0, mc.mesh->VB());
        bgfx::setIndexBuffer(mc.mesh->IB());

        bgfx::setState(
            BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A | BGFX_STATE_WRITE_Z |
            BGFX_STATE_DEPTH_TEST_LESS |
            BGFX_STATE_MSAA);

        bgfx::submit(viewId, m_program);
    }
}

} // namespace Galeshi
