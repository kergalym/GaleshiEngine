#include "GaleshiEngine/Assets/Texture.hpp"

#include <spdlog/spdlog.h>

// stb_image — header-only. Реализацию включаем ровно в одном .cpp.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>

namespace Galeshi {

Texture::~Texture() { Destroy(); }

void Texture::Destroy() {
    if (bgfx::isValid(m_handle)) {
        bgfx::destroy(m_handle);
        m_handle = BGFX_INVALID_HANDLE;
    }
}

Texture* Texture::LoadFromFile(const std::string& path) {
    int w = 0, h = 0, channels = 0;

    // UV в bgfx идут сверху-вниз (как в DDS), но stb по умолчанию
    // грузит снизу-вверх. Разворачиваем.
    stbi_set_flip_vertically_on_load(true);

    unsigned char* pixels = stbi_load(path.c_str(), &w, &h, &channels, 4);
    if (!pixels) {
        spdlog::error("[Texture] Cannot load: {} (reason: {})",
                      path, stbi_failure_reason());
        return nullptr;
    }

    Texture* tex = new Texture();
    tex->m_path   = path;
    tex->m_handle = bgfx::createTexture2D(
        static_cast<uint16_t>(w),
        static_cast<uint16_t>(h),
        false,
        1,
        bgfx::TextureFormat::RGBA8,
        BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE,
        bgfx::copy(pixels, static_cast<uint32_t>(w * h * 4)));

    stbi_image_free(pixels);

    spdlog::info("[Texture] Loaded {} ({}x{})", path, w, h);
    return tex;
}

Texture* Texture::CreateWhite() {
    const uint32_t pixel = 0xFFFFFFFF;
    Texture* tex = new Texture();
    tex->m_handle = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&pixel, sizeof(pixel)));
    return tex;
}

Texture* Texture::CreateBlack() {
    const uint32_t pixel = 0x000000FF;
    Texture* tex = new Texture();
    tex->m_handle = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&pixel, sizeof(pixel)));
    return tex;
}

Texture* Texture::CreateFlatNormal() {
    // (0.5, 0.5, 1.0) в RGBA8
    const uint32_t pixel = 0xFFFF8080;
    Texture* tex = new Texture();
    tex->m_handle = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&pixel, sizeof(pixel)));
    return tex;
}

} // namespace Galeshi
