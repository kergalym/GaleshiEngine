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

    bgfx::FrameBufferHandle Handle()     const { return m_fbh; }
    // RT0..RT3 (deferred G-Buffer targets)
    bgfx::TextureHandle     Color()      const { return m_color; }      // albedo.rgb + metallic.a
    bgfx::TextureHandle     Normal()     const { return m_normal; }     // normal.xyz*0.5+0.5 + rough.a
    bgfx::TextureHandle     WorldPos()   const { return m_worldPos; }   // worldPos.xyz
    bgfx::TextureHandle     Material()   const { return m_material; }   // emissive.rgb + material AO
    bgfx::TextureHandle     Depth()      const { return m_depth; }

    uint16_t Width()  const { return m_width; }
    uint16_t Height() const { return m_height; }
    bool Valid() const { return bgfx::isValid(m_fbh); }

private:
    bgfx::FrameBufferHandle m_fbh      = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_color    = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_normal   = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_worldPos = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_material = BGFX_INVALID_HANDLE;
    bgfx::TextureHandle     m_depth    = BGFX_INVALID_HANDLE;
    uint16_t m_width  = 0;
    uint16_t m_height = 0;
};

} // namespace Galeshi
