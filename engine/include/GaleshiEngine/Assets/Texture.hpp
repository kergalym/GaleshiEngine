#pragma once

#include <bgfx/bgfx.h>
#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <string>

namespace Galeshi {

class Texture {
public:
    struct UdimInfo {
        bool valid = false;
        int minU = 0;
        int minV = 0;
        int tilesX = 1;
        int tilesY = 1;
        glm::vec2 uvScale{1.0f, 1.0f};
        glm::vec2 uvOffset{0.0f, 0.0f};
    };

    Texture() = default;
    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    // Loads PNG/JPG/BMP/TGA via stb and DDS/KTX/PVR/GNF/TEX via bimg.
    // Compressed formats are uploaded in their GPU format.
    // The full mip chain is preserved.
    // srgb enables hardware sRGB-to-linear sampling.
    // Most engine formats use a bottom-left V origin and flip source images
    // on upload. glTF uses a top-left origin and must be loaded with
    // flipVertically=false so its UVs and images stay consistent.
    static Texture* LoadFromFile(const std::string& path, bool srgb = false,
                                 bool flipVertically = true);
    // Loads a Substance Painter/UDIM tile set (1001, 1002, ...) into a single
    // atlas. The returned texture keeps the requested path so scene
    // serialization still references a real source file.
    static Texture* LoadUdimFromFile(const std::string& path, bool srgb,
                                    UdimInfo* outInfo = nullptr,
                                    bool flipVertically = true);
    static bool IsUdimPath(const std::string& path);
    static Texture* LoadFromMemory(
        const unsigned char* data, size_t size,
        const std::string& displayPath = "<embedded>", bool srgb = false,
        bool flipVertically = true);
    // Assimp raw embedded texture: BGRA8, mWidth x mHeight pixels.
    static Texture* CreateFromBgra8(
        const unsigned char* data, int width, int height,
        const std::string& displayPath = "<embedded-raw>", bool srgb = false,
        bool flipVertically = true);

    // 1x1 white, flat normal, and black textures.
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

    // Alpha metadata is measured from the decoded base mip. AlphaCoverage is
    // the fraction of pixels with alpha >= 250; it is used to distinguish
    // near-opaque cutouts (eyes) from true alpha-blended cards (hair, brows).
    bool HasAlpha() const { return m_hasAlpha; }
    float AlphaCoverage() const { return m_alphaCoverage; }

    void Destroy();

private:
    friend struct TextureUploader;

    bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
    std::string         m_path;
    bool                m_srgb = false;
    bool                m_compressed = false;
    bool                m_hasAlpha = false;
    float               m_alphaCoverage = 1.0f;
    uint16_t            m_width = 0;
    uint16_t            m_height = 0;
    uint8_t             m_numMips = 0;
    bgfx::TextureFormat::Enum m_format = bgfx::TextureFormat::RGBA8;
    size_t              m_memoryBytes = 0;
};

} // namespace Galeshi
