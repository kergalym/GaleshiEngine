#pragma once
#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstdint>

namespace Galeshi {

class PostProcessManager {
public:
    PostProcessManager();
    ~PostProcessManager();

    PostProcessManager(const PostProcessManager&)            = delete;
    PostProcessManager& operator=(const PostProcessManager&) = delete;

    void Init();
    void Shutdown();

    // Renders inputColor through active post-process chain into internal FBO.
    // Returns output color texture (use with ImGui::Image / next pass).
    bgfx::TextureHandle Render(bgfx::ViewId viewId,
                               bgfx::TextureHandle inputColor,
                               uint16_t width, uint16_t height);

    bgfx::TextureHandle OutputTexture() const { return m_outputColor; }
    bgfx::FrameBufferHandle OutputFbo() const { return m_outputFbo; }

    bgfx::TextureHandle RenderSSR(bgfx::TextureHandle color, bgfx::TextureHandle normal, bgfx::TextureHandle depth, bgfx::ViewId viewId, uint16_t width, uint16_t height);

    bool SsrEnabled() const    { return m_ssrEnabled; }
    void SetSsrEnabled(bool v) { m_ssrEnabled = v; }

    // Deferred lighting: reads G-Buffer RTs, writes to m_outputColor.
    bgfx::TextureHandle RenderLighting(bgfx::TextureHandle albedoMr,
                                       bgfx::TextureHandle normalRough,
                                       bgfx::TextureHandle worldPos,
                                       bgfx::TextureHandle shadowMap,
                                       const glm::mat4& lightMtx,
                                       const float camPos[4],
                                       const float lightDir[4],
                                       const float lightColorIntensity[4],
                                       const float pointLightPos[][4],
                                       const float pointLightColor[][4],
                                       int pointLightCount,
                                       const float spotLightPosRadius[][4],
                                       const float spotLightDirInner[][4],
                                       const float spotLightColorOuter[][4],
                                       int spotLightCount,
                                       bgfx::ViewId viewId,
                                       uint16_t width, uint16_t height);

    // Effect toggles (UI can flip these at runtime).
    bool InvertEnabled() const       { return m_invertEnabled; }
    void SetInvertEnabled(bool v)    { m_invertEnabled = v; }

    bool FxaaEnabled() const    { return m_fxaaEnabled; }
    void SetFxaaEnabled(bool v) { m_fxaaEnabled = v; }

private:
    void EnsureOutputFbo(uint16_t w, uint16_t h);

    bgfx::ProgramHandle     m_program     = BGFX_INVALID_HANDLE;   // passthrough
    bgfx::ProgramHandle     m_invertProg  = BGFX_INVALID_HANDLE;   // invert
    bgfx::ProgramHandle     m_fxaaProg    = BGFX_INVALID_HANDLE;   // fxaa
    bgfx::ProgramHandle     m_lightingProg = BGFX_INVALID_HANDLE;  // deferred
    bgfx::ProgramHandle     m_ssrProg      = BGFX_INVALID_HANDLE;  // SSR
    bgfx::UniformHandle     s_texColor    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     u_texelSize   = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     u_camPos      = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     u_lightDir    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     u_lightCol    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     s_albedoMr    = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     s_normalRough = BGFX_INVALID_HANDLE;
    bgfx::UniformHandle     s_worldPos    = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_vbh        = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ibh        = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle     m_outputColor = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_outputDepth = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_outputFbo   = BGFX_INVALID_HANDLE;
    uint16_t m_width = 0, m_height = 0;
    bool     m_invertEnabled = false;
    bool     m_fxaaEnabled   = false;
    bool     m_ssrEnabled    = false;
};

} // namespace Galeshi
