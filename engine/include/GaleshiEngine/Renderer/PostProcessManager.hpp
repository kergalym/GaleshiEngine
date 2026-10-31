#pragma once
#include <bgfx/bgfx.h>
#include <memory>
#include <glm/glm.hpp>
#include <cstdint>

namespace Galeshi {

// Mode-independent parameters passed to the AO evaluation pass. The editor
// fills the SSAO fields for mode 0 and the generic fields for HBAO/GTAO.
struct AmbientOcclusionParams {
    bool  enabled     = false;
    int   mode        = 0;       // 0 = SSAO, 1 = HBAO, 2 = GTAO
    float radius      = 80.0f;
    float bias        = 0.0333f;
    float thickness   = 1.0f;
    float maxDistance = 6.9f;
    float strength    = 1.1f;
    int   directions  = 8;
    int   steps       = 4;
    float resolutionScale = 0.25f; // AO source resolution relative to viewport
};

class PostProcessManager {
public:
    PostProcessManager();
    ~PostProcessManager();
    PostProcessManager(const PostProcessManager&) = delete;
    PostProcessManager& operator=(const PostProcessManager&) = delete;

    void Init();
    void Shutdown();

    bgfx::TextureHandle IrradianceMap() const { return m_irradianceMap; }
    bgfx::TextureHandle EnvMap() const { return m_envMap; }
    bgfx::TextureHandle BrdfLUT() const { return m_brdfLut; }
    bgfx::TextureHandle OutputTexture() const { return m_outputColor; }
    bgfx::TextureHandle PointShadowMap() const { return m_pointShadowMap; }
    bgfx::FrameBufferHandle PointShadowFbo(int face) const { return m_pointShadowFbos[face]; }
    bgfx::FrameBufferHandle SpotShadowFbo(int idx) const { return m_spotShadowFbos[idx]; }
    bgfx::FrameBufferHandle OutputFbo() const { return m_outputFbo; }
    bgfx::TextureHandle PingTexture() const { return m_pingColor; }
    bgfx::TextureHandle BlitTextureA() const { return m_blitTexA; }
    bgfx::TextureHandle BlitTextureB() const { return m_blitTexB; }
    bgfx::TextureHandle BlitDepth() const { return m_blitDepth; }
    void BlitTexture(bgfx::TextureHandle src, bgfx::TextureHandle dst, bgfx::ViewId viewId);
    void BlitDepth(bgfx::TextureHandle src, bgfx::ViewId viewId);
    bgfx::FrameBufferHandle PingFbo() const { return m_pingFbo; }
    void SwapFbos();

    bgfx::TextureHandle Render(bgfx::ViewId viewId, bgfx::TextureHandle inputColor, uint16_t width, uint16_t height);

    void RenderLightCulling(int pointLightCount, const float pointLightPosRadius[][4], const glm::mat4& viewProj, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderLighting(bgfx::TextureHandle albedoMr, bgfx::TextureHandle normalRough, bgfx::TextureHandle worldPos,
                                       bgfx::TextureHandle materialData, bgfx::TextureHandle materialFeatures,
                                       bgfx::TextureHandle shadowMap0, bgfx::TextureHandle shadowMap1, bgfx::TextureHandle shadowMap2,
                                       const glm::mat4& lightMtx0, const glm::mat4& lightMtx1, const glm::mat4& lightMtx2,
                                       const glm::mat4& invViewProj, 
                                       const float cascadeSplits[4],
                                       const float cascadeNormalOffsets[4],
                                       const float cascadeDepthBias[4],
                                       const float pssmParams[4],
                                       const float camPos[4], const float lightDir[4], const float lightColorIntensity[4],
                                       const float fogColorDensity[4], const float fogParams[4],
                                       const float fogParams2[4],
                                       const float pointLightPos[][4], const float pointLightColor[][4], int pointLightCount,
                                       const float spotLightPosRadius[][4], const float spotLightDirInner[][4], const float spotLightColorOuter[][4], int spotLightCount,
                                       bgfx::ViewId viewId, uint16_t width, uint16_t height);

    void RenderAmbientOcclusion(bgfx::TextureHandle worldPos, bgfx::TextureHandle normal,
                                const glm::mat4& view, const glm::mat4& proj,
                                bgfx::ViewId viewId, uint16_t width, uint16_t height,
                                const AmbientOcclusionParams& params);
    bgfx::TextureHandle AOTexture() const { return m_aoValid ? m_aoFullTexA : m_whiteTex2D; }
    bgfx::TextureHandle RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::FrameBufferHandle targetFbo, const glm::mat4& proj, const glm::mat4& invProj, const float camPos[4], bgfx::ViewId viewId, uint16_t width, uint16_t height, float intensity);
    void SetBloomEnabled(bool v) { m_bloomEnabled = v; }
    bool BloomEnabled() const { return m_bloomEnabled; }
    bgfx::TextureHandle RenderBloom(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float threshold, float intensity, float radius);

    bgfx::TextureHandle RenderColorCorrection(
        bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo,
        bgfx::ViewId viewId, uint16_t width, uint16_t height,
        float exposure, float contrast, float saturation,
        float paletteAmount, const glm::vec3& paletteShadow,
        const glm::vec3& paletteMid, const glm::vec3& paletteHighlight);
    bgfx::TextureHandle RenderVignette(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height, float intensity, float smoothness);
    bgfx::TextureHandle RenderChromatic(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height, float intensity);
    bgfx::TextureHandle RenderTonemap(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float exposure, float contrast, float saturation);
    bgfx::TextureHandle RenderFxaa(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderInvert(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height);

    bool InvertEnabled() const { return m_invertEnabled; }
    void SetInvertEnabled(bool v) { m_invertEnabled = v; }
    bool FxaaEnabled() const { return m_fxaaEnabled; }
    void SetFxaaEnabled(bool v) { m_fxaaEnabled = v; }
    bool SsaoEnabled() const { return m_ssaoEnabled; }
    void SetSsaoEnabled(bool v) { m_ssaoEnabled = v; }
    bool SsrEnabled() const { return m_ssrEnabled; }
    void SetTaaEnabled(bool v) { m_taaEnabled = v; }
    bool TaaEnabled() const { return m_taaEnabled; }
    void ResetTaaHistory() { m_historyValid = false; }
    bgfx::TextureHandle RenderTaa(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                  bgfx::TextureHandle motionVector,
                                  const glm::mat4& prevVP, const glm::mat4& invVP,
                                  bgfx::FrameBufferHandle target, bgfx::ViewId viewId,
                                  uint16_t w, uint16_t h, float blend);
    bgfx::TextureHandle RenderGodRays(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                      const glm::mat4& viewProj, const float camPos[4], const float sunDir[4],
                                      bgfx::FrameBufferHandle target,
                                      bgfx::ViewId viewId, uint16_t w, uint16_t h,
                                      float intensity, float decay, float density, float weight);
    void SetGodRaysEnabled(bool v) { m_godraysEnabled = v; }
    bool GodRaysEnabled() const { return m_godraysEnabled; }
    bgfx::TextureHandle RenderAtmosphere(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                           const glm::mat4& invVP, const float camPos[4], const float sunDir[4],
                                           bgfx::FrameBufferHandle target,
                                           bgfx::ViewId viewId, uint16_t w, uint16_t h, float intensity);
    void SetAtmosphereEnabled(bool v) { m_atmosphereEnabled = v; }
    bool AtmosphereEnabled() const { return m_atmosphereEnabled; }
    void SetFogEnabled(bool v) { m_fogEnabled = v; }
    bool FogEnabled() const { return m_fogEnabled; }
    void SetFogVolumetric(bool v) { m_fogVolumetric = v; }
    bool FogVolumetric() const { return m_fogVolumetric; }
    bgfx::TextureHandle RenderMotionBlur(bgfx::TextureHandle color,
                                              bgfx::TextureHandle worldPos,
                                              bgfx::TextureHandle motionVector,
                                              const glm::mat4& prevVP, const glm::mat4& invVP,
                                              bgfx::FrameBufferHandle target,
                                              bgfx::ViewId velocityViewId, bgfx::ViewId blurViewId,
                                              uint16_t w, uint16_t h, float frameDelta,
                                              float amount, float maxVelocityPx);
    void SetMotionBlurEnabled(bool v) { m_motionBlurEnabled = v; }
    bool MotionBlurEnabled() const { return m_motionBlurEnabled; }
    bgfx::TextureHandle RenderDof(bgfx::TextureHandle color, bgfx::TextureHandle depth,
                                  const glm::mat4& proj, const glm::mat4& invProj,
                                  bgfx::FrameBufferHandle target,
                                  bgfx::ViewId viewId, uint16_t w, uint16_t h,
                                  float focusDistance, float blurAmount);
    void SetDofEnabled(bool v) { m_dofEnabled = v; }
    bool DofEnabled() const { return m_dofEnabled; }
    void GenerateIBL(const float sunDir[4]);

    bgfx::TextureHandle RenderSky(const glm::mat4& invProj, const glm::mat4& invView, const float sunDir[4], bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h);
    void SetSmaaEnabled(bool v) { m_smaaEnabled = v; }
    bool SmaaEnabled() const { return m_smaaEnabled; }
    bgfx::TextureHandle RenderSmaa(bgfx::TextureHandle color, bgfx::ViewId viewId, uint16_t w, uint16_t h);
    void SetSsrEnabled(bool v) { m_ssrEnabled = v; }

    bgfx::UniformHandle u_spotLightVP0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_spotLightVP1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_spotLightVP2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_spotLightVP3 = BGFX_INVALID_HANDLE;
    glm::mat4 GetPrevViewProj() const { return m_prevViewProj; }
    bool HasPrevViewProj() const { return m_prevViewProjValid; }
    void SetPrevViewProj(const glm::mat4& m) { m_prevViewProj = m; m_prevViewProjValid = true; }

bgfx::TextureHandle EnvScratch() const { return m_envScratch; }
    bgfx::FrameBufferHandle EnvScratchFbo() const { return m_envScratchFbo; }

    bgfx::TextureHandle EnvCubeMap() const { return m_envCubeMap; }
    bgfx::TextureHandle BlackCube() const { return m_blackCube; }
    bgfx::FrameBufferHandle DofCocFbo() const { return m_dofCocFbo; }
    bgfx::FrameBufferHandle DofBlurFboA() const { return m_dofBlurFboA; }
    bgfx::FrameBufferHandle DofBlurFboB() const { return m_dofBlurFboB; }
private:
    // --- Tiled Light Culling ---
    bgfx::ProgramHandle m_lightCullingProg = BGFX_INVALID_HANDLE;
    // Counts occupy the first totalTiles words; indices follow at
    // totalTiles + tileIdx * 64. Keeping both in one SSBO leaves a descriptor
    // slot free for the subsurface material-feature texture.
    bgfx::DynamicIndexBufferHandle m_tileLightBuffer = BGFX_INVALID_HANDLE;
    uint32_t m_numCullTiles = 0;
    bgfx::UniformHandle u_cullParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cullViewProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cullPointLightPosRadius = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cullPointLightCount = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_tileParams = BGFX_INVALID_HANDLE;

    void EnsureOutputFbo(uint16_t w, uint16_t h);
    void EnsureAOFbo(uint16_t w, uint16_t h, float resolutionScale);

    bgfx::ProgramHandle m_program     = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_invertProg  = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_fxaaProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_lightingProg= BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoSampleProg   = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_hbaoProg       = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_gtaoProg       = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoUpsampleProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoDetailProg   = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_aoBlurProg     = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_ssrProg     = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle s_texColor    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_texDepth    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texelSize   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_camPos      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightCol    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogColorDensity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogParams   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_fogParams2  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_albedoMr    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_normalRough = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_worldPos    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_materialTex = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_materialFeatures = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap0  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap1  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap2  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightVP0    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightVP1    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightVP2    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_plPos       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_plCol       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_plCnt       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slPos       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slDir       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slCol       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slCnt       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_pointShadowMap = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pointLightFarPlane = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_pointShadowMap = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_pointShadowDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_spotShadowMaps[4] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_spotShadowFbos[4] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::UniformHandle s_spotShadowMap0 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_spotShadowMap1 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_spotShadowMap2 = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_spotShadowMap3 = BGFX_INVALID_HANDLE;
    
    bgfx::FrameBufferHandle m_pointShadowFbos[6] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };

    bgfx::UniformHandle u_cascadeSplits = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cascadeNormalOffsets = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_cascadeDepthBias = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_pssmParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoView       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoProj       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoParams2    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoViewport   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoSource     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoBlurParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_aoBlurDir    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_aoSource     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_aoTex        = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_viewportSize = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_ssrInvProj  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ssrProj     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ssrParams   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssrNormal   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_smaaEdges   = BGFX_INVALID_HANDLE;

    bgfx::VertexBufferHandle m_vbh    = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ibh    = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle     m_outputColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_outputDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_outputFbo   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_pingColor   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_blitTexA    = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_blitTexB    = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_blitDepth   = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_pingFbo     = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_blitFboA    = BGFX_INVALID_HANDLE;

    // --- Ambient occlusion chain (reduced res -> full res) ---
    bgfx::TextureHandle     m_aoHalfTex   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_aoFullTexA  = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_aoFullTexB  = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoHalfFbo   = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoFullFboA  = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_aoFullFboB  = BGFX_INVALID_HANDLE;
    uint16_t m_aoWidth = 0, m_aoHeight = 0;
    float    m_aoResolutionScale = 0.0f;
    bool     m_aoValid = false;
    bool m_historyValid = false;

    uint16_t m_width = 0, m_height = 0;
    bool m_invertEnabled = false;
    bool m_fxaaEnabled   = false;
    bool m_ssaoEnabled   = false;
    bool m_ssrEnabled    = false;
    bool m_bloomEnabled = true;
    bool m_taaEnabled    = false;
    bgfx::ProgramHandle m_taaProg;
    bgfx::ProgramHandle m_bloomExtractProg;
    bgfx::ProgramHandle m_bloomBlurProg;
    bgfx::ProgramHandle m_bloomCombineProg;
    bgfx::ProgramHandle m_tonemapProg;
    bgfx::ProgramHandle m_colorCorrectionProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_vignetteProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_chromaticProg = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomThreshold;
    bgfx::UniformHandle u_bloomTexelSize = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomDir       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomIntensity;
    bgfx::UniformHandle s_bloomTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_bloomTexA = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_bloomTexB = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_bloomFboA = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_bloomFboB = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_colorParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_colorCorrectionParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envPaletteShadow = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envPaletteMid = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_envPaletteHighlight = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_vignetteParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_chromaParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaPrevVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaInvVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaTexelSize = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_taaHistory = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_taaMotion = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_historyColor = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_historyFbo = BGFX_INVALID_HANDLE;
    glm::mat4 m_prevViewProj{1.0f};
    bool m_prevViewProjValid = false;
    bgfx::ProgramHandle m_skyProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_iblGenProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_irradianceConvProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_prefilterProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_brdfProg = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_envMap = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_envCubeMap = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_envScratch = BGFX_INVALID_HANDLE; // 256x1536 atlas: 6 horizontal slices
    bgfx::FrameBufferHandle m_envScratchFbo = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_irradianceCube = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_prefilteredCube = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_irradianceScratch  = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_prefilterScratch   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_irradianceMap = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_brdfLut = BGFX_INVALID_HANDLE;
    bool m_brdfLutGenerated = false;
    bool m_iblValid = false;
    bgfx::TextureHandle m_blackCube = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_blackTex2D = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_whiteTex2D = BGFX_INVALID_HANDLE;

    // --- Depth of Field ---
    bgfx::TextureHandle m_dofCocTex    = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_dofBlurTexA  = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_dofBlurTexB  = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_dofCocFbo   = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_dofBlurFboA = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_dofBlurFboB = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_dofCocProg       = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_motionBlurProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_mbVelocityProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_atmosphereProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_godraysProg = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_sunScreenPos = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_godraysParams = BGFX_INVALID_HANDLE;
    bool m_godraysEnabled = false;
    bgfx::UniformHandle u_atmoInvVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_atmoSunDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_atmoParams = BGFX_INVALID_HANDLE;
    bool m_atmosphereEnabled = false;
    bool m_fogEnabled = false;
    bool m_fogVolumetric = false;
    bgfx::UniformHandle u_motionBlurParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_mbVelocityParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_mbPrevVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_mbInvVP  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_mbVelocity = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_mbObjectMotion = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_mbVelocityTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_mbVelocityFbo = BGFX_INVALID_HANDLE;
    bool m_motionBlurEnabled = true;
    bgfx::ProgramHandle m_dofBlurProg      = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_dofCompositeProg = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_dofParams    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_dofTexelSize = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_dofDir       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_dofColor     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_dofDepth     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_dofBlur      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_dofCoc       = BGFX_INVALID_HANDLE;
    bool m_dofEnabled = false;
    std::shared_ptr<class Texture> m_brdfTexture;
    bgfx::FrameBufferHandle m_envFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_irradianceFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_brdfFbo = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_iblParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_faceParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_prefilterParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_envCube = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_iblLightDir = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_iblIrradiance = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_iblEnv = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_iblBrdf = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_skyInvVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_skySun = BGFX_INVALID_HANDLE;
    bool m_smaaEnabled   = false;
    bgfx::ProgramHandle m_smaaEdgeProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_smaaBlendProg = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_smaaEdgeTex = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_smaaColorTex = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_smaaColorFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_smaaEdgeFbo = BGFX_INVALID_HANDLE;

    bgfx::FrameBufferHandle m_envCubeFbos[6] = { BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE, BGFX_INVALID_HANDLE };
    bgfx::FrameBufferHandle m_irradianceScratchFbo = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_prefilterScratchFbo  = BGFX_INVALID_HANDLE;
};

} // namespace Galeshi
