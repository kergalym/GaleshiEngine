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

        uint8_t NextViewId();

        // Add PBR rendering methods here
    };
}
