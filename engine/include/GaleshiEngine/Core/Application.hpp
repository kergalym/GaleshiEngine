#pragma once

#include <memory>
#include <string>

namespace Galeshi {

class Renderer;
class InputAxisSystem;

struct ApplicationSpec {
    std::string name   = "GaleshiEngine App";
    int         width  = 1920; // <= 0 uses the primary monitor width
    int         height = 1080; // <= 0 uses the primary monitor height
    bool        editor = false;
    bool        debug  = false;
    // Optional startup splash image, relative to the executable directory.
    std::string splashImage;
};

class Application {
public:
    explicit Application(const ApplicationSpec& spec);
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    int Run();
    void RequestExit();

    Renderer* GetRenderer() const;
    InputAxisSystem& GetInput();
    // Keeps the OS cursor inside the window while the editor is mouse-looking.
    void SetMouseCapture(bool enabled);

protected:
    virtual void OnStart()          {}
    virtual void OnUpdate(float dt) { (void)dt; }
    virtual void OnRender()         {}
    virtual void OnImGui()          {}
    virtual void OnShutdown()       {}
    // x/y are window-relative drop coordinates, matching ImGui's
    // single-viewport mouse coordinate space.
    virtual void OnFileDrop(const std::string& filepath, float x = 0.0f,
                            float y = 0.0f) {
        (void)filepath;
        (void)x;
        (void)y;
    }

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
