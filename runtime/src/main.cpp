#include "GaleshiEngine/Core/Application.hpp"

class GaleshiRuntimeApp final : public Galeshi::Application {
public:
    GaleshiRuntimeApp()
        : Galeshi::Application({
              .name = "GaleshiRuntime", .width = 1280, .height = 720, .editor = false}) {}
};

int main(int, char**) {
    GaleshiRuntimeApp app;
    return app.Run();
}