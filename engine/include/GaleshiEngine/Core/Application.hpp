#pragma once

#include <memory>
#include <string>

namespace Galeshi {

class Renderer;

struct ApplicationSpec {
    std::string name   = "GaleshiEngine App";
    int         width  = 1600;
    int         height = 900;
    bool        editor = false;
};

class Application {
public:
    explicit Application(const ApplicationSpec& spec);
    virtual ~Application();

    Application(const Application&)            = delete;
    Application& operator=(const Application&) = delete;

    int Run();

    Renderer* GetRenderer() const;

protected:
    virtual void OnStart()          {}
    virtual void OnUpdate(float dt) { (void)dt; }
    virtual void OnRender()         {}
    virtual void OnImGui()          {}
    virtual void OnShutdown()       {}

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace Galeshi
