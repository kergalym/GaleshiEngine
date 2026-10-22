#include "GaleshiEngine/Renderer/Framebuffer.hpp"
#include <spdlog/spdlog.h>

namespace Galeshi {

Framebuffer::~Framebuffer() { Destroy(); }

void Framebuffer::Destroy() {
    if (bgfx::isValid(m_fbh))      bgfx::destroy(m_fbh);
    if (bgfx::isValid(m_color))    bgfx::destroy(m_color);
    if (bgfx::isValid(m_normal))   bgfx::destroy(m_normal);
    if (bgfx::isValid(m_worldPos)) bgfx::destroy(m_worldPos);
    if (bgfx::isValid(m_depth))    bgfx::destroy(m_depth);
    m_fbh      = BGFX_INVALID_HANDLE;
    m_color    = BGFX_INVALID_HANDLE;
    m_normal   = BGFX_INVALID_HANDLE;
    m_worldPos = BGFX_INVALID_HANDLE;
    m_depth    = BGFX_INVALID_HANDLE;
    m_width = m_height = 0;
}

bool Framebuffer::Create(uint16_t w, uint16_t h) {
    if (w == 0 || h == 0) return false;
    m_width = w; m_height = h;

    const uint64_t flags = BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP;

    // RT0: albedo.rgb + metallic.a. It also receives the final post-processed
    // image for the transparent/debug overlay pass, so it must be a blit dst.
    m_color = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags | BGFX_TEXTURE_BLIT_DST);
    // RT1: normal.xyz encoded + roughness.a
    m_normal = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    // RT2: worldPos.xyz (F16 for precision)
    m_worldPos = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::RGBA16F, flags);
    // Depth
    m_depth = bgfx::createTexture2D(w, h, false, 1, bgfx::TextureFormat::D32F, flags);

    if (!bgfx::isValid(m_color) || !bgfx::isValid(m_normal) ||
        !bgfx::isValid(m_worldPos) || !bgfx::isValid(m_depth)) {
        spdlog::error("[FBO] texture creation failed (color={} normal={} worldPos={} depth={})",
            bgfx::isValid(m_color), bgfx::isValid(m_normal),
            bgfx::isValid(m_worldPos), bgfx::isValid(m_depth));
        Destroy();
        return false;
    }

    bgfx::TextureHandle att[] = { m_color, m_normal, m_worldPos, m_depth };
    m_fbh = bgfx::createFrameBuffer(4, att, false);
    if (!bgfx::isValid(m_fbh)) {
        spdlog::error("[FBO] createFrameBuffer failed");
        Destroy();
        return false;
    }
    spdlog::info("[FBO] Created {}x{} (color + normal + worldPos + depth)", w, h);
    return true;
}

void Framebuffer::Resize(uint16_t w, uint16_t h) {
    if (w == m_width && h == m_height) return;
    Destroy();
    Create(w, h);
}

} // namespace Galeshi
