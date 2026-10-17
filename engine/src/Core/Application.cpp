#include "GaleshiEngine/Core/Application.hpp"
#include "GaleshiEngine/Renderer/Renderer.hpp"
#include "GaleshiEngine/Renderer/ImGuiBgfx.hpp"

#include <SDL3/SDL.h>
#include <spdlog/spdlog.h>
#include <imgui.h>
#ifdef IMGUI_ENABLE_FREETYPE
#include "misc/freetype/imgui_freetype.h"
#endif
#include <imgui_impl_sdl3.h>

#include <filesystem>

namespace Galeshi {


static void ApplyBlackTheme() {
    ImGuiStyle& style = ImGui::GetStyle();
    ImVec4* c = style.Colors;

    // Отключаем скругления
    style.WindowRounding    = 0.0f;
    style.ChildRounding     = 0.0f;
    style.FrameRounding     = 2.0f;
    style.PopupRounding     = 0.0f;
    style.ScrollbarRounding = 0.0f;
    style.GrabRounding      = 2.0f;
    style.TabRounding       = 2.0f;

    style.WindowBorderSize  = 1.0f;
    style.FrameBorderSize   = 0.0f;
    style.PopupBorderSize   = 1.0f;

    style.WindowPadding     = ImVec2(8, 8);
    style.FramePadding      = ImVec2(6, 4);
    style.ItemSpacing       = ImVec2(8, 6);
    style.ItemInnerSpacing  = ImVec2(6, 4);
    style.ScrollbarSize     = 12.0f;
    style.GrabMinSize       = 10.0f;

    // --- Чёрная палитра ---
    const ImVec4 bg0  = ImVec4(0.06f, 0.06f, 0.06f, 1.00f);  // фон окон
    const ImVec4 bg1  = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);  // фон фреймов
    const ImVec4 bg2  = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);  // hover
    const ImVec4 bg3  = ImVec4(0.20f, 0.20f, 0.20f, 1.00f);  // active
    const ImVec4 line = ImVec4(0.25f, 0.25f, 0.25f, 1.00f);  // границы
    const ImVec4 txt  = ImVec4(0.90f, 0.90f, 0.90f, 1.00f);  // текст
    const ImVec4 acc  = ImVec4(0.55f, 0.55f, 0.55f, 1.00f);  // акцент (серый)

    c[ImGuiCol_Text]                  = txt;
    c[ImGuiCol_TextDisabled]          = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);
    c[ImGuiCol_WindowBg]              = bg0;
    c[ImGuiCol_ChildBg]               = bg0;
    c[ImGuiCol_PopupBg]               = bg1;
    c[ImGuiCol_Border]                = line;
    c[ImGuiCol_BorderShadow]          = ImVec4(0, 0, 0, 0);

    c[ImGuiCol_FrameBg]               = bg1;
    c[ImGuiCol_FrameBgHovered]        = bg2;
    c[ImGuiCol_FrameBgActive]         = bg3;

    c[ImGuiCol_TitleBg]               = bg0;
    c[ImGuiCol_TitleBgActive]         = bg1;
    c[ImGuiCol_TitleBgCollapsed]      = bg0;
    c[ImGuiCol_MenuBarBg]             = bg0;

    c[ImGuiCol_ScrollbarBg]           = bg0;
    c[ImGuiCol_ScrollbarGrab]         = bg2;
    c[ImGuiCol_ScrollbarGrabHovered]  = bg3;
    c[ImGuiCol_ScrollbarGrabActive]   = acc;

    c[ImGuiCol_CheckMark]             = acc;
    c[ImGuiCol_SliderGrab]            = acc;
    c[ImGuiCol_SliderGrabActive]      = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);

    c[ImGuiCol_Button]                = bg1;
    c[ImGuiCol_ButtonHovered]         = bg2;
    c[ImGuiCol_ButtonActive]          = bg3;

    c[ImGuiCol_Header]                = bg1;
    c[ImGuiCol_HeaderHovered]         = bg2;
    c[ImGuiCol_HeaderActive]          = bg3;

    c[ImGuiCol_Separator]             = line;
    c[ImGuiCol_SeparatorHovered]      = acc;
    c[ImGuiCol_SeparatorActive]       = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);

    c[ImGuiCol_ResizeGrip]            = bg2;
    c[ImGuiCol_ResizeGripHovered]     = acc;
    c[ImGuiCol_ResizeGripActive]      = ImVec4(0.75f, 0.75f, 0.75f, 1.00f);

    c[ImGuiCol_Tab]                   = bg0;
    c[ImGuiCol_TabHovered]            = bg2;
#if IMGUI_VERSION_NUM >= 19200
    c[ImGuiCol_TabSelected]           = bg1;
    c[ImGuiCol_TabDimmed]             = bg0;
    c[ImGuiCol_TabDimmedSelected]     = bg1;
#else
    c[ImGuiCol_TabActive]             = bg1;
    c[ImGuiCol_TabUnfocused]          = bg0;
    c[ImGuiCol_TabUnfocusedActive]    = bg1;
#endif

    c[ImGuiCol_DockingPreview]        = ImVec4(0.55f, 0.55f, 0.55f, 0.70f);
    c[ImGuiCol_DockingEmptyBg]        = ImVec4(0.03f, 0.03f, 0.03f, 1.00f);

    c[ImGuiCol_PlotLines]             = acc;
    c[ImGuiCol_PlotLinesHovered]      = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);
    c[ImGuiCol_PlotHistogram]         = acc;
    c[ImGuiCol_PlotHistogramHovered]  = ImVec4(0.85f, 0.85f, 0.85f, 1.00f);

    c[ImGuiCol_TableHeaderBg]         = bg1;
    c[ImGuiCol_TableBorderStrong]     = line;
    c[ImGuiCol_TableBorderLight]      = bg2;
    c[ImGuiCol_TableRowBg]            = ImVec4(0, 0, 0, 0);
    c[ImGuiCol_TableRowBgAlt]         = ImVec4(1, 1, 1, 0.03f);

    c[ImGuiCol_TextSelectedBg]        = ImVec4(0.55f, 0.55f, 0.55f, 0.35f);
    c[ImGuiCol_DragDropTarget]        = ImVec4(0.85f, 0.85f, 0.85f, 0.90f);
    c[ImGuiCol_NavHighlight]          = acc;
    c[ImGuiCol_NavWindowingHighlight] = ImVec4(0.85f, 0.85f, 0.85f, 0.70f);
    c[ImGuiCol_NavWindowingDimBg]     = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
    c[ImGuiCol_ModalWindowDimBg]      = ImVec4(0.00f, 0.00f, 0.00f, 0.55f);
}

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

    // Пытаемся найти TTF шрифт в системе
    ImFontConfig font_cfg;
    font_cfg.OversampleH = 1;
    font_cfg.OversampleV = 1;
            font_cfg.PixelSnapH = true;

#ifdef IMGUI_ENABLE_FREETYPE
    font_cfg.FontLoaderFlags = ImGuiFreeTypeLoaderFlags_NoHinting;
    spdlog::info("[Galeshi] FreeType enabled (LightHinting)");
#endif

    // Устанавливаем RasterizerDensity в зависимости от DPI
    float dpi_scale = 1.0f;
    if (SDL_Window* win = m_impl->window) {
        int w, h, pw, ph;
        SDL_GetWindowSize(win, &w, &h);
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        if (w > 0 && h > 0) {
            dpi_scale = (float)pw / (float)w;
        }
    }
    font_cfg.RasterizerDensity = dpi_scale;
    spdlog::info("[Galeshi] Font RasterizerDensity = {:.2f}", dpi_scale);
    
    // Диагностика размеров окна
    if (SDL_Window* win = m_impl->window) {
        int w, h, pw, ph;
        SDL_GetWindowSize(win, &w, &h);
        SDL_GetWindowSizeInPixels(win, &pw, &ph);
        spdlog::info("[Galeshi] Window size: {}x{}, pixels: {}x{}", w, h, pw, ph);
    }

    const char* fontPaths[] = {
        "/usr/share/fonts/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
        "/usr/share/fonts/TTF/Ubuntu-R.ttf",
        "/usr/share/fonts/truetype/ubuntu/Ubuntu-R.ttf",
        "/usr/share/fonts/liberation-sans/LiberationSans-Regular.ttf"
    };

    bool fontLoaded = false;
    for (const char* p : fontPaths) {
        if (std::filesystem::exists(p)) {
            io.Fonts->AddFontFromFileTTF(p, 32.0f, &font_cfg);
            spdlog::info("[Galeshi] Font loaded: {}", p);
            fontLoaded = true;
            io.FontGlobalScale = 0.5f; // Суперсэмплинг: рендерим 32px, показываем 16px
            break;
        }
    }
    if (!fontLoaded) {
        spdlog::warn("[Galeshi] No TTF font found. Install media-fonts/dejavu");
    }

    // Загружаем TTF шрифт для сглаживания
    std::string fontPath = "/usr/share/fonts/TTF/DejaVuSans.ttf";
    if (!std::filesystem::exists(fontPath)) fontPath = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
    if (!std::filesystem::exists(fontPath)) fontPath = "/usr/share/fonts/TTF/Ubuntu-R.ttf";
    if (std::filesystem::exists(fontPath)) {
        io.Fonts->AddFontFromFileTTF(fontPath.c_str(), 16.0f);
    }
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // // io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // Отключено для HiDPI // Отключено для корректного DPI
    ApplyBlackTheme();

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
        
        // Обновляем размеры для корректного DPI/масштабирования
        ImGuiIO& io = ImGui::GetIO();
        int w, h;
        SDL_GetWindowSize(m_impl->window, &w, &h);
        int pw, ph;
        SDL_GetWindowSizeInPixels(m_impl->window, &pw, &ph);
        if (w > 0 && h > 0) {
            io.DisplaySize = ImVec2((float)w, (float)h);
            io.DisplayFramebufferScale = ImVec2((float)pw / (float)w, (float)ph / (float)h);
        }
        
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
