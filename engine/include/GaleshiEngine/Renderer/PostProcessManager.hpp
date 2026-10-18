#pragma once
#include <bgfx/bgfx.h>
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

    // Effect toggles (UI can flip these at runtime).
    bool InvertEnabled() const       { return m_invertEnabled; }
    void SetInvertEnabled(bool v)    { m_invertEnabled = v; }

private:
    void EnsureOutputFbo(uint16_t w, uint16_t h);

    bgfx::ProgramHandle     m_program     = BGFX_INVALID_HANDLE;   // passthrough
    bgfx::ProgramHandle     m_invertProg  = BGFX_INVALID_HANDLE;   // invert
    bgfx::UniformHandle     s_texColor    = BGFX_INVALID_HANDLE;
    bgfx::VertexBufferHandle m_vbh        = BGFX_INVALID_HANDLE;
    bgfx::IndexBufferHandle  m_ibh        = BGFX_INVALID_HANDLE;

    bgfx::TextureHandle     m_outputColor = BGFX_INVALID_HANDLE;
    bgfx::FrameBufferHandle m_outputFbo   = BGFX_INVALID_HANDLE;
    uint16_t m_width = 0, m_height = 0;
    bool     m_invertEnabled = false;
};

} // namespace Galeshi
