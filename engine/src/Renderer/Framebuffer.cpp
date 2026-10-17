#include "GaleshiEngine/Renderer/Framebuffer.hpp"
#include <spdlog/spdlog.h>

namespace Galeshi {

Framebuffer::~Framebuffer() { Destroy(); }

void Framebuffer::Destroy() {
    if (bgfx::isValid(m_fbh))   bgfx::destroy(m_fbh);
    if (bgfx::isValid(m_color)) bgfx::destroy(m_color);
    if (bgfx::isValid(m_depth)) bgfx::destroy(m_depth);

    m_fbh   = BGFX_INVALID_HANDLE;
    m_color = BGFX_INVALID_HANDLE;
    m_depth = BGFX_INVALID_HANDLE;
    m_width = m_height = 0;
}

bool Framebuffer::Create(uint16_t width, uint16_t height) {
    if (width == 0 || height == 0) return false;
    m_width  = width;
    m_height = height;

    const uint64_t texFlags =
        BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    m_color = bgfx::createTexture2D(
        width, height, false, 1,
        bgfx::TextureFormat::RGBA8, texFlags);

    m_depth = bgfx::createTexture2D(
        width, height, false, 1,
        bgfx::TextureFormat::D24S8, texFlags);

    bgfx::TextureHandle attachments[] = { m_color, m_depth };
    m_fbh = bgfx::createFrameBuffer(2, attachments, false);
    return bgfx::isValid(m_fbh);
}

void Framebuffer::Resize(uint16_t width, uint16_t height) {
    if (width == m_width && height == m_height) return;
    Destroy();
    Create(width, height);
}

} // namespace Galeshi
