#include "GaleshiEditorApp.hpp"
#include "EditorPackage.hpp"

int main(int argc, char** argv) {
    // Install the editor log capture before constructing the application so
    // startup diagnostics are available in the Output Log tab.
    auto editorLogSink = std::make_shared<EditorLogSink>();
    g_editorLogSink = editorLogSink;
    if (auto logger = spdlog::default_logger()) {
        logger->sinks().push_back(editorLogSink);
    }

    std::string packagePath = "";
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--package" && i + 1 < argc) {
            packagePath = argv[i + 1];
        }
    }

    if (!packagePath.empty()) {
        // Unbuffered output so package errors are not lost.
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);
        return PackageProject(packagePath);
    }


    bool debugMode = false;
    bool gameMode = false;
    std::string gameProjectPath;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--debug") debugMode = true;
        if (arg == "--game") {
            gameMode = true;
            if (i + 1 < argc && argv[i + 1][0] != '-') {
                gameProjectPath = argv[++i];
            }
        }
    }
    if (debugMode) {
        spdlog::set_level(spdlog::level::debug);
    }
    // Unbuffered stdout so logs survive crashes.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    GaleshiEditorApp* app =
        new GaleshiEditorApp(debugMode, gameMode, gameProjectPath);
    int exitCode = app->Run();

    std::fflush(stdout);
    std::fflush(stderr);
    std::_Exit(exitCode);
}
