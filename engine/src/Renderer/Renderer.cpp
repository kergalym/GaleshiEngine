#include "GaleshiEngine/Renderer/Renderer.hpp"

#include <SDL3/SDL.h>
#include <bgfx/bgfx.h>
#include <bgfx/platform.h>
#include <bx/math.h>
#include <spdlog/spdlog.h>

namespace Galeshi {

struct Renderer::Impl {
    RendererSpec spec{};
    uint32_t width  = 1600;
    uint32_t height = 900;
    uint32_t resetFlags = BGFX_RESET_NONE;
    uint8_t  nextViewId = 10;   // 0..9 — служебные view
    bool     initialized = false;
};

Renderer::Renderer()  : m_impl(std::make_unique<Impl>()) {}
Renderer::~Renderer() { Shutdown(); }

bool Renderer::Init(const RendererSpec& spec) {
    m_impl->spec   = spec;
    m_impl->width  = spec.width;
    m_impl->height = spec.height;

    if (!spec.window) {
        spdlog::error("[Renderer] SDL_Window is null");
        return false;
    }

    SDL_PropertiesID props = SDL_GetWindowProperties(spec.window);

    bgfx::PlatformData pd{};
#if defined(_WIN32)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr);
#elif defined(__linux__)
    pd.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr);
    pd.nwh = reinterpret_cast<void*>(static_cast<uintptr_t>(
        SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0)));
#elif defined(__APPLE__)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
#else
    #error "Unsupported platform for bgfx native window handle"
#endif

    bgfx::setPlatformData(pd);

    bgfx::Init init{};
    init.type              = bgfx::RendererType::Vulkan;
    init.vendorId          = BGFX_PCI_ID_NONE;
    init.platformData      = pd;
    init.resolution.width  = m_impl->width;
    init.resolution.height = m_impl->height;
    init.resolution.reset  = BGFX_RESET_VSYNC;

    if (!bgfx::init(init)) {
        spdlog::error("[Renderer] bgfx::init failed (Vulkan backend)");
        return false;
    }

    m_impl->resetFlags  = BGFX_RESET_VSYNC;
    m_impl->initialized = true;

    bgfx::setViewClear(0,
        BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH,
        0x232629ff, 1.0f, 0);
    bgfx::setViewRect(0, 0, 0,
        static_cast<uint16_t>(m_impl->width),
        static_cast<uint16_t>(m_impl->height));

    spdlog::info("[Renderer] bgfx initialized — {}",
                 bgfx::getRendererName(bgfx::getRendererType()));

    return true;
}

void Renderer::Shutdown() {
    if (!m_impl) return;
    if (m_impl->initialized) {
        bgfx::shutdown();
        m_impl->initialized = false;
    }
}

void Renderer::BeginFrame() {
    bgfx::setViewRect(0, 0, 0,
        static_cast<uint16_t>(m_impl->width),
        static_cast<uint16_t>(m_impl->height));
    bgfx::touch(0);
}

void Renderer::EndFrame() {
    bgfx::frame();
}

void Renderer::Resize(uint32_t w, uint32_t h) {
    if (w == 0 || h == 0) return;
    m_impl->width  = w;
    m_impl->height = h;
    bgfx::reset(w, h, m_impl->resetFlags);
    bgfx::setViewRect(0, 0, 0,
        static_cast<uint16_t>(w), static_cast<uint16_t>(h));
}

uint32_t Renderer::Width()  const { return m_impl->width; }
uint32_t Renderer::Height() const { return m_impl->height; }

uint8_t Renderer::NextViewId() {
    const uint8_t id = m_impl->nextViewId++;
    if (m_impl->nextViewId > 250) m_impl->nextViewId = 10;
    return id;
}

} // namespace Galeshi
