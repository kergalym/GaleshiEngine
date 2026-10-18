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
                                       bgfx::TextureHandle shadowMap, const glm::mat4& lightMtx, 
                                       const float camPos[4], const float lightDir[4], const float lightColorIntensity[4],
                                       const float pointLightPos[][4], const float pointLightColor[][4], int pointLightCount,
                                       const float spotLightPosRadius[][4], const float spotLightDirInner[][4], const float spotLightColorOuter[][4], int spotLightCount,
                                       bgfx::ViewId viewId, uint16_t width, uint16_t height);

    bgfx::TextureHandle RenderSSAO(bgfx::TextureHandle color, bgfx::TextureHandle depth, bgfx::TextureHandle normal, bgfx::FrameBufferHandle targetFbo, const glm::mat4& proj, const glm::mat4& invProj, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::FrameBufferHandle targetFbo, const glm::mat4& invProj, const float camPos[4], bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderFxaa(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height);
    bgfx::TextureHandle RenderInvert(bgfx::TextureHandle color, bgfx::FrameBufferHandle targetFbo, bgfx::ViewId viewId, uint16_t width, uint16_t height);

    bool InvertEnabled() const { return m_invertEnabled; }
    void SetInvertEnabled(bool v) { m_invertEnabled = v; }
    bool FxaaEnabled() const { return m_fxaaEnabled; }
    void SetFxaaEnabled(bool v) { m_fxaaEnabled = v; }
    bool SsaoEnabled() const { return m_ssaoEnabled; }
    void SetSsaoEnabled(bool v) { m_ssaoEnabled = v; }
    bool SsrEnabled() const { return m_ssrEnabled; }
    void SetSsrEnabled(bool v) { m_ssrEnabled = v; }

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
    bgfx::UniformHandle s_shadowMap   = BGFX_INVALID_HANDLE;
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

    uint16_t m_width = 0, m_height = 0;
    bool m_invertEnabled = false;
    bool m_fxaaEnabled   = false;
    bool m_ssaoEnabled   = false;
    bool m_ssrEnabled    = false;
};

} // namespace Galeshi
