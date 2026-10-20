namespace Galeshi {
    Renderer::Renderer()  : m_impl(std::make_unique<Impl>()) {}
    Renderer::~Renderer() = default;

    bool Renderer::Init(const RendererSpec& spec) {
        // Initialize PBR shaders and rendering passes
        return true;
    }

    void Renderer::Shutdown() {
        // Shutdown PBR shaders and rendering passes
    }

    void Renderer::BeginFrame() {
        // Begin PBR rendering
    }

    void Renderer::EndFrame() {
        // End PBR rendering
    }

    uint8_t Renderer::NextViewId() {
        const uint8_t id = m_impl->nextViewId++;
        if (m_impl->nextViewId > 250) m_impl->nextViewId = 10;
        return id;
    }

    // Add PBR rendering methods here
}
