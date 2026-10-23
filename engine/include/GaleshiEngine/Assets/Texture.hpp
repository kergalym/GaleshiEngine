#pragma once

#include <bgfx/bgfx.h>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Galeshi {

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    // Загрузка из файла: PNG/JPG/BMP/TGA (stb) и DDS/KTX/PVR/GNF/TEX (bimg).
    // Сжатые форматы (BC*, ETC*, ASTC, ...) загружаются без распаковки и
    // передаются на GPU вместе с полной цепочкой mip-уровней.
    // srgb=true включает аппаратное sRGB->linear преобразование при выборке.
    static Texture* LoadFromFile(const std::string& path, bool srgb = false);
    static Texture* LoadFromMemory(const unsigned char* data, size_t size,
                                  const std::string& displayPath = "<embedded>",
                                  bool srgb = false);
    // Assimp raw embedded texture: BGRA8, mWidth x mHeight pixels.
    static Texture* CreateFromBgra8(const unsigned char* data, int width, int height,
                                    const std::string& displayPath = "<embedded-raw>",
                                    bool srgb = false);

    // 1x1 белая / плоская normal / чёрная
    static Texture* CreateWhite();
    static Texture* CreateFlatNormal();
    static Texture* CreateBlack();

    // Compress ordinary source images (PNG/JPG/...) to BC7 while loading.
    // Disabled by default: runtime BC7 encoding is intentionally opt-in because
    // it is expensive. The GALESHI_TEXTURE_COMPRESSION environment variable can
    // enable it at startup. Already-compressed DDS/KTX data is unaffected.
    static void SetCompressionEnabled(bool enabled);
    static bool CompressionEnabled();

    bgfx::TextureHandle Handle() const { return m_handle; }
    const std::string& Path() const { return m_path; }
    bool IsSrgb() const { return m_srgb; }
    bool Valid() const { return bgfx::isValid(m_handle); }

    uint16_t Width() const { return m_width; }
    uint16_t Height() const { return m_height; }
    uint8_t MipCount() const { return m_numMips; }
    bgfx::TextureFormat::Enum Format() const { return m_format; }
    bool IsCompressed() const { return m_compressed; }
    size_t MemoryBytes() const { return m_memoryBytes; }

    void Destroy();

private:
    friend struct TextureUploader;

    bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
    std::string         m_path;
    bool                m_srgb = false;
    bool                m_compressed = false;
    uint16_t            m_width = 0;
    uint16_t            m_height = 0;
    uint8_t             m_numMips = 0;
    bgfx::TextureFormat::Enum m_format = bgfx::TextureFormat::RGBA8;
    size_t              m_memoryBytes = 0;
};

} // namespace Galeshi
