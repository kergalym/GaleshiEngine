#pragma once

#include <bgfx/bgfx.h>
#include <string>
#include <cstddef>

namespace Galeshi {

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    // Загрузка из файла (PNG/JPG/DDS/KTX через bimg).
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

    bgfx::TextureHandle Handle() const { return m_handle; }
    const std::string& Path() const { return m_path; }
    bool IsSrgb() const { return m_srgb; }
    bool Valid() const { return bgfx::isValid(m_handle); }

    void Destroy();

private:
    bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
    std::string         m_path;
    bool                m_srgb = false;
};

} // namespace Galeshi
