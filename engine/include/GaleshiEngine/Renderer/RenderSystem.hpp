#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>

namespace Galeshi {
class Scene; class Camera; class AssetManager; class Frustum;
class RenderSystem {
public:
    RenderSystem(); ~RenderSystem();
    void Init(AssetManager* assets);
    void Shutdown();
    void Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    void RenderPointShadowMap(Scene& scene, const glm::mat4& lightMtx, const glm::vec3& lightPos, bgfx::ViewId viewId, bgfx::FrameBufferHandle shadowFbo);
    void RenderShadowMap(Scene& scene, const glm::mat4& lightMtx, bgfx::ViewId viewId, bgfx::FrameBufferHandle shadowFbo);
    void RenderGlass(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    void RenderWater(Scene& scene, const Camera& camera,
                     bgfx::TextureHandle sceneColor, bgfx::TextureHandle sceneDepth,
                     bgfx::TextureHandle envCube,
                     const float lightDir[4], const float lightColorIntensity[4],
                     bgfx::ViewId viewId, uint16_t width, uint16_t height,
                     float time, float nearZ, float farZ);
    
    void SetSkipGlass(bool skip) { m_skipGlass = skip; }
    bool DeferredEnabled() const { return m_deferred; }
    void SetDeferredEnabled(bool v) { m_deferred = v; }
    void SetBrdfLUT(bgfx::TextureHandle tex) { m_brdfLUT = tex; }

    static constexpr int kMaxPointLights = 4;
    static constexpr int kMaxBones = 256;
private:
    void RenderVegetation(Scene& scene, const Frustum& frustum, bgfx::ViewId viewId);
    void RenderVegetationShadows(Scene& scene, bgfx::ProgramHandle program,
                                 bgfx::ViewId viewId);

    AssetManager* m_assets = nullptr;
    bool m_deferred = false;
    bool m_skipGlass = false;
    bgfx::TextureHandle m_brdfLUT = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_brdfLUT = BGFX_INVALID_HANDLE;

    bgfx::ProgramHandle m_program      = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_gbufferProg  = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_shadowProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_shadowPointProg = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowLightPos = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_glassProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_waterProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_instancedProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_instancedGbufferProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_instancedShadowProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_instancedShadowPointProg = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_lightMtx = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_albedoFactor = BGFX_INVALID_HANDLE, u_metallicFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_roughnessFactor = BGFX_INVALID_HANDLE, u_emissiveFactor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraPosition = BGFX_INVALID_HANDLE, u_lightDirection = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightColorIntensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_materialParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_materialFlags = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_glassParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cameraForward = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterParams0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterParams1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterParams2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterParams3 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterFlow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterInvNearFar = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterShallow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterDeep = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterFoam = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_waterSceneColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_waterDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_waterEnvCube = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pointLightPosRadius = BGFX_INVALID_HANDLE, u_pointLightColorInt = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pointLightCount = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_boneMatrices = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_boneTexture = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_albedoMap = BGFX_INVALID_HANDLE, s_metallicRoughnessMap = BGFX_INVALID_HANDLE, s_normalMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_irradianceMap = BGFX_INVALID_HANDLE, s_envMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_emissiveMap = BGFX_INVALID_HANDLE, s_occlusionMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_opacityMap = BGFX_INVALID_HANDLE, s_metallicMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_roughnessMap = BGFX_INVALID_HANDLE, s_specularMap = BGFX_INVALID_HANDLE;
};
} // namespace Galeshi
