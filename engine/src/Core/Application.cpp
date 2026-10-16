#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/ImGuiBgfx.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>

#include <filesystem>

namespace Galeshi {

struct Application::Impl {
    ApplicationSpec spec;
    SDL_Window*     window = nullptr;
    bool            running = false;
    Uint64          lastCounter = 0;
    double          freq = 0.0;
    std::unique_ptr<Renderer> renderer;
};

Application::Application(const ApplicationSpec& spec)
    : m_impl(std::make_unique<Impl>())
{
    m_impl->spec = spec;

#if defined(__linux__)
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");
#endif

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        spdlog::critical("[Galeshi] SDL_Init failed: {}", SDL_GetError());
        std::abort();
    }

    const SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    m_impl->window = SDL_CreateWindow(spec.name.c_str(), spec.width, spec.height, flags);
    if (!m_impl->window) {
        spdlog::critical("[Galeshi] SDL_CreateWindow failed: {}", SDL_GetError());
        std::abort();
    }

    if (const char* basePath = SDL_GetBasePath()) {
        std::error_code ec;
        std::filesystem::current_path(basePath, ec);
        if (ec) {
            spdlog::warn("[Galeshi] Не удалось перейти в {}: {}",
                         basePath, ec.message());
        } else {
            spdlog::info("[Galeshi] CWD → {}", basePath);
        }
    }

    m_impl->renderer = std::make_unique<Renderer>();
    RendererSpec rspec;
    rspec.window = m_impl->window;
    rspec.width  = static_cast<uint32_t>(spec.width);
    rspec.height = static_cast<uint32_t>(spec.height);
    if (!m_impl->renderer->Init(rspec)) {
        spdlog::critical("[Galeshi] Renderer init failed");
        std::abort();
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
    ImGui::StyleColorsDark();

    ImGui_ImplSDL3_InitForOther(m_impl->window);
    ImGuiBgfx::Init();

    m_impl->freq = static_cast<double>(SDL_GetPerformanceFrequency());
    m_impl->lastCounter = SDL_GetPerformanceCounter();

    spdlog::info("[Galeshi] '{}' initialized ({}x{})",
                 spec.name, spec.width, spec.height);
}

Application::~Application() {
    if (!m_impl) return;

    // ВАЖНО: сначала освобождаем ресурсы ImGui, потом гасим bgfx.
    // Иначе bgfx::destroy() вызовется после bgfx::shutdown() и даст утечку.
    ImGuiBgfx::Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();

    if (m_impl->renderer) m_impl->renderer->Shutdown();

    if (m_impl->window) SDL_DestroyWindow(m_impl->window);
    SDL_Quit();
}

Renderer* Application::GetRenderer() const { return m_impl->renderer.get(); }

int Application::Run() {
    OnStart();
    m_impl->running = true;

    while (m_impl->running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            if (e.type == SDL_EVENT_QUIT) m_impl->running = false;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED &&
                e.window.windowID == SDL_GetWindowID(m_impl->window))
                m_impl->running = false;
            if (e.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED) {
                int w = 0, h = 0;
                SDL_GetWindowSizeInPixels(m_impl->window, &w, &h);
                m_impl->renderer->Resize(static_cast<uint32_t>(w),
                                         static_cast<uint32_t>(h));
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        const float dt = static_cast<float>((now - m_impl->lastCounter) / m_impl->freq);
        m_impl->lastCounter = now;

        OnUpdate(dt);

        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        OnImGui();
        ImGui::Render();

        m_impl->renderer->BeginFrame();
        OnRender();
        ImGuiBgfx::Render(m_impl->renderer->NextViewId());
        m_impl->renderer->EndFrame();
    }

    OnShutdown();
    return 0;
}

} // namespace Galeshi
