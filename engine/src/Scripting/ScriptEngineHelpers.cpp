#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Audio/AudioSystem.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <string>
#include <vector>

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace Galeshi::ScriptEngineInternal {

std::string NormalizeKey(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '-' || c == ' ') c = '_';
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

UIWidgetType UIWidgetTypeFromName(const std::string& value) {
    const std::string key = NormalizeKey(value);
    if (key == "panel") return UIWidgetType::Panel;
    if (key == "text" || key == "label") return UIWidgetType::Text;
    if (key == "button") return UIWidgetType::Button;
    if (key == "image") return UIWidgetType::Image;
    if (key == "slider") return UIWidgetType::Slider;
    if (key == "checkbox") return UIWidgetType::CheckBox;
    if (key == "progress") return UIWidgetType::Progress;
    if (key == "input" || key == "text_input") return UIWidgetType::Input;
    if (key == "dropdown") return UIWidgetType::Dropdown;
    if (key == "scroll_list") return UIWidgetType::ScrollList;
    if (key == "toggle") return UIWidgetType::Toggle;
    if (key == "layout_group") return UIWidgetType::LayoutGroup;
    return UIWidgetType::None;
}


UICanvas* FindCanvasById(Scene& scene, const std::string& id) {
    auto view = scene.Registry().view<UICanvasComponent>();
    for (entt::entity entity : view) {
        auto& canvas = view.get<UICanvasComponent>(entity).canvas;
        if (canvas.canvasId == id) return &canvas;
    }
    return nullptr;
}


UIWidget* FindWidgetById(UICanvas& canvas, const std::string& id) {
    for (UIWidget& widget : canvas.widgets) {
        if (widget.id == id) return &widget;
    }
    return nullptr;
}


std::string LuaValueToString(const sol::object& value) {
    switch (value.get_type()) {
        case sol::type::nil:
        case sol::type::none:
            return "nil";
        case sol::type::boolean:
            return value.as<bool>() ? "true" : "false";
        case sol::type::number: {
            std::ostringstream out;
            out << value.as<double>();
            return out.str();
        }
        case sol::type::string:
            return value.as<std::string>();
        case sol::type::function:
            return "<function>";
        case sol::type::table:
            return "<table>";
        case sol::type::userdata:
            return "<userdata>";
        case sol::type::thread:
            return "<thread>";
        default:
            return "<value>";
    }
}


int AudioBusIndexFromName(const std::string& name) {
    const std::string key = NormalizeKey(name);
    if (key == "master" || key == "0") return static_cast<int>(AudioBus::Master);
    if (key == "music" || key == "1") return static_cast<int>(AudioBus::Music);
    if (key == "sfx" || key == "effects" || key == "2") return static_cast<int>(AudioBus::SFX);
    if (key == "voice" || key == "3") return static_cast<int>(AudioBus::Voice);
    if (key == "ambient" || key == "4") return static_cast<int>(AudioBus::Ambient);
    return -1;
}


const char* WidgetTypeName(UIWidgetType type) {
    switch (type) {
        case UIWidgetType::Panel: return "panel";
        case UIWidgetType::Text: return "text";
        case UIWidgetType::Button: return "button";
        case UIWidgetType::Image: return "image";
        case UIWidgetType::Slider: return "slider";
        case UIWidgetType::CheckBox: return "checkbox";
        case UIWidgetType::Progress: return "progress";
        case UIWidgetType::Input: return "input";
        case UIWidgetType::Dropdown: return "dropdown";
        case UIWidgetType::ScrollList: return "scroll_list";
        case UIWidgetType::Toggle: return "toggle";
        case UIWidgetType::LayoutGroup: return "layout_group";
        case UIWidgetType::None:
        default: return "none";
    }
}


float InterpFactor(float deltaTime, float speed) {
    if (speed <= 0.0f) return 1.0f;
    if (deltaTime <= 0.0f) return 0.0f;
    return std::clamp(1.0f - std::exp(-speed * deltaTime), 0.0f, 1.0f);
}


GaleshiScriptVec3 ToScriptVec3(const glm::vec3& value) {
    return {value.x, value.y, value.z};
}


glm::vec3 FromScriptVec3(const GaleshiScriptVec3& value) {
    return {value.x, value.y, value.z};
}


bool IsCppScriptPath(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension == ".cpp" || extension == ".cc" || extension == ".cxx";
}


bool ReadWorkerPacket(int fd, CppWorkerPacket& packet) {
#if defined(__unix__) || defined(__APPLE__)
    auto* bytes = reinterpret_cast<unsigned char*>(&packet);
    std::size_t offset = 0;
    while (offset < sizeof(packet)) {
        const ssize_t count = read(fd, bytes + offset,
                                   sizeof(packet) - offset);
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
#else
    (void)fd;
    (void)packet;
    return false;
#endif
}

bool WriteWorkerPacket(int fd, const CppWorkerPacket& packet) {
#if defined(__unix__) || defined(__APPLE__)
    const auto* bytes =
        reinterpret_cast<const unsigned char*>(&packet);
    std::size_t offset = 0;
    while (offset < sizeof(packet)) {
        const ssize_t count = write(fd, bytes + offset,
                                    sizeof(packet) - offset);
        if (count <= 0) return false;
        offset += static_cast<std::size_t>(count);
    }
    return true;
#else
    (void)fd;
    (void)packet;
    return false;
#endif
}


#if defined(__unix__) || defined(__APPLE__)
bool CompileCppScript(const std::filesystem::path& source,
                      const std::filesystem::path& output,
                      const std::string& includeRoot,
                      const std::string& projectRoot,
                      std::string& compilerOutput,
                      const std::string& extraInclude,
                      bool syntaxOnly) {
    const char* envCompiler = std::getenv("CXX");
    const std::string compiler =
        envCompiler && *envCompiler ? envCompiler : "g++";

    std::vector<std::string> arguments = {
        compiler,
        "-std=c++20",
        "-O1",
        "-I" + includeRoot,
        "-I" + projectRoot,
        "-I" + source.parent_path().string(),
    };
    if (!extraInclude.empty()) {
        arguments.push_back("-I" + extraInclude);
    }
    if (syntaxOnly) {
        arguments.push_back("-fsyntax-only");
    } else {
        arguments.push_back("-shared");
        arguments.push_back("-fPIC");
        arguments.push_back("-o");
        arguments.push_back(output.string());
    }
    arguments.push_back(source.string());
    std::vector<char*> argv;
    argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) argv.push_back(argument.data());
    argv.push_back(nullptr);

    int pipeFds[2] = {-1, -1};
    if (pipe(pipeFds) != 0) {
        compilerOutput = "Could not create compiler output pipe";
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(pipeFds[0]);
        close(pipeFds[1]);
        compilerOutput = "Could not start compiler process";
        return false;
    }
    if (pid == 0) {
        dup2(pipeFds[1], STDERR_FILENO);
        close(pipeFds[0]);
        close(pipeFds[1]);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    close(pipeFds[1]);
    char buffer[4096];
    bool timedOut = false;
    while (true) {
        pollfd descriptor{pipeFds[0], POLLIN, 0};
        const int ready = poll(&descriptor, 1, 30000);
        if (ready == 0) {
            compilerOutput += "\nCompiler timed out after 30 seconds.";
            kill(pid, SIGKILL);
            timedOut = true;
            break;
        }
        if (ready < 0) break;
        if ((descriptor.revents & (POLLIN | POLLHUP)) == 0) break;
        const ssize_t count = read(pipeFds[0], buffer, sizeof(buffer));
        if (count > 0) {
            compilerOutput.append(buffer, static_cast<std::size_t>(count));
        } else {
            break;
        }
    }
    close(pipeFds[0]);

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        compilerOutput += "\nCould not wait for compiler process";
        return false;
    }
    return !timedOut && WIFEXITED(status) && WEXITSTATUS(status) == 0;
}
#else
bool CompileCppScript(const std::filesystem::path&,
                      const std::filesystem::path&,
                      const std::string&,
                      const std::string&,
                      std::string& compilerOutput) {
    compilerOutput = "C++ script compilation is only supported on Unix";
    return false;
}
#endif



} // namespace Galeshi::ScriptEngineInternal
