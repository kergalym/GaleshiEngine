#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <unordered_map>

namespace Galeshi {
class Scene; class Camera; class AssetManager; class Frustum; class Material;
class RenderSystem {
public:
    RenderSystem(); ~RenderSystem();
    void Init(AssetManager* assets);
    void Shutdown();
    void Render(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    // Renders a small depth-only occluder map and updates the previous-frame
    // occlusion queries. Must be submitted before the main scene view.
    void RenderOcclusion(Scene& scene, const Camera& camera,
                         bgfx::ViewId viewId, uint16_t width, uint16_t height);
    void RenderPointShadowMap(Scene& scene, const glm::mat4& lightMtx, const glm::vec3& lightPos, bgfx::ViewId viewId, bgfx::FrameBufferHandle shadowFbo);
    void RenderShadowMap(Scene& scene, const glm::mat4& lightMtx, bgfx::ViewId viewId, bgfx::FrameBufferHandle shadowFbo);
    void RenderGlass(Scene& scene, const Camera& camera, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    // Alpha-blended, lit forward pass for materials with translucencyEnabled.
    // sceneDepth is the copied opaque depth used for the manual depth test;
    // the PSSM shadow depth maps provide both surface shadowing and the
    // light transmission depth used by the backlight lobe.
    void RenderTranslucency(Scene& scene, const Camera& camera,
                            bgfx::TextureHandle sceneDepth,
                            bgfx::TextureHandle envCube,
                            bgfx::TextureHandle shadowMap0,
                            bgfx::TextureHandle shadowMap1,
                            bgfx::TextureHandle shadowMap2,
                            const glm::mat4& lightMtx0,
                            const glm::mat4& lightMtx1,
                            const glm::mat4& lightMtx2,
                            const float cascadeSplits[4],
                            const float pssmParams[4],
                            const float lightDir[4],
                            const float lightColorIntensity[4],
                            bgfx::ViewId viewId, uint16_t width, uint16_t height);
    void RenderWater(Scene& scene, const Camera& camera,
                     bgfx::TextureHandle sceneColor, bgfx::TextureHandle sceneDepth,
                     bgfx::TextureHandle envCube,
                     bgfx::TextureHandle planarReflection, float planarPlaneY,
                     const float lightDir[4], const float lightColorIntensity[4],
                     bgfx::ViewId viewId, uint16_t width, uint16_t height,
                     float time, float nearZ, float farZ);

    // Renders the opaque scene mirrored across the nearest visible water
    // plane. The resulting HDR texture is sampled by RenderWater and falls
    // back to the environment cubemap where no reflected geometry exists.
    bool RenderPlanarReflection(Scene& scene, const Camera& camera,
                                bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle PlanarReflectionTexture() const {
        if (!m_planarActive) return bgfx::TextureHandle(BGFX_INVALID_HANDLE);
        return m_reflectionColor;
    }
    float PlanarReflectionPlaneY() const { return m_planarPlaneY; }
    bool HasPlanarReflection() const { return m_planarActive; }
    void RenderHair(Scene& scene, const Camera& camera,
                    bgfx::TextureHandle sceneDepth, bgfx::TextureHandle envCube,
                    const float sunDir[4], const float lightColorIntensity[4],
                    bgfx::ViewId viewId, uint16_t width, uint16_t height,
                    float nearZ, float farZ);

    // Previous unjittered camera view-projection. Feed this from the same
    // history matrix used by TAA so the G-buffer velocity and reprojection
    // agree.
    void SetPreviousViewProj(const glm::mat4& prev) {
        m_prevViewProj = prev;
        m_hasPrevViewProj = true;
    }

    void SetOcclusionCullingEnabled(bool enabled);
    bool OcclusionCullingEnabled() const { return m_occlusionEnabled; }
    uint32_t OccludedObjectCount() const { return m_occludedObjectCount; }

    void SetLodEnabled(bool enabled) { m_lodEnabled = enabled; }
    bool LodEnabled() const { return m_lodEnabled; }
    void SetLodBias(float bias);
    float LodBias() const { return m_lodBias; }
    
    void SetSkipGlass(bool skip) { m_skipGlass = skip; }
    bool DeferredEnabled() const { return m_deferred; }
    void SetDeferredEnabled(bool v) { m_deferred = v; }
    void SetBrdfLUT(bgfx::TextureHandle tex) { m_brdfLUT = tex; }

    static constexpr int kMaxPointLights = 4;
    static constexpr int kMaxBones = 256;
private:
    struct OcclusionState {
        bgfx::OcclusionQueryHandle query = BGFX_INVALID_HANDLE;
        bool visible = true;
        uint64_t lastTestedFrame = 0;
        uint64_t lastSeenFrame = 0;
    };

    void RenderVegetation(Scene& scene, const Frustum& frustum, bgfx::ViewId viewId,
                          bool useDeferred = true, bool reflectionView = false);
    void RenderVegetationShadows(Scene& scene, bgfx::ProgramHandle program,
                                 bgfx::ViewId viewId);
    void BindMaterial(const Material& mat);
    bool EnsureReflectionTarget(uint16_t width, uint16_t height);
    void DestroyReflectionTarget();
    bool EnsureOcclusionTarget(uint16_t width, uint16_t height);
    bool IsOccluded(uint32_t entityId) const;
    void ResetOcclusionState();

    AssetManager* m_assets = nullptr;
    bool m_deferred = false;
    bool m_skipGlass = false;
    bool m_lodEnabled = true;
    float m_lodBias = 1.0f;
    bool m_occlusionEnabled = true;
    bool m_occlusionSupported = false;
    uint32_t m_occludedObjectCount = 0;
    uint64_t m_occlusionFrame = 0;
    glm::vec3 m_lastOcclusionCameraPos{0.0f};
    bool m_hasLastOcclusionCamera = false;
    bgfx::TextureHandle m_occlusionColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_occlusionDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_occlusionFbo = BGFX_INVALID_HANDLE;
    uint16_t m_occlusionWidth = 0;
    uint16_t m_occlusionHeight = 0;
    std::unordered_map<uint32_t, OcclusionState> m_occlusionStates;
    bgfx::TextureHandle m_brdfLUT = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_brdfLUT = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle m_reflectionColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_reflectionDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_reflectionFbo = BGFX_INVALID_HANDLE;
    uint16_t m_reflectionWidth = 0;
    uint16_t m_reflectionHeight = 0;
    float m_planarPlaneY = 0.0f;
    bool m_planarActive = false;
    glm::mat4 m_planarLastView{1.0f};
    glm::mat4 m_planarLastProj{1.0f};
    bool m_planarHasLastView = false;
    uint32_t m_planarCadence = 0;

    bgfx::ProgramHandle m_program      = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_gbufferProg  = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_shadowProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_shadowSkinnedProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_shadowPointProg = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowLightPos = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_prevViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_prevModel = BGFX_INVALID_HANDLE;
    glm::mat4 m_prevViewProj{1.0f};
    bool m_hasPrevViewProj = false;
    std::unordered_map<uint32_t, glm::mat4> m_prevModels;
    bgfx::ProgramHandle m_glassProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_translucentProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_waterProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_hairProg     = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle u_subsurfaceParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_shadowDepthOffset = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_translucencyParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_translucencyColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_translucencyShadow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cascadeSplits = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pssmParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightViewProj0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightViewProj1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightViewProj2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_translucentDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_translucentShadow0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_translucentShadow1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_translucentShadow2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_translucentEnvCube = BGFX_INVALID_HANDLE;
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
    bgfx::UniformHandle s_waterPlanar = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_waterPlanar = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_reflectionClip = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_hairParams0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_hairParams1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_hairInvNearFar = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_hairRootColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_hairTipColor = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_hairDepth = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_hairEnvCube = BGFX_INVALID_HANDLE;
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
