#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace Galeshi {

class PostProcessManager {
public:
    PostProcessManager();
    ~PostProcessManager();
    PostProcessManager(const PostProcessManager&) = delete;
    PostProcessManager& operator=(const PostProcessManager&) = delete;

    void Init();
    void Shutdown();

    bgfx::TextureHandle OutputTexture() const { return m_outputColor; }
    bgfx::FrameBufferHandle OutputFbo() const { return m_outputFbo; }
    bgfx::TextureHandle PingTexture() const { return m_pingColor; }
    bgfx::TextureHandle BlitTextureA() const { return m_blitTexA; }
    bgfx::TextureHandle BlitTextureB() const { return m_blitTexB; }
    bgfx::TextureHandle BlitDepth() const { return m_blitDepth; }
    void BlitTexture(bgfx::TextureHandle src, bgfx::TextureHandle dst, bgfx::ViewId viewId);
    bgfx::FrameBufferHandle PingFbo() const { return m_pingFbo; }
    void SwapFbos();

    bgfx::TextureHandle Render(bgfx::ViewId viewId, bgfx::TextureHandle inputColor, uint16_t width, uint16_t height);

    bgfx::TextureHandle RenderLighting(bgfx::TextureHandle albedoMr, bgfx::TextureHandle normalRough, bgfx::TextureHandle worldPos,
                                       bgfx::TextureHandle shadowMap0, bgfx::TextureHandle shadowMap1, bgfx::TextureHandle shadowMap2,
                                       const glm::mat4& lightMtx0, const glm::mat4& lightMtx1, const glm::mat4& lightMtx2,
                                       const glm::mat4& invViewProj, 
                                       const float camPos[4], const float lightDir[4], const float lightColorIntensity[4],
                                       const float pointLightPos[][4], const float pointLightColor[][4], int pointLightCount,
                                       const float spotLightPosRadius[][4], const float spotLightDirInner[][4], const float spotLightColorOuter[][4], int spotLightCount,
                                       bgfx::ViewId viewId, uint16_t width, uint16_t height);

    bgfx::TextureHandle RenderSSAO(bgfx::TextureHandle color, bgfx::TextureHandle depth, bgfx::TextureHandle normal, bgfx::FrameBufferHandle targetFbo, const glm::mat4& proj, const glm::mat4& invProj, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::FrameBufferHandle targetFbo, const glm::mat4& invProj, const float camPos[4], bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderBloom(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h, float threshold, float intensity);
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
    bgfx::TextureHandle RenderTaa(bgfx::TextureHandle color, bgfx::TextureHandle depth, const glm::mat4& prevVP, const glm::mat4& invVP, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h);
    bgfx::TextureHandle RenderSky(const glm::mat4& invProj, const glm::mat4& invView, const float sunDir[4], bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h);
    void SetSmaaEnabled(bool v) { m_smaaEnabled = v; }
    bool SmaaEnabled() const { return m_smaaEnabled; }
    bgfx::TextureHandle RenderSmaa(bgfx::TextureHandle color, bgfx::FrameBufferHandle target, bgfx::ViewId viewId, uint16_t w, uint16_t h);
    void SetSsrEnabled(bool v) { m_ssrEnabled = v; }

    glm::mat4 GetPrevViewProj() const { return m_prevViewProj; }
    void SetPrevViewProj(const glm::mat4& m) { m_prevViewProj = m; }

private:
    void EnsureOutputFbo(uint16_t w, uint16_t h);

    bgfx::ProgramHandle m_program     = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_invertProg  = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_fxaaProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_lightingProg= BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_ssaoProg    = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_ssrProg     = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle s_texColor    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_texelSize   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_camPos      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightDir    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightCol    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_albedoMr    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_normalRough = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_worldPos    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap0  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap1  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_shadowMap2  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_lightVP     = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_plPos       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_plCol       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_plCnt       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slPos       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slDir       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slCol       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_slCnt       = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_ssaoProj    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ssaoInvProj = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_ssaoParams  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssaoColor   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssaoDepth   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssaoNormal  = BGFX_INVALID_HANDLE;

    bgfx::UniformHandle u_ssrInvProj  = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssrColor    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssrNormal   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_ssrDepth    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_smaaEdges   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_taaColor    = BGFX_INVALID_HANDLE;

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
    bool m_historyValid = false;

    uint16_t m_width = 0, m_height = 0;
    bool m_invertEnabled = false;
    bool m_fxaaEnabled   = false;
    bool m_ssaoEnabled   = false;
    bool m_ssrEnabled    = false;
    bool m_taaEnabled    = false;
    bgfx::ProgramHandle m_taaProg;
    bgfx::ProgramHandle m_bloomExtractProg;
    bgfx::ProgramHandle m_bloomBlurProg;
    bgfx::ProgramHandle m_bloomCombineProg;
    bgfx::ProgramHandle m_tonemapProg;
    bgfx::UniformHandle u_bloomThreshold;
    bgfx::UniformHandle u_bloomTexelSize = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomDir       = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_bloomIntensity;
    bgfx::UniformHandle u_colorParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaPrevVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaInvVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_taaParams = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_taaHistory = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle s_taaDepth = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_historyColor = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_historyFbo = BGFX_INVALID_HANDLE;
    glm::mat4 m_prevViewProj{1.0f};
    bgfx::ProgramHandle m_skyProg = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_skyInvVP = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle u_skySun = BGFX_INVALID_HANDLE;
    bool m_smaaEnabled   = false;
    bgfx::ProgramHandle m_smaaEdgeProg = BGFX_INVALID_HANDLE;
    bgfx::ProgramHandle m_smaaBlendProg = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle m_smaaEdgeTex = BGFX_INVALID_HANDLE;
};

} // namespace Galeshi
