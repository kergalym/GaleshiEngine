#include "GaleshiEngine/Assets/Texture.hpp"

#include <bimg/bimg.h>
#include <bimg/encode.h>
#include <bx/error.h>
#include <bx/math.h>

#include <spdlog/spdlog.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

// stb_image — header-only. Реализацию включаем ровно в одном .cpp.
#define STB_IMAGE_STATIC
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#include <stb_image.h>

namespace Galeshi {

// Internal helper used by the texture loading implementation. Declared as a
// friend so uploaded texture metadata stays encapsulated.
struct TextureUploader {
    static void Assign(Texture* tex, bgfx::TextureHandle handle,
                       const std::string& path, bool srgb, uint16_t width,
                       uint16_t height, uint8_t numMips,
                       bgfx::TextureFormat::Enum format, bool compressed,
                       size_t memoryBytes) {
        tex->m_handle = handle;
        tex->m_path = path;
        tex->m_srgb = srgb;
        tex->m_width = width;
        tex->m_height = height;
        tex->m_numMips = numMips;
        tex->m_format = format;
        tex->m_compressed = compressed;
        tex->m_memoryBytes = memoryBytes;
    }
};

namespace {

static_assert(static_cast<int>(bimg::TextureFormat::Count) ==
                  static_cast<int>(bgfx::TextureFormat::Count),
              "bimg and bgfx texture format enums must stay in sync");
static_assert(static_cast<int>(bimg::TextureFormat::BC7) ==
                  static_cast<int>(bgfx::TextureFormat::BC7),
              "bimg and bgfx texture format enums must stay in sync");
static_assert(static_cast<int>(bimg::TextureFormat::RGBA8) ==
                  static_cast<int>(bgfx::TextureFormat::RGBA8),
              "bimg and bgfx texture format enums must stay in sync");

bool& CompressionFlag() {
    static bool enabled = []() {
        const char* value = std::getenv("GALESHI_TEXTURE_COMPRESSION");
        if (!value || !*value) return false;
        return std::strcmp(value, "0") != 0 &&
               std::strcmp(value, "false") != 0 &&
               std::strcmp(value, "off") != 0;
    }();
    return enabled;
}

uint64_t TextureFlags(bool srgb) {
    uint64_t flags = BGFX_TEXTURE_NONE | BGFX_SAMPLER_NONE;
    if (srgb) flags |= BGFX_TEXTURE_SRGB;
    return flags;
}

bimg::TextureFormat::Enum ToBimg(bgfx::TextureFormat::Enum format) {
    return static_cast<bimg::TextureFormat::Enum>(format);
}

const char* FormatName(bgfx::TextureFormat::Enum format) {
    return bimg::getName(ToBimg(format));
}

bool EndsWithNoCase(const std::string& value, const char* suffix) {
    const size_t suffixLength = std::strlen(suffix);
    if (value.size() < suffixLength) return false;
    const size_t offset = value.size() - suffixLength;
    for (size_t i = 0; i < suffixLength; ++i) {
        const unsigned char a = static_cast<unsigned char>(value[offset + i]);
        const unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (std::tolower(a) != std::tolower(b)) return false;
    }
    return true;
}

bool LooksLikeContainer(const uint8_t* data, size_t size,
                        const std::string& path) {
    // bimg's imageParse logs a trace message for every non-container input.
    // Check the signatures first so PNG/JPEG/BMP/TGA files go straight to stb.
    if (size >= 4) {
        uint32_t magic = 0;
        std::memcpy(&magic, data, sizeof(magic));
        switch (magic) {
            case 0x20534444u: // "DDS "
            case 0x58544BABu: // KTX 1
            case 0x03525650u: // "PVR3"
            case 0x20464E47u: // "GNF "
            case 0x00584554u: // "TEX\0"
                return true;
            default:
                break;
        }
    }

    return EndsWithNoCase(path, ".dds") || EndsWithNoCase(path, ".ktx") ||
           EndsWithNoCase(path, ".pvr") || EndsWithNoCase(path, ".gnf") ||
           EndsWithNoCase(path, ".tex");
}

bool FormatSupported(bgfx::TextureFormat::Enum format, bool srgb) {
    const bgfx::Caps* caps = bgfx::getCaps();
    if (!caps) return false;
    uint32_t required = BGFX_CAPS_FORMAT_TEXTURE_2D;
    if (srgb) required |= BGFX_CAPS_FORMAT_TEXTURE_2D_SRGB;
    return (caps->formats[format] & required) == required;
}

struct MipLevel {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> pixels;
};

uint8_t LinearToByte(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    return static_cast<uint8_t>(clamped * 255.0f + 0.5f);
}

void DownsampleRgba8(const uint8_t* src, uint32_t srcW, uint32_t srcH,
                     uint8_t* dst, uint32_t dstW, uint32_t dstH, bool srgb) {
    if (!src || !dst || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) return;

    for (uint32_t y = 0; y < dstH; ++y) {
        const uint32_t y0 = static_cast<uint32_t>(uint64_t(y) * srcH / dstH);
        uint32_t y1 = static_cast<uint32_t>(uint64_t(y + 1) * srcH / dstH);
        if (y1 <= y0) y1 = y0 + 1;
        y1 = std::min(y1, srcH);

        for (uint32_t x = 0; x < dstW; ++x) {
            const uint32_t x0 = static_cast<uint32_t>(uint64_t(x) * srcW / dstW);
            uint32_t x1 = static_cast<uint32_t>(uint64_t(x + 1) * srcW / dstW);
            if (x1 <= x0) x1 = x0 + 1;
            x1 = std::min(x1, srcW);

            float r = 0.0f;
            float g = 0.0f;
            float b = 0.0f;
            float a = 0.0f;
            uint32_t count = 0;
            for (uint32_t sy = y0; sy < y1; ++sy) {
                const uint8_t* row = src + static_cast<size_t>(sy) * srcW * 4;
                for (uint32_t sx = x0; sx < x1; ++sx) {
                    const uint8_t* pixel = row + static_cast<size_t>(sx) * 4;
                    if (srgb) {
                        r += bx::toLinear(static_cast<float>(pixel[0]) / 255.0f);
                        g += bx::toLinear(static_cast<float>(pixel[1]) / 255.0f);
                        b += bx::toLinear(static_cast<float>(pixel[2]) / 255.0f);
                    } else {
                        r += static_cast<float>(pixel[0]);
                        g += static_cast<float>(pixel[1]);
                        b += static_cast<float>(pixel[2]);
                    }
                    a += static_cast<float>(pixel[3]);
                    ++count;
                }
            }

            if (count == 0) count = 1;
            const float inv = 1.0f / static_cast<float>(count);
            uint8_t* out = dst + (static_cast<size_t>(y) * dstW + x) * 4;
            if (srgb) {
                out[0] = LinearToByte(bx::toGamma(r * inv));
                out[1] = LinearToByte(bx::toGamma(g * inv));
                out[2] = LinearToByte(bx::toGamma(b * inv));
            } else {
                out[0] = LinearToByte(r * inv / 255.0f);
                out[1] = LinearToByte(g * inv / 255.0f);
                out[2] = LinearToByte(b * inv / 255.0f);
            }
            out[3] = LinearToByte(a * inv / 255.0f);
        }
    }
}

std::vector<MipLevel> BuildMipChain(const uint8_t* pixels, uint32_t width,
                                    uint32_t height, bool srgb) {
    std::vector<MipLevel> levels;
    if (!pixels || width == 0 || height == 0) return levels;

    levels.reserve(static_cast<size_t>(std::max<uint32_t>(
        1u, bimg::imageGetNumMips(bimg::TextureFormat::RGBA8,
                                  static_cast<uint16_t>(width),
                                  static_cast<uint16_t>(height)))));

    MipLevel base;
    base.width = width;
    base.height = height;
    base.pixels.assign(pixels, pixels + static_cast<size_t>(width) * height * 4);
    levels.push_back(std::move(base));

    uint32_t srcW = width;
    uint32_t srcH = height;
    while (srcW > 1 || srcH > 1) {
        const uint32_t dstW = std::max(1u, srcW / 2);
        const uint32_t dstH = std::max(1u, srcH / 2);

        MipLevel level;
        level.width = dstW;
        level.height = dstH;
        level.pixels.resize(static_cast<size_t>(dstW) * dstH * 4);

        const std::vector<uint8_t>& src = levels.back().pixels;
        // bimg's SIMD path handles the common even-sized, sRGB case quickly.
        if (srgb && srcW > 1 && srcH > 1 && (srcW & 1u) == 0 && (srcH & 1u) == 0) {
            bimg::imageRgba8Downsample2x2(level.pixels.data(), srcW, srcH, 1,
                                          srcW * 4, dstW * 4, src.data());
        } else {
            DownsampleRgba8(src.data(), srcW, srcH, level.pixels.data(),
                            dstW, dstH, srgb);
        }

        levels.push_back(std::move(level));
        srcW = dstW;
        srcH = dstH;
    }

    return levels;
}

bool EncodeBc7(const MipLevel& level, std::vector<uint8_t>& out) {
    if (level.width == 0 || level.height == 0) return false;

    const uint32_t blockW = (level.width + 3) / 4;
    const uint32_t blockH = (level.height + 3) / 4;
    out.resize(static_cast<size_t>(blockW) * blockH * 16);

    // nvtt's BC7 encoder reads full 4x4 blocks. Replicate the edge pixels so
    // non-multiple-of-four mip dimensions do not read past the source image.
    const uint32_t paddedW = blockW * 4;
    const uint32_t paddedH = blockH * 4;
    std::vector<float> padded(static_cast<size_t>(paddedW) * paddedH * 4);
    for (uint32_t y = 0; y < paddedH; ++y) {
        const uint32_t sy = std::min(y, level.height - 1);
        for (uint32_t x = 0; x < paddedW; ++x) {
            const uint32_t sx = std::min(x, level.width - 1);
            const uint8_t* src = level.pixels.data() +
                                 (static_cast<size_t>(sy) * level.width + sx) * 4;
            float* dst = padded.data() + (static_cast<size_t>(y) * paddedW + x) * 4;
            dst[0] = static_cast<float>(src[0]) / 255.0f;
            dst[1] = static_cast<float>(src[1]) / 255.0f;
            dst[2] = static_cast<float>(src[2]) / 255.0f;
            dst[3] = static_cast<float>(src[3]) / 255.0f;
        }
    }

    bx::Error error;
    bimg::imageEncodeFromRgba32f(nullptr, out.data(), padded.data(), paddedW,
                                 paddedH, 1, bimg::TextureFormat::BC7,
                                 bimg::Quality::Default, &error);
    return error.isOk();
}

MipLevel PadRgba8ToBlocks(const uint8_t* pixels, uint32_t width,
                          uint32_t height) {
    MipLevel padded;
    if (!pixels || width == 0 || height == 0) return padded;

    padded.width = std::max(4u, ((width + 3u) / 4u) * 4u);
    padded.height = std::max(4u, ((height + 3u) / 4u) * 4u);
    padded.pixels.resize(static_cast<size_t>(padded.width) * padded.height * 4);

    for (uint32_t y = 0; y < padded.height; ++y) {
        const uint32_t sy = std::min(y, height - 1);
        for (uint32_t x = 0; x < padded.width; ++x) {
            const uint32_t sx = std::min(x, width - 1);
            std::memcpy(padded.pixels.data() +
                            (static_cast<size_t>(y) * padded.width + x) * 4,
                        pixels + (static_cast<size_t>(sy) * width + sx) * 4, 4);
        }
    }
    return padded;
}

Texture* UploadRgba8(const std::vector<MipLevel>& levels, uint32_t width,
                     uint32_t height, const std::string& path, bool srgb) {
    if (levels.empty() || width == 0 || height == 0) return nullptr;

    size_t total = 0;
    for (const MipLevel& level : levels) {
        total += level.pixels.size();
    }
    if (total == 0 || total > UINT32_MAX) return nullptr;

    std::vector<uint8_t> upload;
    upload.reserve(total);
    for (const MipLevel& level : levels) {
        upload.insert(upload.end(), level.pixels.begin(), level.pixels.end());
    }

    Texture* tex = new Texture();
    const bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(width), static_cast<uint16_t>(height),
        levels.size() > 1, 1, bgfx::TextureFormat::RGBA8, TextureFlags(srgb),
        bgfx::copy(upload.data(), static_cast<uint32_t>(upload.size())));

    if (!bgfx::isValid(handle)) {
        spdlog::error("[Texture] Cannot upload RGBA8 texture '{}' ({}x{}, {} mips)",
                      path, width, height, levels.size());
        delete tex;
        return nullptr;
    }
    TextureUploader::Assign(tex, handle, path, srgb, static_cast<uint16_t>(width),
                            static_cast<uint16_t>(height),
                            static_cast<uint8_t>(levels.size()),
                            bgfx::TextureFormat::RGBA8, false, upload.size());
    return tex;
}

Texture* UploadBc7(const std::vector<MipLevel>& levels, uint32_t width,
                   uint32_t height, const std::string& path, bool srgb) {
    if (levels.empty() || width == 0 || height == 0) return nullptr;

    std::vector<uint8_t> encoded;
    size_t total = 0;
    for (const MipLevel& level : levels) {
        std::vector<uint8_t> mip;
        if (!EncodeBc7(level, mip)) {
            spdlog::warn("[Texture] BC7 compression failed for '{}' at {}x{}",
                         path, level.width, level.height);
            return nullptr;
        }
        total += mip.size();
        encoded.insert(encoded.end(), mip.begin(), mip.end());
    }
    if (total == 0 || total > UINT32_MAX) return nullptr;

    const uint32_t expectedSize = bimg::imageGetSize(
        nullptr, static_cast<uint16_t>(width), static_cast<uint16_t>(height),
        1, false, levels.size() > 1, 1, bimg::TextureFormat::BC7);
    if (encoded.size() != expectedSize) {
        spdlog::error("[Texture] BC7 mip chain size mismatch for '{}' ({} bytes, expected {})",
                      path, encoded.size(), expectedSize);
        return nullptr;
    }

    Texture* tex = new Texture();
    const bgfx::TextureHandle handle = bgfx::createTexture2D(
        static_cast<uint16_t>(width), static_cast<uint16_t>(height),
        levels.size() > 1, 1, bgfx::TextureFormat::BC7, TextureFlags(srgb),
        bgfx::copy(encoded.data(), static_cast<uint32_t>(encoded.size())));

    if (!bgfx::isValid(handle)) {
        spdlog::error("[Texture] Cannot upload BC7 texture '{}' ({}x{}, {} mips)",
                      path, width, height, levels.size());
        delete tex;
        return nullptr;
    }
    TextureUploader::Assign(tex, handle, path, srgb, static_cast<uint16_t>(width),
                            static_cast<uint16_t>(height),
                            static_cast<uint8_t>(levels.size()),
                            bgfx::TextureFormat::BC7, true, encoded.size());
    return tex;
}

Texture* CreateFromRgba8Source(const uint8_t* pixels, uint32_t width,
                               uint32_t height, const std::string& path,
                               bool srgb, bool allowCompression) {
    if (!pixels || width == 0 || height == 0) return nullptr;

    if (allowCompression && CompressionFlag() &&
        FormatSupported(bgfx::TextureFormat::BC7, srgb)) {
        // BC storage always covers complete 4x4 blocks. Pad the base image to
        // block boundaries first so bgfx and the GPU agree on every mip size,
        // including non-multiple-of-four source dimensions.
        const MipLevel padded = PadRgba8ToBlocks(pixels, width, height);
        const std::vector<MipLevel> compressedLevels =
            BuildMipChain(padded.pixels.data(), padded.width, padded.height, srgb);
        if (Texture* compressed = UploadBc7(
                compressedLevels, padded.width, padded.height, path, srgb)) {
            spdlog::info("[Texture] Loaded '{}' ({}x{}, {} mips, BC7{})",
                         path, compressed->Width(), compressed->Height(),
                         compressed->MipCount(), srgb ? ", sRGB" : "");
            return compressed;
        }
        spdlog::warn("[Texture] Falling back to RGBA8 for '{}'", path);
    }

    const std::vector<MipLevel> levels = BuildMipChain(pixels, width, height, srgb);
    if (levels.empty()) return nullptr;

    Texture* tex = UploadRgba8(levels, width, height, path, srgb);
    if (tex) {
        spdlog::info("[Texture] Loaded '{}' ({}x{}, {} mips, RGBA8{})",
                     path, width, height, levels.size(), srgb ? ", sRGB" : "");
    }
    return tex;
}

Texture* LoadContainer(const bimg::ImageContainer& image, const uint8_t* bytes,
                       uint32_t size, const std::string& path, bool srgb) {
    if (image.m_width == 0 || image.m_height == 0 ||
        image.m_width > UINT16_MAX || image.m_height > UINT16_MAX) {
        spdlog::error("[Texture] Invalid dimensions in '{}' ({}x{})", path,
                      image.m_width, image.m_height);
        return nullptr;
    }
    if (image.m_depth > 1 || image.m_cubeMap || image.m_numLayers > 1) {
        spdlog::error("[Texture] '{}' is not a 2D texture (depth={}, layers={}, cube={})",
                      path, image.m_depth, image.m_numLayers, image.m_cubeMap);
        return nullptr;
    }
    if (!bimg::isValid(image.m_format) || bimg::isDepth(image.m_format)) {
        spdlog::error("[Texture] Unsupported texture format in '{}' ({})", path,
                      static_cast<int>(image.m_format));
        return nullptr;
    }

    const bimg::TextureFormat::Enum format = image.m_format;
    const bgfx::TextureFormat::Enum gpuFormat =
        static_cast<bgfx::TextureFormat::Enum>(format);
    const uint32_t width = image.m_width;
    const uint32_t height = image.m_height;
    const uint8_t expectedMips =
        bimg::imageGetNumMips(format, static_cast<uint16_t>(width),
                              static_cast<uint16_t>(height));
    const bool completeMips = image.m_numMips == expectedMips;

    if (bimg::isCompressed(format)) {
        const bimg::ImageBlockInfo& blockInfo = bimg::getBlockInfo(format);
        const uint32_t storageWidth = std::max<uint32_t>(
            blockInfo.blockWidth * blockInfo.minBlockX,
            ((width + blockInfo.blockWidth - 1) / blockInfo.blockWidth) *
                blockInfo.blockWidth);
        const uint32_t storageHeight = std::max<uint32_t>(
            blockInfo.blockHeight * blockInfo.minBlockY,
            ((height + blockInfo.blockHeight - 1) / blockInfo.blockHeight) *
                blockInfo.blockHeight);
        if (storageWidth > UINT16_MAX || storageHeight > UINT16_MAX) {
            spdlog::error("[Texture] '{}' is too large after block alignment ({}x{})",
                          path, storageWidth, storageHeight);
            return nullptr;
        }

        if (completeMips && FormatSupported(gpuFormat, srgb)) {
            std::vector<uint8_t> upload;
            upload.reserve(static_cast<size_t>(storageWidth) * storageHeight);
            uint8_t lodCount = 0;
            for (uint8_t lod = 0; lod < image.m_numMips; ++lod) {
                bimg::ImageMip mip{};
                if (!bimg::imageGetRawData(image, 0, lod, bytes, size, mip)) {
                    spdlog::error("[Texture] Cannot read mip {} from '{}'", lod, path);
                    return nullptr;
                }
                upload.insert(upload.end(), mip.m_data, mip.m_data + mip.m_size);
                ++lodCount;
            }

            const uint32_t expectedSize = bimg::imageGetSize(
                nullptr, static_cast<uint16_t>(storageWidth),
                static_cast<uint16_t>(storageHeight), 1, false, true, 1, format);
            if (upload.size() != expectedSize) {
                spdlog::warn("[Texture] '{}' has an incomplete mip chain ({} bytes, expected {}); decoding",
                              path, upload.size(), expectedSize);
            } else {
                Texture* tex = new Texture();
                const bgfx::TextureHandle handle = bgfx::createTexture2D(
                    static_cast<uint16_t>(storageWidth),
                    static_cast<uint16_t>(storageHeight),
                    lodCount > 1, 1, gpuFormat, TextureFlags(srgb),
                    bgfx::copy(upload.data(), static_cast<uint32_t>(upload.size())));

                if (bgfx::isValid(handle)) {
                    TextureUploader::Assign(
                        tex, handle, path, srgb,
                        static_cast<uint16_t>(storageWidth),
                        static_cast<uint16_t>(storageHeight), lodCount,
                        gpuFormat, true, upload.size());
                    spdlog::info("[Texture] Loaded compressed '{}' ({}x{}, {} mips, {}{})",
                                 path, storageWidth, storageHeight, lodCount,
                                 FormatName(gpuFormat), srgb ? ", sRGB" : "");
                    return tex;
                }
                spdlog::error("[Texture] Cannot upload compressed texture '{}' ({})",
                              path, FormatName(gpuFormat));
                delete tex;
                return nullptr;
            }
        } else if (!completeMips) {
            spdlog::warn("[Texture] '{}' has a partial mip chain ({} of {}); decoding",
                         path, image.m_numMips, expectedMips);
        } else {
            spdlog::warn("[Texture] '{}' uses unsupported compressed format {}; decoding",
                         path, FormatName(gpuFormat));
        }

        // Decode the base mip to RGBA8 and rebuild a safe, complete mip chain.
        bimg::ImageMip baseMip{};
        if (!bimg::imageGetRawData(image, 0, 0, bytes, size, baseMip)) {
            spdlog::error("[Texture] Cannot read base mip from '{}'", path);
            return nullptr;
        }
        std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
        bimg::imageDecodeToRgba8(nullptr, rgba.data(), baseMip.m_data, width,
                                 height, width * 4, format);
        return CreateFromRgba8Source(rgba.data(), width, height, path, srgb,
                                     CompressionFlag());
    }

    // Uncompressed container (RGBA8/BGRA8/R8/...). Convert every available
    // mip to RGBA8 so shaders always see the expected channel order.
    if (completeMips) {
        std::vector<MipLevel> levels;
        levels.reserve(image.m_numMips);
        for (uint8_t lod = 0; lod < image.m_numMips; ++lod) {
            bimg::ImageMip mip{};
            if (!bimg::imageGetRawData(image, 0, lod, bytes, size, mip)) {
                spdlog::error("[Texture] Cannot read mip {} from '{}'", lod, path);
                return nullptr;
            }
            MipLevel level;
            level.width = mip.m_width;
            level.height = mip.m_height;
            level.pixels.resize(static_cast<size_t>(level.width) * level.height * 4);
            if (!bimg::imageConvert(nullptr, level.pixels.data(),
                                    bimg::TextureFormat::RGBA8, mip.m_data,
                                    format, level.width, level.height, 1)) {
                spdlog::error("[Texture] Cannot convert '{}' from {} to RGBA8",
                              path, FormatName(gpuFormat));
                return nullptr;
            }
            levels.push_back(std::move(level));
        }
        if (Texture* tex = UploadRgba8(levels, width, height, path, srgb)) {
            spdlog::info("[Texture] Loaded '{}' ({}x{}, {} mips, RGBA8{})",
                         path, width, height, levels.size(), srgb ? ", sRGB" : "");
            return tex;
        }
        return nullptr;
    }

    bimg::ImageMip baseMip{};
    if (!bimg::imageGetRawData(image, 0, 0, bytes, size, baseMip)) {
        spdlog::error("[Texture] Cannot read base mip from '{}'", path);
        return nullptr;
    }
    std::vector<uint8_t> rgba(static_cast<size_t>(width) * height * 4);
    bimg::imageDecodeToRgba8(nullptr, rgba.data(), baseMip.m_data, width, height,
                             width * 4, format);
    spdlog::warn("[Texture] '{}' has a partial mip chain ({} of {}); rebuilding",
                 path, image.m_numMips, expectedMips);
    return CreateFromRgba8Source(rgba.data(), width, height, path, srgb,
                                 CompressionFlag());
}

Texture* LoadEncodedBytes(const uint8_t* data, size_t size,
                          const std::string& displayPath, bool srgb) {
    if (!data || size == 0 || size > UINT32_MAX) return nullptr;

    // Container formats (DDS/KTX/PVR/GNF/TEX) are parsed by bimg. Other image
    // files go straight to stb so bimg does not log a failed magic check.
    if (LooksLikeContainer(data, size, displayPath)) {
        bimg::ImageContainer image{};
        bx::Error error;
        if (!bimg::imageParse(image, data, static_cast<uint32_t>(size), &error)) {
            spdlog::error("[Texture] Cannot parse texture container '{}' ({} bytes)",
                          displayPath, size);
            return nullptr;
        }
        return LoadContainer(image, data, static_cast<uint32_t>(size),
                             displayPath, srgb);
    }

    int width = 0;
    int height = 0;
    int channels = 0;

    // UV в bgfx идут сверху-вниз (как в DDS), но stb по умолчанию
    // грузит снизу-вверх. Разворачиваем.
    stbi_set_flip_vertically_on_load(true);
    unsigned char* pixels = stbi_load_from_memory(
        data, static_cast<int>(size), &width, &height, &channels, 4);
    if (!pixels) {
        spdlog::error("[Texture] Cannot decode '{}' ({} bytes): {}", displayPath,
                      size, stbi_failure_reason());
        return nullptr;
    }
    if (width <= 0 || height <= 0 || width > UINT16_MAX || height > UINT16_MAX) {
        spdlog::error("[Texture] Invalid source image dimensions in '{}' ({}x{})",
                      displayPath, width, height);
        stbi_image_free(pixels);
        return nullptr;
    }

    Texture* tex = CreateFromRgba8Source(
        pixels, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
        displayPath, srgb, true);
    stbi_image_free(pixels);
    return tex;
}

} // namespace

Texture::~Texture() { Destroy(); }

void Texture::Destroy() {
    if (bgfx::isValid(m_handle)) {
        bgfx::destroy(m_handle);
        m_handle = BGFX_INVALID_HANDLE;
    }
}

void Texture::SetCompressionEnabled(bool enabled) { CompressionFlag() = enabled; }
bool Texture::CompressionEnabled() { return CompressionFlag(); }

Texture* Texture::LoadFromFile(const std::string& path, bool srgb) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        spdlog::error("[Texture] Cannot open: {}", path);
        return nullptr;
    }

    const std::streamoff length = file.tellg();
    if (length <= 0 || static_cast<uint64_t>(length) > UINT32_MAX) {
        spdlog::error("[Texture] Empty or oversized file: {}", path);
        return nullptr;
    }

    std::vector<uint8_t> bytes(static_cast<size_t>(length));
    file.seekg(0, std::ios::beg);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), length)) {
        spdlog::error("[Texture] Cannot read: {}", path);
        return nullptr;
    }

    return LoadEncodedBytes(bytes.data(), bytes.size(), path, srgb);
}

Texture* Texture::LoadFromMemory(const unsigned char* data, size_t size,
                                 const std::string& displayPath, bool srgb) {
    return LoadEncodedBytes(data, size, displayPath, srgb);
}

Texture* Texture::CreateFromBgra8(const unsigned char* data, int width, int height,
                                  const std::string& displayPath, bool srgb) {
    if (!data || width <= 0 || height <= 0) return nullptr;

    const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
    std::vector<unsigned char> rgba(pixelCount * 4);
    for (size_t i = 0; i < pixelCount; ++i) {
        rgba[i * 4 + 0] = data[i * 4 + 2];
        rgba[i * 4 + 1] = data[i * 4 + 1];
        rgba[i * 4 + 2] = data[i * 4 + 0];
        rgba[i * 4 + 3] = data[i * 4 + 3];
    }

    Texture* tex = CreateFromRgba8Source(
        rgba.data(), static_cast<uint32_t>(width), static_cast<uint32_t>(height),
        displayPath, srgb, true);
    if (!tex) {
        spdlog::error("[Texture] Cannot upload raw embedded texture '{}' ({}x{})",
                      displayPath, width, height);
    }
    return tex;
}

Texture* Texture::CreateWhite() {
    const uint32_t pixel = 0xFFFFFFFF;
    Texture* tex = new Texture();
    tex->m_path = "<white>";
    tex->m_width = 1;
    tex->m_height = 1;
    tex->m_numMips = 1;
    tex->m_format = bgfx::TextureFormat::RGBA8;
    tex->m_memoryBytes = sizeof(pixel);
    tex->m_handle = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&pixel, sizeof(pixel)));
    return tex;
}

Texture* Texture::CreateBlack() {
    // RGBA8: little-endian bytes 00 00 00 FF -> opaque black.
    const uint32_t pixel = 0xFF000000u;
    Texture* tex = new Texture();
    tex->m_path = "<black>";
    tex->m_width = 1;
    tex->m_height = 1;
    tex->m_numMips = 1;
    tex->m_format = bgfx::TextureFormat::RGBA8;
    tex->m_memoryBytes = sizeof(pixel);
    tex->m_handle = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&pixel, sizeof(pixel)));
    return tex;
}

Texture* Texture::CreateFlatNormal() {
    // (0.5, 0.5, 1.0) в RGBA8
    const uint32_t pixel = 0xFFFF8080;
    Texture* tex = new Texture();
    tex->m_path = "<flat-normal>";
    tex->m_width = 1;
    tex->m_height = 1;
    tex->m_numMips = 1;
    tex->m_format = bgfx::TextureFormat::RGBA8;
    tex->m_memoryBytes = sizeof(pixel);
    tex->m_handle = bgfx::createTexture2D(1, 1, false, 1,
        bgfx::TextureFormat::RGBA8, BGFX_TEXTURE_NONE,
        bgfx::copy(&pixel, sizeof(pixel)));
    return tex;
}

} // namespace Galeshi
