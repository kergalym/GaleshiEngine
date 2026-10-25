#pragma once

#include <memory>
#include <string>

namespace Galeshi {

class Renderer;
class InputAxisSystem;

struct ApplicationSpec {
    std::string name   = "GaleshiEngine App";
    int         width  = 1600;
    int         height = 900;
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

protected:
    virtual void OnStart()          {}
    virtual void OnUpdate(float dt) { (void)dt; }
    virtual void OnRender()         {}
    virtual void OnImGui()          {}
    virtual void OnShutdown()       {}
    virtual void OnFileDrop(const std::string& filepath) {}

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
