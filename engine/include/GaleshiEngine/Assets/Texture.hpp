#pragma once

#include <bgfx/bgfx.h>
#include <string>

namespace Galeshi {

class Texture {
public:
    Texture() = default;
    ~Texture();

    Texture(const Texture&)            = delete;
    Texture& operator=(const Texture&) = delete;

    // Загрузка из файла (PNG/JPG/DDS/KTX через bimg)
    static Texture* LoadFromFile(const std::string& path);

    // 1x1 белая / плоская normal / чёрная
    static Texture* CreateWhite();
    static Texture* CreateFlatNormal();
    static Texture* CreateBlack();

    bgfx::TextureHandle Handle() const { return m_handle; }
    bool Valid() const { return bgfx::isValid(m_handle); }

    void Destroy();

private:
    bgfx::TextureHandle m_handle = BGFX_INVALID_HANDLE;
    std::string         m_path;
};

} // namespace Galeshi
