#pragma once

#include <cstdint>
#include <memory>

struct SDL_Window;

namespace Galeshi {

struct RendererSpec {
    SDL_Window* window = nullptr;
    uint32_t    width  = 1600;
    uint32_t    height = 900;
    bool        vsync  = true;
    bool        debug  = false;
};

class Renderer {
public:
    Renderer();
    ~Renderer();

    Renderer(const Renderer&)            = delete;
    Renderer& operator=(const Renderer&) = delete;

    bool Init(const RendererSpec& spec);
    void Shutdown();

    void BeginFrame();
    void EndFrame();

    void Resize(uint32_t w, uint32_t h);

    uint32_t Width()  const;
    uint32_t Height() const;

    uint8_t NextViewId();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
