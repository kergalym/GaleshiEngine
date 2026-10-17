#pragma once

#include <bgfx/bgfx.h>
#include <cstdint>

namespace Galeshi {

class Framebuffer {
public:
    Framebuffer() = default;
    ~Framebuffer();

    Framebuffer(const Framebuffer&)            = delete;
    Framebuffer& operator=(const Framebuffer&) = delete;

    bool Create(uint16_t width, uint16_t height);
    void Destroy();
    void Resize(uint16_t width, uint16_t height);

    bgfx::FrameBufferHandle Handle()    const { return m_fbh; }
    bgfx::TextureHandle     Color()     const { return m_color; }
    uint16_t Width()  const { return m_width; }
    uint16_t Height() const { return m_height; }
    bool Valid() const { return bgfx::isValid(m_fbh); }

private:
    bgfx::FrameBufferHandle m_fbh   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_color = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_depth = BGFX_INVALID_HANDLE;
    uint16_t m_width  = 0;
    uint16_t m_height = 0;
};

} // namespace Galeshi
