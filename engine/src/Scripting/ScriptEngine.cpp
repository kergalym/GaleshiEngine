#include "GaleshiEngine/Scripting/ScriptEngine.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"
#include "GaleshiEngine/Scene/Components.hpp"
#include "GaleshiEngine/Animation/AnimationSystem.hpp"
#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include "GaleshiEngine/Assets/Material.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include <spdlog/spdlog.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include <filesystem>
#include <fstream>
#include <limits>
#include <map>
#include <set>
#include <sstream>
#include <utility>

#if defined(__unix__) || defined(__APPLE__)
#include <poll.h>
#include <signal.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__linux__)
#include <sys/prctl.h>
#endif
#endif

namespace Galeshi {

namespace {

std::string NormalizeKey(std::string value) {
    std::string out;
    out.reserve(value.size());
    for (char c : value) {
        if (c == '-' || c == ' ') c = '_';
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

struct PostProcessBoolField {
    const char* name;
    bool PostProcessSettings::* member;
};
struct PostProcessFloatField {
    const char* name;
    float PostProcessSettings::* member;
};
struct PostProcessIntField {
    const char* name;
    int PostProcessSettings::* member;
};
struct PostProcessVec3Field {
    const char* name;
    glm::vec3 PostProcessSettings::* member;
};

constexpr PostProcessBoolField kPostProcessBools[] = {
    {"bloom", &PostProcessSettings::bloomEnabled},
    {"dof", &PostProcessSettings::dofEnabled},
    {"dof_auto_focus", &PostProcessSettings::dofAutoFocus},
    {"motion_blur", &PostProcessSettings::motionBlurEnabled},
    {"environment_palette",
     &PostProcessSettings::environmentPaletteEnabled},
    {"ssao", &PostProcessSettings::ssaoEnabled},
    {"ssr", &PostProcessSettings::ssrEnabled},
    {"ssgi", &PostProcessSettings::ssgiEnabled},
    {"fxaa", &PostProcessSettings::fxaaEnabled},
    {"smaa", &PostProcessSettings::smaaEnabled},
    {"taa", &PostProcessSettings::taaEnabled},
    {"god_rays", &PostProcessSettings::godRaysEnabled},
    {"atmosphere", &PostProcessSettings::atmosphereEnabled},
    {"fog", &PostProcessSettings::fogEnabled},
    {"fog_volumetric", &PostProcessSettings::fogVolumetric},
    {"invert", &PostProcessSettings::invertEnabled},
};

constexpr PostProcessFloatField kPostProcessFloats[] = {
    {"bloom_threshold", &PostProcessSettings::bloomThreshold},
    {"bloom_intensity", &PostProcessSettings::bloomIntensity},
    {"bloom_radius", &PostProcessSettings::bloomRadius},
    {"dof_focus_distance", &PostProcessSettings::dofFocusDistance},
    {"dof_blur", &PostProcessSettings::dofBlur},
    {"motion_blur_amount", &PostProcessSettings::motionBlurAmount},
    {"motion_blur_max_velocity",
     &PostProcessSettings::motionBlurMaxVelocity},
    {"exposure", &PostProcessSettings::exposure},
    {"contrast", &PostProcessSettings::contrast},
    {"saturation", &PostProcessSettings::saturation},
    {"color_exposure", &PostProcessSettings::colorExposure},
    {"color_contrast", &PostProcessSettings::colorContrast},
    {"color_saturation", &PostProcessSettings::colorSaturation},
    {"environment_palette_amount",
     &PostProcessSettings::environmentPaletteAmount},
    {"vignette_intensity", &PostProcessSettings::vignetteIntensity},
    {"vignette_smoothness", &PostProcessSettings::vignetteSmoothness},
    {"chromatic_aberration",
     &PostProcessSettings::chromaticAberration},
    {"ssao_radius", &PostProcessSettings::ssaoRadius},
    {"ssao_bias", &PostProcessSettings::ssaoBias},
    {"ssao_max_distance", &PostProcessSettings::ssaoMaxDistance},
    {"ssao_strength", &PostProcessSettings::ssaoStrength},
    {"ao_resolution_scale",
     &PostProcessSettings::aoResolutionScale},
    {"ao_radius", &PostProcessSettings::aoRadius},
    {"ao_bias", &PostProcessSettings::aoBias},
    {"ao_thickness", &PostProcessSettings::aoThickness},
    {"ao_max_distance", &PostProcessSettings::aoMaxDistance},
    {"ao_strength", &PostProcessSettings::aoStrength},
    {"ssr_intensity", &PostProcessSettings::ssrIntensity},
    {"ssgi_intensity", &PostProcessSettings::ssgiIntensity},
    {"ssgi_radius", &PostProcessSettings::ssgiRadius},
    {"ssgi_thickness", &PostProcessSettings::ssgiThickness},
    {"taa_blend", &PostProcessSettings::taaBlend},
    {"god_rays_intensity",
     &PostProcessSettings::godRaysIntensity},
    {"god_rays_decay", &PostProcessSettings::godRaysDecay},
    {"god_rays_density", &PostProcessSettings::godRaysDensity},
    {"god_rays_weight", &PostProcessSettings::godRaysWeight},
    {"atmosphere_intensity",
     &PostProcessSettings::atmosphereIntensity},
    {"fog_density", &PostProcessSettings::fogDensity},
    {"fog_height_falloff",
     &PostProcessSettings::fogHeightFalloff},
    {"fog_base_height", &PostProcessSettings::fogBaseHeight},
    {"fog_start_distance",
     &PostProcessSettings::fogStartDistance},
    {"fog_max_distance", &PostProcessSettings::fogMaxDistance},
    {"fog_sun_scattering",
     &PostProcessSettings::fogSunScattering},
    {"fog_anisotropy", &PostProcessSettings::fogAnisotropy},
};

constexpr PostProcessIntField kPostProcessInts[] = {
    {"ao_directions", &PostProcessSettings::aoDirections},
    {"ao_steps", &PostProcessSettings::aoSteps},
    {"ssgi_rays", &PostProcessSettings::ssgiRays},
    {"ssgi_steps", &PostProcessSettings::ssgiSteps},
    {"fog_steps", &PostProcessSettings::fogSteps},
};

constexpr PostProcessVec3Field kPostProcessVec3s[] = {
    {"environment_palette_shadow",
     &PostProcessSettings::environmentPaletteShadow},
    {"environment_palette_mid",
     &PostProcessSettings::environmentPaletteMid},
    {"environment_palette_highlight",
     &PostProcessSettings::environmentPaletteHighlight},
    {"fog_color", &PostProcessSettings::fogColor},
};

template <typename T, std::size_t N>
const T* FindPostProcessField(const T (&fields)[N],
                              const std::string& name) {
    for (const T& field : fields) {
        if (name == field.name) return &field;
    }
    return nullptr;
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

struct UIBoolField {
    const char* name;
    bool UIWidget::* member;
};
struct UIFloatField {
    const char* name;
    float UIWidget::* member;
};
struct UIIntField {
    const char* name;
    int UIWidget::* member;
};
struct UIStringField {
    const char* name;
    std::string UIWidget::* member;
};
struct UIVec2Field {
    const char* name;
    glm::vec2 UIWidget::* member;
};
struct UIVec4Field {
    const char* name;
    glm::vec4 UIWidget::* member;
};

constexpr UIBoolField kUIBools[] = {
    {"visible", &UIWidget::visible},
    {"enabled", &UIWidget::enabled},
    {"interactable", &UIWidget::interactable},
    {"block_input", &UIWidget::blockInput},
    {"checked", &UIWidget::checked},
    {"use_color", &UIWidget::useColor},
    {"use_border_color", &UIWidget::useBorderColor},
};
constexpr UIFloatField kUIFloats[] = {
    {"opacity", &UIWidget::opacity},
    {"button_height", &UIWidget::buttonHeight},
    {"slider_min", &UIWidget::sliderMin},
    {"slider_max", &UIWidget::sliderMax},
    {"value", &UIWidget::sliderValue},
    {"slider_step", &UIWidget::sliderStep},
    {"font_size", &UIWidget::fontSize},
    {"text_wrap_width", &UIWidget::textWrapWidth},
    {"radius", &UIWidget::radius},
    {"border", &UIWidget::border},
};
constexpr UIIntField kUIInts[] = {
    {"parent", &UIWidget::parent},
    {"z_order", &UIWidget::zOrder},
    {"selected_option", &UIWidget::selectedOption},
    {"max_chars", &UIWidget::maxChars},
};
constexpr UIStringField kUIStrings[] = {
    {"label", &UIWidget::label},
    {"tooltip", &UIWidget::tooltip},
    {"image", &UIWidget::imagePath},
    {"on_click", &UIWidget::onClick},
    {"on_hover", &UIWidget::onHover},
    {"on_show", &UIWidget::onShow},
};
constexpr UIVec2Field kUIVec2s[] = {
    {"position", &UIWidget::position},
    {"size", &UIWidget::size},
    {"pivot", &UIWidget::pivot},
    {"anchor_min", &UIWidget::anchorMin},
    {"anchor_max", &UIWidget::anchorMax},
};
constexpr UIVec4Field kUIVec4s[] = {
    {"color", &UIWidget::color},
    {"border_color", &UIWidget::borderColor},
};

struct CanvasBoolField {
    const char* name;
    bool UICanvas::* member;
};
struct CanvasFloatField {
    const char* name;
    float UICanvas::* member;
};
struct CanvasStringField {
    const char* name;
    std::string UICanvas::* member;
};
struct CanvasVec2Field {
    const char* name;
    glm::vec2 UICanvas::* member;
};
constexpr CanvasBoolField kCanvasBools[] = {
    {"visible", &UICanvas::visible},
    {"resizable", &UICanvas::resizable},
    {"movable", &UICanvas::movable},
    {"closeable", &UICanvas::closeable},
};
constexpr CanvasFloatField kCanvasFloats[] = {
    {"background_alpha", &UICanvas::backgroundAlpha},
    {"match_width_or_height", &UICanvas::matchWidthOrHeight},
};
constexpr CanvasStringField kCanvasStrings[] = {
    {"title", &UICanvas::title},
    {"on_menu_open", &UICanvas::onMenuOpen},
    {"on_menu_hover", &UICanvas::onMenuHover},
    {"on_menu_click", &UICanvas::onMenuClick},
};
constexpr CanvasVec2Field kCanvasVec2s[] = {
    {"screen_position", &UICanvas::screenPosition},
    {"screen_size", &UICanvas::screenSize},
};

struct VegetationBoolField {
    const char* name;
    bool VegetationComponent::* member;
};
struct VegetationIntField {
    const char* name;
    int VegetationComponent::* member;
};
struct VegetationFloatField {
    const char* name;
    float VegetationComponent::* member;
};
constexpr VegetationBoolField kVegetationBools[] = {
    {"enabled", &VegetationComponent::enabled},
    {"cast_shadows", &VegetationComponent::castShadows},
    {"grass_cast_shadows", &VegetationComponent::grassCastShadows},
    {"hide_default_instances",
     &VegetationComponent::hideDefaultInstances},
};
constexpr VegetationIntField kVegetationInts[] = {
    {"tree_count", &VegetationComponent::treeCount},
    {"grass_count", &VegetationComponent::grassCount},
};
constexpr VegetationFloatField kVegetationFloats[] = {
    {"tree_min_scale", &VegetationComponent::treeMinScale},
    {"tree_max_scale", &VegetationComponent::treeMaxScale},
    {"grass_min_scale", &VegetationComponent::grassMinScale},
    {"grass_max_scale", &VegetationComponent::grassMaxScale},
    {"slope_limit", &VegetationComponent::slopeLimit},
    {"paint_density", &VegetationComponent::paintDensity},
    {"paint_size", &VegetationComponent::paintSize},
};

struct ParticleBoolField {
    const char* name;
    bool ParticleEmitterComponent::* member;
};
struct ParticleIntField {
    const char* name;
    uint32_t ParticleEmitterComponent::* member;
};
struct ParticleFloatField {
    const char* name;
    float ParticleEmitterComponent::* member;
};
struct ParticleVec3Field {
    const char* name;
    glm::vec3 ParticleEmitterComponent::* member;
};
struct ParticleVec4Field {
    const char* name;
    glm::vec4 ParticleEmitterComponent::* member;
};
constexpr ParticleBoolField kParticleBools[] = {
    {"enabled", &ParticleEmitterComponent::enabled},
};
constexpr ParticleIntField kParticleInts[] = {
    {"max_particles", &ParticleEmitterComponent::maxParticles},
};
constexpr ParticleFloatField kParticleFloats[] = {
    {"emit_rate", &ParticleEmitterComponent::emitRate},
    {"lifetime", &ParticleEmitterComponent::lifetime},
    {"lifetime_variance", &ParticleEmitterComponent::lifetimeVariance},
    {"start_size", &ParticleEmitterComponent::startSize},
    {"end_size", &ParticleEmitterComponent::endSize},
    {"velocity_variance", &ParticleEmitterComponent::velocityVariance},
    {"spawn_radius", &ParticleEmitterComponent::spawnRadius},
    {"drag", &ParticleEmitterComponent::drag},
};
constexpr ParticleVec3Field kParticleVec3s[] = {
    {"velocity", &ParticleEmitterComponent::velocity},
    {"spawn_box_size", &ParticleEmitterComponent::spawnBoxSize},
    {"gravity", &ParticleEmitterComponent::gravity},
};
constexpr ParticleVec4Field kParticleVec4s[] = {
    {"start_color", &ParticleEmitterComponent::startColor},
    {"end_color", &ParticleEmitterComponent::endColor},
};

struct GroomBoolField {
    const char* name;
    bool GroomComponent::* member;
};
struct GroomIntField {
    const char* name;
    int GroomComponent::* member;
};
struct GroomFloatField {
    const char* name;
    float GroomComponent::* member;
};
struct GroomVec3Field {
    const char* name;
    glm::vec3 GroomComponent::* member;
};
struct GroomStringField {
    const char* name;
    std::string GroomComponent::* member;
};
constexpr GroomBoolField kGroomBools[] = {
    {"enabled", &GroomComponent::enabled},
    {"simulate", &GroomComponent::simulate},
    {"visible_in_editor", &GroomComponent::visibleInEditor},
};
constexpr GroomIntField kGroomInts[] = {
    {"guide_count", &GroomComponent::guideCount},
    {"strands_per_guide", &GroomComponent::strandsPerGuide},
    {"segments", &GroomComponent::segments},
};
constexpr GroomFloatField kGroomFloats[] = {
    {"length", &GroomComponent::length},
    {"width", &GroomComponent::width},
    {"root_width", &GroomComponent::rootWidth},
    {"tip_width", &GroomComponent::tipWidth},
    {"clump_radius", &GroomComponent::clumpRadius},
    {"clumping", &GroomComponent::clumping},
    {"frizz", &GroomComponent::frizz},
    {"curl", &GroomComponent::curl},
    {"volume", &GroomComponent::volume},
    {"scalp_start", &GroomComponent::scalpStart},
    {"scalp_end", &GroomComponent::scalpEnd},
    {"normal_threshold", &GroomComponent::normalThreshold},
    {"gravity", &GroomComponent::gravity},
    {"stiffness", &GroomComponent::stiffness},
    {"damping", &GroomComponent::damping},
    {"wind_strength", &GroomComponent::windStrength},
    {"wind_frequency", &GroomComponent::windFrequency},
    {"specular", &GroomComponent::specular},
    {"shininess", &GroomComponent::shininess},
    {"ambient", &GroomComponent::ambient},
    {"opacity", &GroomComponent::opacity},
    {"random_tint", &GroomComponent::randomTint},
};
constexpr GroomVec3Field kGroomVec3s[] = {
    {"groom_direction", &GroomComponent::groomDirection},
    {"wind_direction", &GroomComponent::windDirection},
    {"root_color", &GroomComponent::rootColor},
    {"tip_color", &GroomComponent::tipColor},
};
constexpr GroomStringField kGroomStrings[] = {
    {"target_mesh_name", &GroomComponent::targetMeshName},
};

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
                      const std::string& extraInclude = {},
                      bool syntaxOnly = false) {
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

} // namespace

ScriptEngine::ScriptEngine() {
    m_lua.open_libraries(sol::lib::base, sol::lib::math,
                         sol::lib::table, sol::lib::string);
    BindFunctions();
#if defined(__unix__) || defined(__APPLE__)
    // A dead worker must not terminate the editor with SIGPIPE.
    signal(SIGPIPE, SIG_IGN);
#endif
}

ScriptEngine::~ScriptEngine() {
    StopCppWorker();
}

void ScriptEngine::AddLog(const std::string& msg) {
    m_logs.push_back(msg);
    if (m_logs.size() > 1000) m_logs.erase(m_logs.begin());
    spdlog::info("[Script] {}", msg);
}

bool ScriptEngine::RunScript(const std::string& filepath) {
    try {
        m_lua.script_file(filepath);
        // A fresh script (including a hot reload) gets its own RunOnce pass.
        m_runOnceInvoked = false;
        AddLog("Ran script: " + filepath);
        return true;
    } catch (const sol::error& e) {
        AddLog(std::string("ERROR: ") + e.what());
        return false;
    }
}

void ScriptEngine::BuildApi() {
    if (m_apiBuilt) return;
    m_apiBuilt = true;

    m_api = m_lua.create_table();
    m_lua["Galeshi"] = m_api;
    m_api["version"] = "0.3.0";

    // ------------------------------------------------------------------
    // Small conversion helpers shared by all modules.
    // ------------------------------------------------------------------
    auto readNumber = [](const sol::object& value, float& out) {
        if (value.get_type() != sol::type::number) return false;
        out = static_cast<float>(value.as<double>());
        return true;
    };

    auto readBool = [](const sol::object& value, bool& out) {
        if (value.get_type() != sol::type::boolean) return false;
        out = value.as<bool>();
        return true;
    };

    auto readString = [](const sol::object& value, std::string& out) {
        if (value.get_type() != sol::type::string) return false;
        out = value.as<std::string>();
        return true;
    };

    auto readVec2 = [readNumber](const sol::object& value, glm::vec2& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float x = 0.0f;
        float y = 0.0f;
        const sol::object xo = table.get<sol::object>("x");
        const sol::object yo = table.get<sol::object>("y");
        if (!readNumber(xo, x)) {
            const sol::object xi = table.get<sol::object>(1);
            if (!readNumber(xi, x)) return false;
        }
        if (!readNumber(yo, y)) {
            const sol::object yi = table.get<sol::object>(2);
            if (!readNumber(yi, y)) return false;
        }
        out = {x, y};
        return true;
    };

    auto readVec3 = [readNumber](const sol::object& value, glm::vec3& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float components[3] = {0.0f, 0.0f, 0.0f};
        const char* names[3] = {"x", "y", "z"};
        for (int i = 0; i < 3; ++i) {
            const sol::object named = table.get<sol::object>(names[i]);
            if (!readNumber(named, components[i])) {
                const sol::object indexed = table.get<sol::object>(i + 1);
                if (!readNumber(indexed, components[i])) return false;
            }
        }
        out = {components[0], components[1], components[2]};
        return true;
    };

    auto readVec4 = [readNumber](const sol::object& value, glm::vec4& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float components[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const char* names[4] = {"x", "y", "z", "w"};
        for (int i = 0; i < 4; ++i) {
            const sol::object named = table.get<sol::object>(names[i]);
            if (!readNumber(named, components[i])) {
                const sol::object indexed = table.get<sol::object>(i + 1);
                if (!readNumber(indexed, components[i])) {
                    // RGB color helpers may omit alpha; default it to opaque.
                    if (i == 3) {
                        components[3] = 1.0f;
                        continue;
                    }
                    return false;
                }
            }
        }
        out = {components[0], components[1], components[2], components[3]};
        return true;
    };

    auto readQuat = [readNumber](const sol::object& value, glm::quat& out) {
        if (value.get_type() != sol::type::table) return false;
        sol::table table = value.as<sol::table>();
        float components[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        const char* names[4] = {"x", "y", "z", "w"};
        for (int i = 0; i < 4; ++i) {
            const sol::object named = table.get<sol::object>(names[i]);
            if (!readNumber(named, components[i])) {
                const sol::object indexed = table.get<sol::object>(i + 1);
                if (!readNumber(indexed, components[i])) return false;
            }
        }
        out = glm::quat(components[3], components[0], components[1], components[2]);
        return true;
    };

    auto readVec3Args = [readVec3, readNumber](sol::variadic_args args,
                                               glm::vec3& out) {
        if (args.size() == 1) {
            return readVec3(args[0], out);
        }
        if (args.size() < 3) return false;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;
        if (!readNumber(args[0], x) || !readNumber(args[1], y) ||
            !readNumber(args[2], z)) {
            return false;
        }
        out = {x, y, z};
        return true;
    };

    auto makeVec2 = [this](const glm::vec2& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table["x"] = value.x;
        table["y"] = value.y;
        return table;
    };

    auto makeVec3 = [this](const glm::vec3& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table[3] = value.z;
        table["x"] = value.x;
        table["y"] = value.y;
        table["z"] = value.z;
        return table;
    };

    auto makeVec4 = [this](const glm::vec4& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table[3] = value.z;
        table[4] = value.w;
        table["x"] = value.x;
        table["y"] = value.y;
        table["z"] = value.z;
        table["w"] = value.w;
        return table;
    };

    auto makeQuat = [this](const glm::quat& value) {
        sol::table table = m_lua.create_table();
        table[1] = value.x;
        table[2] = value.y;
        table[3] = value.z;
        table[4] = value.w;
        table["x"] = value.x;
        table["y"] = value.y;
        table["z"] = value.z;
        table["w"] = value.w;
        return table;
    };

    // ------------------------------------------------------------------
    // Logging and math helpers.
    // ------------------------------------------------------------------
    auto printArgs = [this](sol::variadic_args args) {
        std::string out;
        for (auto value : args) {
            if (!out.empty()) out += ' ';
            out += LuaValueToString(value);
        }
        AddLog(out);
    };

    const auto logFn = [this](const std::string& message) { AddLog(message); };
    const auto warnFn = [this](const std::string& message) {
        AddLog(std::string("WARN: ") + message);
        spdlog::warn("[Lua] {}", message);
    };
    const auto errorFn = [this](const std::string& message) {
        AddLog(std::string("ERROR: ") + message);
        spdlog::error("[Lua] {}", message);
    };

    m_api["log"] = logFn;
    m_api["warn"] = warnFn;
    m_api["error"] = errorFn;
    m_api["print"] = printArgs;
    m_lua.set_function("log", logFn);
    m_lua.set_function("print", printArgs);

    m_api["vec2"] = [makeVec2](float x, float y) {
        return makeVec2(glm::vec2{x, y});
    };
    m_api["vec3"] = [makeVec3](float x, float y, float z) {
        return makeVec3(glm::vec3{x, y, z});
    };
    m_api["color"] = [makeVec4](float r, float g, float b,
                                sol::optional<float> a) {
        return makeVec4(glm::vec4{r, g, b, a.value_or(1.0f)});
    };
    m_api["quat"] = [makeQuat](float x, float y, float z,
                               sol::optional<float> w) {
        return makeQuat(glm::quat(w.value_or(1.0f), x, y, z));
    };

    auto fInterp = [](float current, float target, float deltaTime,
                      float speed) {
        return current + (target - current) *
                             InterpFactor(deltaTime, speed);
    };
    auto vInterp = [readVec3, makeVec3](
                       const sol::object& current,
                       const sol::object& target,
                       float deltaTime,
                       float speed) {
        glm::vec3 from{0.0f};
        glm::vec3 to{0.0f};
        if (!readVec3(current, from) || !readVec3(target, to)) {
            return makeVec3(glm::vec3{0.0f});
        }
        const float t = InterpFactor(deltaTime, speed);
        return makeVec3(from + (to - from) * t);
    };

    sol::table math = m_lua.create_table();
    math["FInterp"] = fInterp;
    math["f_interp"] = fInterp;
    math["VInterp"] = vInterp;
    math["v_interp"] = vInterp;
    m_api["math"] = math;
    m_api["FInterp"] = fInterp;
    m_api["f_interp"] = fInterp;
    m_api["VInterp"] = vInterp;
    m_api["v_interp"] = vInterp;
    m_lua.set_function("FInterp", fInterp);
    m_lua.set_function("f_interp", fInterp);
    m_lua.set_function("VInterp", vInterp);
    m_lua.set_function("v_interp", vInterp);

    sol::table world = m_lua.create_table();
    sol::table input = m_lua.create_table();
    sol::table physics = m_lua.create_table();
    sol::table audio = m_lua.create_table();
    sol::table animation = m_lua.create_table();
    sol::table ai = m_lua.create_table();
    sol::table navigation = m_lua.create_table();
    sol::table ui = m_lua.create_table();
    sol::table particles = m_lua.create_table();
    sol::table engine = m_lua.create_table();
    sol::table camera = m_lua.create_table();
    sol::table player = m_lua.create_table();
    sol::table shader = m_lua.create_table();
    m_api["world"] = world;
    m_api["input"] = input;
    m_api["physics"] = physics;
    m_api["audio"] = audio;
    m_api["animation"] = animation;
    m_api["ai"] = ai;
    m_api["navigation"] = navigation;
    m_api["ui"] = ui;
    m_api["particles"] = particles;
    m_api["engine"] = engine;
    m_api["camera"] = camera;
    m_api["player"] = player;
    m_api["shader"] = shader;

    // ------------------------------------------------------------------
    // Entity handle helpers.  Handles are lightweight Lua tables with an
    // `id` field and a shared metatable containing colon-callable methods.
    // ------------------------------------------------------------------
    auto validId = [this](uint32_t id) {
        if (!m_scene) return false;
        return m_scene->Registry().valid(static_cast<entt::entity>(id));
    };

    auto idFromValue = [](const sol::object& value) -> uint32_t {
        if (value.get_type() == sol::type::number) {
            const double number = value.as<double>();
            if (number < 0.0 || number > 4294967295.0) {
                return static_cast<uint32_t>(entt::null);
            }
            return static_cast<uint32_t>(number);
        }
        if (value.get_type() == sol::type::table) {
            sol::table table = value.as<sol::table>();
            const sol::object id = table.get<sol::object>("id");
            if (id.get_type() == sol::type::number) {
                const double number = id.as<double>();
                if (number < 0.0 || number > 4294967295.0) {
                    return static_cast<uint32_t>(entt::null);
                }
                return static_cast<uint32_t>(number);
            }
        }
        return static_cast<uint32_t>(entt::null);
    };

    auto resolve = [this, idFromValue, validId](const sol::object& value) {
        const uint32_t id = idFromValue(value);
        if (!validId(id)) return entt::entity{entt::null};
        return static_cast<entt::entity>(id);
    };

    auto idFromTable = [idFromValue](const sol::table& table) {
        return idFromValue(sol::object(table));
    };

    auto resolveTable = [this, idFromTable, validId](const sol::table& table) {
        const uint32_t id = idFromTable(table);
        if (!validId(id)) return entt::entity{entt::null};
        return static_cast<entt::entity>(id);
    };

    auto makeEntityTable = [this](uint32_t id) {
        sol::table table = m_lua.create_table();
        table["id"] = id;
        table[sol::metatable_key] = m_entityMeta;
        return table;
    };

    auto makeEntity = [this, makeEntityTable, validId](uint32_t id) -> sol::object {
        if (!validId(id)) return sol::make_object(m_lua, sol::nil);
        return sol::make_object(m_lua, makeEntityTable(id));
    };

    auto getTransform = [this](entt::entity entity, bool create) -> TransformComponent* {
        if (!m_scene || !m_scene->Registry().valid(entity)) return nullptr;
        auto& registry = m_scene->Registry();
        if (registry.all_of<TransformComponent>(entity)) {
            return &registry.get<TransformComponent>(entity);
        }
        if (!create) return nullptr;
        return &registry.emplace<TransformComponent>(entity);
    };

    // Build the shared entity metatable before any handle can be returned.
    sol::table methods = m_lua.create_table();
    m_entityMeta = m_lua.create_table();

    methods["valid"] = [resolveTable](sol::table self) {
        return resolveTable(self) != entt::entity{entt::null};
    };
    methods["get_id"] = [idFromTable](sol::table self) {
        return idFromTable(self);
    };
    methods["name"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return std::string{};
        auto& registry = m_scene->Registry();
        if (!registry.all_of<NameComponent>(entity)) return std::string{};
        return registry.get<NameComponent>(entity).name;
    };
    methods["set_name"] = [this, resolveTable](sol::table self,
                                                const std::string& name) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return self;
        auto& registry = m_scene->Registry();
        if (registry.all_of<NameComponent>(entity)) {
            registry.get<NameComponent>(entity).name = name;
        } else {
            registry.emplace<NameComponent>(entity, NameComponent{name});
        }
        return self;
    };
    methods["position"] = [this, resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->position : glm::vec3(0.0f));
    };
    methods["set_position"] = [resolveTable, getTransform, readVec3Args](
                                  sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 position{0.0f};
        if (entity == entt::null || !readVec3Args(args, position)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position = position;
        }
        return self;
    };
    methods["translate"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 delta{0.0f};
        if (entity == entt::null || !readVec3Args(args, delta)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position += delta;
        }
        return self;
    };
    methods["rotation"] = [this, resolveTable, getTransform, makeQuat](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeQuat(transform ? transform->rotation
                                  : glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    };
    methods["set_rotation"] = [resolveTable, getTransform, readQuat, readNumber](
                                  sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return self;
        glm::quat rotation(1.0f, 0.0f, 0.0f, 0.0f);
        bool ok = false;
        if (args.size() == 1) {
            ok = readQuat(args[0], rotation);
        } else if (args.size() >= 4) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            float w = 1.0f;
            ok = readNumber(args[0], x) && readNumber(args[1], y) &&
                 readNumber(args[2], z) && readNumber(args[3], w);
            if (ok) rotation = glm::quat(w, x, y, z);
        } else if (args.size() >= 3) {
            float x = 0.0f;
            float y = 0.0f;
            float z = 0.0f;
            ok = readNumber(args[0], x) && readNumber(args[1], y) &&
                 readNumber(args[2], z);
            if (ok) rotation = glm::quat(glm::radians(glm::vec3{x, y, z}));
        }
        if (!ok) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->rotation = glm::normalize(rotation);
        }
        return self;
    };
    methods["euler"] = [this, resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        if (!transform) return makeVec3(glm::vec3(0.0f));
        return makeVec3(glm::degrees(glm::eulerAngles(transform->rotation)));
    };
    methods["set_euler"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 euler{0.0f};
        if (entity == entt::null || !readVec3Args(args, euler)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->rotation = glm::normalize(
                glm::quat(glm::radians(euler)));
        }
        return self;
    };
    methods["scale"] = [resolveTable, getTransform, makeVec3](sol::table self) {
        const entt::entity entity = resolveTable(self);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->scale : glm::vec3(1.0f));
    };
    methods["set_scale"] = [resolveTable, getTransform, readVec3Args](
                               sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 scale{1.0f};
        if (entity == entt::null || !readVec3Args(args, scale)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->scale = scale;
        }
        return self;
    };
    methods["scale_by"] = [resolveTable, getTransform, readVec3Args](
                              sol::table self, sol::variadic_args args) {
        const entt::entity entity = resolveTable(self);
        glm::vec3 scale{1.0f};
        if (entity == entt::null || !readVec3Args(args, scale)) return self;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->scale *= scale;
        }
        return self;
    };
    methods["parent"] = [this, resolveTable, makeEntity](sol::table self) -> sol::object {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return sol::make_object(m_lua, sol::nil);
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParentComponent>(entity)) {
            return sol::make_object(m_lua, sol::nil);
        }
        const entt::entity parent = registry.get<ParentComponent>(entity).parent;
        if (parent == entt::null || !registry.valid(parent)) {
            return sol::make_object(m_lua, sol::nil);
        }
        return makeEntity(static_cast<uint32_t>(parent));
    };
    methods["set_parent"] = [this, resolveTable, resolve](sol::table self,
                                                          sol::object parentValue) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        if (parentValue.get_type() == sol::type::nil ||
            parentValue.get_type() == sol::type::none) {
            auto& registry = m_scene->Registry();
            if (registry.all_of<ParentComponent>(entity)) {
                registry.remove<ParentComponent>(entity);
            }
            return true;
        }
        const entt::entity parent = resolve(parentValue);
        if (parent == entt::null || parent == entity) return false;
        auto& registry = m_scene->Registry();
        entt::entity cursor = parent;
        for (int depth = 0; depth < 256 && cursor != entt::null; ++depth) {
            if (cursor == entity) return false;
            if (!registry.all_of<ParentComponent>(cursor)) break;
            cursor = registry.get<ParentComponent>(cursor).parent;
        }
        registry.emplace_or_replace<ParentComponent>(entity,
                                                     ParentComponent{parent});
        return true;
    };
    methods["detach"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParentComponent>(entity)) return false;
        registry.remove<ParentComponent>(entity);
        return true;
    };
    methods["children"] = [this, resolveTable, makeEntityTable](sol::table self) {
        sol::table result = m_lua.create_table();
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return result;
        int index = 1;
        auto view = m_scene->Registry().view<ParentComponent>();
        for (entt::entity child : view) {
            if (view.get<ParentComponent>(child).parent == entity) {
                result[index++] = makeEntityTable(static_cast<uint32_t>(child));
            }
        }
        return result;
    };
    methods["destroy"] = [this, resolveTable](sol::table self) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    };
    methods["has_component"] = [this, resolveTable](sol::table self,
                                                     const std::string& name) {
        const entt::entity entity = resolveTable(self);
        if (entity == entt::null || !m_scene) return false;
        auto& registry = m_scene->Registry();
        const std::string key = NormalizeKey(name);
        if (key == "name") return registry.all_of<NameComponent>(entity);
        if (key == "transform") return registry.all_of<TransformComponent>(entity);
        if (key == "parent") return registry.all_of<ParentComponent>(entity);
        if (key == "mesh") return registry.all_of<MeshComponent>(entity);
        if (key == "material") return registry.all_of<MaterialComponent>(entity);
        if (key == "skeletal") return registry.all_of<SkeletalComponent>(entity);
        if (key == "camera") return registry.all_of<CameraComponent>(entity);
        if (key == "directional_light") return registry.all_of<DirectionalLightComponent>(entity);
        if (key == "point_light") return registry.all_of<PointLightComponent>(entity);
        if (key == "spot_light") return registry.all_of<SpotLightComponent>(entity);
        if (key == "rigid_body" || key == "rigidbody") return registry.all_of<RigidBodyComponent>(entity);
        if (key == "collider") return registry.all_of<ColliderComponent>(entity);
        if (key == "landscape") return registry.all_of<LandscapeComponent>(entity);
        if (key == "vegetation") return registry.all_of<VegetationComponent>(entity);
        if (key == "nav_agent") return registry.all_of<NavAgentComponent>(entity);
        if (key == "soft_body") return registry.all_of<SoftBodyComponent>(entity);
        if (key == "ai_controller") return registry.all_of<AIControllerComponent>(entity);
        if (key == "player_tag") return registry.all_of<PlayerTagComponent>(entity);
        if (key == "perception") return registry.all_of<PerceptionComponent>(entity);
        if (key == "behavior_tree") return registry.all_of<BehaviorTreeComponent>(entity);
        if (key == "audio_source") return registry.all_of<AudioSourceComponent>(entity);
        if (key == "audio_listener") return registry.all_of<AudioListenerComponent>(entity);
        if (key == "post_process_volume") return registry.all_of<PostProcessVolumeComponent>(entity);
        if (key == "particle_emitter") return registry.all_of<ParticleEmitterComponent>(entity);
        if (key == "water") return registry.all_of<WaterComponent>(entity);
        if (key == "groom" || key == "groom_hair") return registry.all_of<GroomComponent>(entity);
        if (key == "ui_canvas") return registry.all_of<UICanvasComponent>(entity);
        if (key == "animation_state_machine") return registry.all_of<AnimationStateMachineComponent>(entity);
        if (key == "lua_entity") return registry.all_of<LuaEntityComponent>(entity);
        return false;
    };

    m_entityMeta[sol::meta_function::index] = methods;
    m_entityMeta[sol::meta_function::to_string] = [this, idFromTable](sol::table self) {
        const uint32_t id = idFromTable(self);
        if (!m_scene || !m_scene->Registry().valid(static_cast<entt::entity>(id))) {
            return std::string("Entity(invalid)");
        }
        auto& registry = m_scene->Registry();
        if (registry.all_of<NameComponent>(static_cast<entt::entity>(id))) {
            return std::string("Entity(") +
                   registry.get<NameComponent>(static_cast<entt::entity>(id)).name +
                   ", id=" + std::to_string(id) + ")";
        }
        return std::string("Entity(id=") + std::to_string(id) + ")";
    };
    m_entityMeta[sol::meta_function::equal_to] = [idFromTable](sol::table lhs,
                                                                sol::table rhs) {
        return idFromTable(lhs) == idFromTable(rhs);
    };

    auto createEntityId = [this](const std::string& name) -> uint32_t {
        if (!m_scene) return static_cast<uint32_t>(entt::null);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        return static_cast<uint32_t>(static_cast<entt::entity>(entity));
    };

    // ------------------------------------------------------------------
    // World / ECS
    // ------------------------------------------------------------------
    world["create"] = [createEntityId, makeEntity](const std::string& name) {
        return makeEntity(createEntityId(name));
    };
    world["entity"] = [makeEntity, idFromValue](sol::object value) {
        return makeEntity(idFromValue(value));
    };
    world["find"] = [this, makeEntity](const std::string& name) {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->FindEntityByName(name);
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };
    world["find_all"] = [this, makeEntityTable](sol::optional<std::string> name) {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<NameComponent>();
        for (entt::entity entity : view) {
            if (name && view.get<NameComponent>(entity).name != *name) continue;
            result[index++] = makeEntityTable(static_cast<uint32_t>(entity));
        }
        return result;
    };
    world["all"] = [this, makeEntityTable](sol::optional<std::string> name) {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<NameComponent>();
        for (entt::entity entity : view) {
            if (name && view.get<NameComponent>(entity).name != *name) continue;
            result[index++] = makeEntityTable(static_cast<uint32_t>(entity));
        }
        return result;
    };
    world["exists"] = [resolve](sol::object value) {
        return resolve(value) != entt::entity{entt::null};
    };
    world["destroy"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    };
    world["count"] = [this]() -> size_t {
        if (!m_scene) return 0;
        return m_scene->Registry().view<NameComponent>().size();
    };
    world["clear_script_entities"] = [this]() {
        ClearLuaEntities();
    };

    // Legacy ECS aliases.
    m_lua.set_function("new_entity", [createEntityId](const std::string& name) {
        return createEntityId(name);
    });
    m_lua.set_function("set_position", [resolve, getTransform, readVec3Args](
                                          sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 position{0.0f};
        if (entity == entt::null || !readVec3Args(args, position)) return;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position = position;
        }
    });
    m_lua.set_function("get_position", [resolve, getTransform, makeVec3](
                                          sol::object value) {
        const entt::entity entity = resolve(value);
        const TransformComponent* transform = getTransform(entity, false);
        return makeVec3(transform ? transform->position : glm::vec3(0.0f));
    });
    m_lua.set_function("translate", [resolve, getTransform, readVec3Args](
                                        sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 delta{0.0f};
        if (entity == entt::null || !readVec3Args(args, delta)) return;
        if (TransformComponent* transform = getTransform(entity, true)) {
            transform->position += delta;
        }
    });
    m_lua.set_function("destroy_entity", [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        DestroyEntity(entity);
        return true;
    });

    // ------------------------------------------------------------------
    // Input (SDL3-backed by the host application).
    // ------------------------------------------------------------------
    const auto keyDown = [this](const std::string& key) {
        return m_isKeyDown ? m_isKeyDown(key) : false;
    };
    const auto axis = [this](const std::string& name) {
        return m_getAxis ? m_getAxis(name) : 0.0f;
    };
    const auto axisRaw = [this](const std::string& name) {
        return m_getAxisRaw ? m_getAxisRaw(name) : 0.0f;
    };
    const auto mouseDown = [this](const std::string& button) {
        return m_isMouseButtonDown ? m_isMouseButtonDown(button) : false;
    };
    const auto mousePosition = [this, makeVec2]() {
        return makeVec2(m_getMousePosition ? m_getMousePosition() : glm::vec2(0.0f));
    };
    const auto mouseDelta = [this, makeVec2]() {
        return makeVec2(m_getMouseDelta ? m_getMouseDelta() : glm::vec2(0.0f));
    };
    const auto mouseWheel = [this]() {
        return m_getMouseWheel ? m_getMouseWheel() : 0.0f;
    };

    input["key_down"] = keyDown;
    input["axis"] = axis;
    input["axis_raw"] = axisRaw;
    input["mouse_down"] = mouseDown;
    input["mouse_position"] = mousePosition;
    input["mouse_delta"] = mouseDelta;
    input["mouse_wheel"] = mouseWheel;
    m_lua.set_function("is_key_down", keyDown);
    m_lua.set_function("get_axis", axis);
    m_lua.set_function("get_axis_raw", axisRaw);
    m_lua.set_function("is_mouse_button_down", mouseDown);
    m_lua.set_function("get_mouse_position", mousePosition);
    m_lua.set_function("get_mouse_delta", mouseDelta);
    m_lua.set_function("get_mouse_wheel", mouseWheel);

    // ------------------------------------------------------------------
    // Physics.  The script layer owns the ECS components; the host creates
    // the native Jolt body through the callbacks registered by BindPhysics().
    // ------------------------------------------------------------------
    auto spawnPhysicsEntity = [this, makeEntity](const std::string& name,
                                                  int shape,
                                                  const glm::vec3& position,
                                                  const glm::vec3& size,
                                                  float radius,
                                                  float halfHeight,
                                                  bool isStatic) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        if (name == "Player") entity.AddComponent<PlayerTagComponent>();

        auto& transform = entity.AddComponent<TransformComponent>();
        transform.position = position;

        auto& body = entity.AddComponent<RigidBodyComponent>();
        body.motionType = isStatic ? RigidBodyComponent::MotionType::Static
                                   : RigidBodyComponent::MotionType::Dynamic;
        body.mass = isStatic ? 0.0f : 1.0f;
        body.useGravity = !isStatic;

        auto& collider = entity.AddComponent<ColliderComponent>();
        collider.friction = 0.5f;

        uint32_t bodyId = 0xFFFFFFFFu;
        if (shape == 0) {
            transform.scale = size;
            collider.shapeType = ColliderShapeType::Box;
            collider.halfExtents = size * 0.5f;
            if (m_physics.createBox) {
                bodyId = m_physics.createBox(size * 0.5f, position,
                                             static_cast<entt::entity>(entity),
                                             isStatic);
            }
        } else if (shape == 1) {
            transform.scale = glm::vec3(radius * 2.0f);
            collider.shapeType = ColliderShapeType::Sphere;
            collider.radius = radius;
            if (m_physics.createSphere) {
                bodyId = m_physics.createSphere(radius, position,
                                                static_cast<entt::entity>(entity),
                                                isStatic);
            }
        } else {
            transform.scale = glm::vec3(radius * 2.0f,
                                        (halfHeight + radius) * 2.0f,
                                        radius * 2.0f);
            collider.shapeType = ColliderShapeType::Capsule;
            collider.radius = radius;
            collider.halfHeight = halfHeight;
            if (m_physics.createCapsule) {
                bodyId = m_physics.createCapsule(halfHeight, radius, position,
                                                 static_cast<entt::entity>(entity),
                                                 isStatic);
            }
        }
        body.bodyId = bodyId;
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };

    physics["create_box"] = [this, spawnPhysicsEntity, readVec3](
                                 const std::string& name, float x, float y, float z,
                                 sol::object sizeValue,
                                 sol::optional<bool> isStatic) -> sol::object {
        glm::vec3 size{1.0f};
        if (sizeValue.get_type() == sol::type::table) {
            if (!readVec3(sizeValue, size)) {
                return sol::make_object(m_lua, sol::nil);
            }
        } else if (sizeValue.get_type() == sol::type::number) {
            size = glm::vec3(static_cast<float>(sizeValue.as<double>()));
        } else {
            return sol::make_object(m_lua, sol::nil);
        }
        return spawnPhysicsEntity(name, 0, {x, y, z}, size, 0.0f, 0.0f,
                                  isStatic.value_or(false));
    };
    physics["create_sphere"] = [spawnPhysicsEntity](
                                    const std::string& name, float x, float y,
                                    float z, float radius,
                                    sol::optional<bool> isStatic) {
        return spawnPhysicsEntity(name, 1, {x, y, z}, glm::vec3(radius * 2.0f),
                                  radius, 0.0f, isStatic.value_or(false));
    };
    physics["create_capsule"] = [spawnPhysicsEntity](
                                     const std::string& name, float x, float y,
                                     float z, float halfHeight, float radius,
                                     sol::optional<bool> isStatic) {
        return spawnPhysicsEntity(name, 2, {x, y, z}, glm::vec3(0.0f),
                                  radius, halfHeight, isStatic.value_or(false));
    };

    const auto applyImpulse = [this, resolve, readVec3Args](
                                  sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 impulse{0.0f};
        if (entity == entt::null || !readVec3Args(args, impulse)) return false;
        const uint32_t id = static_cast<uint32_t>(entity);
        if (m_applyImpulse && HasPhysicsBody(id)) {
            m_applyImpulse(id, impulse);
            return true;
        }

        // A body-less character root (for example a skeleton/visual root
        // without a ColliderComponent) moves through the script fallback
        // controller. Apply the impulse there as a velocity change so Jump
        // works for those characters as well.
        if (!m_scene) return false;
        auto& registry = m_scene->Registry();
        if (!registry.valid(entity) ||
            !registry.all_of<RigidBodyComponent, TransformComponent>(entity)) {
            return false;
        }
        const auto& rb = registry.get<RigidBodyComponent>(entity);
        auto& fallback = m_fallbackVelocities[id];
        if (!fallback.hasGroundY) {
            fallback.groundY =
                registry.get<TransformComponent>(entity).position.y;
            fallback.hasGroundY = true;
        }
        fallback.value += impulse / std::max(rb.mass, 0.01f);
        fallback.frame = m_frame;
        return true;
    };
    const auto getVelocity = [this, resolve, makeVec3](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return makeVec3(glm::vec3(0.0f));
        const uint32_t id = static_cast<uint32_t>(entity);
        if (m_getVelocity && HasPhysicsBody(id)) {
            return makeVec3(m_getVelocity(id));
        }
        const auto it = m_fallbackVelocities.find(id);
        return makeVec3(it != m_fallbackVelocities.end()
                            ? it->second.value
                            : glm::vec3(0.0f));
    };
    const auto setVelocity = [this, resolve, readVec3Args](
                                  sol::object value, sol::variadic_args args) {
        const entt::entity entity = resolve(value);
        glm::vec3 velocity{0.0f};
        if (entity == entt::null || !readVec3Args(args, velocity)) return false;
        const uint32_t id = static_cast<uint32_t>(entity);
        if (m_setVelocity && HasPhysicsBody(id)) {
            m_setVelocity(id, velocity);
            m_fallbackVelocities.erase(id);
        } else {
            auto& fallback = m_fallbackVelocities[id];
            if (!fallback.hasGroundY && m_scene) {
                auto& registry = m_scene->Registry();
                if (registry.valid(entity) &&
                    registry.all_of<TransformComponent>(entity)) {
                    fallback.groundY =
                        registry.get<TransformComponent>(entity).position.y;
                    fallback.hasGroundY = true;
                }
            }
            fallback.value = velocity;
            fallback.frame = m_frame;
        }
        return true;
    };
    const auto destroyBody = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<RigidBodyComponent>(entity)) return false;
        auto& body = registry.get<RigidBodyComponent>(entity);
        if (body.bodyId == 0xFFFFFFFFu) return false;
        if (m_physics.destroyBody) m_physics.destroyBody(body.bodyId);
        body.bodyId = 0xFFFFFFFFu;
        return true;
    };

    physics["add_impulse"] = applyImpulse;
    physics["get_velocity"] = getVelocity;
    physics["velocity"] = getVelocity;
    physics["set_velocity"] = setVelocity;
    physics["destroy_body"] = destroyBody;
    m_lua.set_function("apply_impulse", applyImpulse);
    m_lua.set_function("get_velocity", getVelocity);
    m_lua.set_function("set_velocity", setVelocity);
    m_lua.set_function("new_physics_cube", [spawnPhysicsEntity](
                                                const std::string& name,
                                                float x, float y, float z,
                                                float size) {
        return spawnPhysicsEntity(name, 0, {x, y, z}, glm::vec3(size),
                                  0.0f, 0.0f, false);
    });

    // ------------------------------------------------------------------
    // Audio (miniaudio-backed by the host application).  Mixer bus values
    // live in Scene and are applied by the audio system each frame.
    // ------------------------------------------------------------------
    const auto playSound = [this](const std::string& path,
                                  sol::optional<bool> loop) {
        if (!m_playSound) {
            AddLog("[Lua] audio.play: no audio backend is bound");
            return false;
        }
        m_playSound(path, loop.value_or(false));
        return true;
    };
    const auto stopSound = [this](const std::string& path) {
        if (m_stopSound) m_stopSound(path);
    };
    const auto isSoundPlaying = [this](const std::string& path) {
        return m_isSoundPlaying ? m_isSoundPlaying(path) : false;
    };
    const auto stopAllSounds = [this]() {
        if (m_stopAllSounds) m_stopAllSounds();
    };
    const auto setBus = [this](const std::string& name, float volume,
                               sol::optional<bool> muted) {
        if (!m_scene) return false;
        const int index = AudioBusIndexFromName(name);
        if (index < 0 || index >= kAudioBusCount) return false;
        auto& bus = m_scene->AudioBuses()[static_cast<size_t>(index)];
        bus.volume = std::clamp(volume, 0.0f, 1.0f);
        if (muted) bus.muted = *muted;
        return true;
    };
    const auto getBus = [this](const std::string& name) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        const int index = AudioBusIndexFromName(name);
        if (index < 0 || index >= kAudioBusCount) {
            return sol::make_object(m_lua, sol::nil);
        }
        const auto& bus = m_scene->AudioBuses()[static_cast<size_t>(index)];
        sol::table table = m_lua.create_table();
        table["name"] = AudioBusName(static_cast<AudioBus>(index));
        table["volume"] = bus.volume;
        table["muted"] = bus.muted;
        return sol::make_object(m_lua, table);
    };

    audio["play"] = playSound;
    audio["stop"] = stopSound;
    audio["is_playing"] = isSoundPlaying;
    audio["stop_all"] = stopAllSounds;
    audio["set_bus"] = setBus;
    audio["get_bus"] = getBus;
    m_lua.set_function("play_sound", [this](const std::string& path) {
        if (!m_playSound) {
            AddLog("[Lua] play_sound: no audio backend is bound");
            return false;
        }
        m_playSound(path, false);
        return true;
    });
    m_lua.set_function("stop_sound", stopSound);
    m_lua.set_function("is_sound_playing", isSoundPlaying);

    // ------------------------------------------------------------------
    // Animation state machines.
    // ------------------------------------------------------------------
    auto findAnimationMachine = [this](entt::entity entity)
        -> AnimationStateMachineComponent* {
        if (!m_scene) return nullptr;
        auto& registry = m_scene->Registry();
        int depth = 0;
        while (registry.valid(entity) && depth++ < 128) {
            if (registry.all_of<AnimationStateMachineComponent>(entity)) {
                return &registry.get<AnimationStateMachineComponent>(entity);
            }
            if (!registry.all_of<ParentComponent>(entity)) break;
            entity = registry.get<ParentComponent>(entity).parent;
        }
        return nullptr;
    };

    const auto setAnimationState = [resolve, findAnimationMachine](
                                       sol::object value,
                                       const std::string& state) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::SetState(*machine, state);
            return true;
        }
        return false;
    };
    const auto setAnimationParameter = [resolve, findAnimationMachine](
                                           sol::object value,
                                           const std::string& name,
                                           float parameter) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::SetParameter(*machine, name, parameter);
            return true;
        }
        return false;
    };
    const auto getAnimationParameter = [resolve, findAnimationMachine](
                                           sol::object value,
                                           const std::string& name,
                                           sol::optional<float> fallback) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            return AnimationSystem::GetParameter(*machine, name,
                                                 fallback.value_or(0.0f));
        }
        return fallback.value_or(0.0f);
    };
    const auto triggerAnimation = [resolve, findAnimationMachine](
                                      sol::object value,
                                      const std::string& name) {
        const entt::entity entity = resolve(value);
        if (auto* machine = findAnimationMachine(entity)) {
            AnimationSystem::Trigger(*machine, name);
            return true;
        }
        return false;
    };

    animation["set_state"] = setAnimationState;
    animation["set_parameter"] = setAnimationParameter;
    animation["get_parameter"] = getAnimationParameter;
    animation["trigger"] = triggerAnimation;
    m_lua.set_function("set_animation_state", setAnimationState);
    m_lua.set_function("set_animation_parameter", setAnimationParameter);
    m_lua.set_function("get_animation_parameter", getAnimationParameter);
    m_lua.set_function("trigger_animation", triggerAnimation);

    // ------------------------------------------------------------------
    // Skeletal model assets and playback.
    //
    // Galeshi.animation.model(path) returns a lightweight model object that
    // remembers the source file and clip list.  Spawn/apply attach the full
    // imported mesh hierarchy and SkeletalComponent to the scene; play passes
    // the model and a clip directly to the animation system.
    // ------------------------------------------------------------------
    auto skeletalPath = [](const sol::object& value) -> std::string {
        if (value.get_type() == sol::type::string) {
            return value.as<std::string>();
        }
        if (value.get_type() == sol::type::table) {
            sol::table table = value.as<sol::table>();
            const sol::object path = table.get<sol::object>("path");
            if (path.get_type() == sol::type::string) {
                return path.as<std::string>();
            }
        }
        return {};
    };

    auto makeSkeletalModel = [this](const std::string& path) -> sol::object {
        if (!m_assets) {
            AddLog("[Lua] animation.model: no AssetManager is bound");
            return sol::make_object(m_lua, sol::nil);
        }
        if (path.empty()) return sol::make_object(m_lua, sol::nil);

        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty() && !loaded.hasAnimation) {
            AddLog("[Lua] animation.model: could not load '" + path + "'");
            return sol::make_object(m_lua, sol::nil);
        }

        const std::string relativePath = m_assets->MakeRelativePath(path);
        sol::table model = m_lua.create_table();
        model["path"] = relativePath;
        model["name"] = std::filesystem::path(path).stem().string();
        model["valid"] = true;
        model["has_animation"] = loaded.hasAnimation;
        model["mesh_count"] = static_cast<int>(loaded.meshes.size());
        model["bone_count"] = loaded.totalBones;
        model["num_clips"] = static_cast<int>(loaded.animationNames.size());

        sol::table clips = m_lua.create_table();
        sol::table durations = m_lua.create_table();
        for (size_t i = 0; i < loaded.animationNames.size(); ++i) {
            clips[i + 1] = loaded.animationNames[i];
        }
        for (size_t i = 0; i < loaded.animationDurations.size(); ++i) {
            durations[i + 1] = loaded.animationDurations[i];
        }
        model["clips"] = clips;
        model["durations"] = durations;
        model[sol::metatable_key] = m_skeletalMeta;
        return sol::make_object(m_lua, model);
    };

    auto spawnSkeletalModel = [this, makeEntity](
                                  const std::string& path,
                                  const std::string& requestedName,
                                  const glm::vec3& position) -> sol::object {
        if (!m_assets || !m_scene) {
            AddLog("[Lua] animation.spawn: no AssetManager or Scene is bound");
            return sol::make_object(m_lua, sol::nil);
        }
        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty()) {
            AddLog("[Lua] animation.spawn: model has no meshes: " + path);
            return sol::make_object(m_lua, sol::nil);
        }

        const std::string source = m_assets->MakeRelativePath(path);
        const std::string stem = std::filesystem::path(path).stem().string();
        const std::string rootName = requestedName.empty()
            ? (stem.empty() ? std::string("SkeletalModel") : stem)
            : requestedName;

        Entity root = m_scene->CreateEntity(rootName);
        root.AddComponent<LuaEntityComponent>();
        auto& transform = root.GetComponent<TransformComponent>();
        transform.position = position;

        if (loaded.hasAnimation || loaded.totalBones > 0) {
            auto& skeleton = root.AddComponent<SkeletalComponent>();
            skeleton.isAnimated = loaded.hasAnimation;
            skeleton.sourcePath = source;
            skeleton.boneMatrices.assign(256, glm::mat4(1.0f));
            if (loaded.hasAnimation) {
                auto& machine =
                    root.AddComponent<AnimationStateMachineComponent>();
                machine = AnimationSystem::CreateDefaultStateMachine(
                    loaded.animationNames);
            }
        }

        for (size_t i = 0; i < loaded.meshes.size(); ++i) {
            Entity meshEntity = m_scene->CreateEntity(
                rootName + "_" + std::to_string(i));
            meshEntity.AddComponent<LuaEntityComponent>();
            meshEntity.AddComponent<ParentComponent>(
                ParentComponent{static_cast<entt::entity>(root)});
            meshEntity.AddComponent<MeshComponent>(MeshComponent{
                loaded.meshes[i], source, "", static_cast<int>(i)});

            std::shared_ptr<Material> material;
            if (i < loaded.meshMaterialIndex.size()) {
                const int materialIndex = loaded.meshMaterialIndex[i];
                if (materialIndex >= 0 &&
                    materialIndex < static_cast<int>(loaded.materials.size())) {
                    material = loaded.materials[static_cast<size_t>(materialIndex)];
                }
            }
            if (!material && !loaded.materials.empty()) {
                material = loaded.materials.front();
            }
            if (!material) material = std::make_shared<Material>();
            meshEntity.AddComponent<MaterialComponent>(
                MaterialComponent{material});
        }

        spdlog::info("[Lua] Spawned skeletal model '{}' ({} meshes, {} clips)",
                     source, loaded.meshes.size(), loaded.animationNames.size());
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(root)));
    };

    auto applySkeletalModel = [this](entt::entity entity,
                                     const std::string& path) -> bool {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        auto loaded = m_assets->LoadModel(path);
        if (loaded.meshes.empty() && !loaded.hasAnimation) return false;

        auto& registry = m_scene->Registry();
        SkeletalComponent* skeleton = nullptr;
        if (registry.all_of<SkeletalComponent>(entity)) {
            skeleton = &registry.get<SkeletalComponent>(entity);
        } else {
            skeleton = &registry.emplace<SkeletalComponent>(entity);
        }
        skeleton->isAnimated = loaded.hasAnimation;
        skeleton->sourcePath = m_assets->MakeRelativePath(path);
        skeleton->currentTime = 0.0f;
        skeleton->activeAnimation = 0;
        skeleton->boneMatrices.assign(256, glm::mat4(1.0f));

        if (loaded.hasAnimation) {
            if (!registry.all_of<AnimationStateMachineComponent>(entity)) {
                auto& machine =
                    registry.emplace<AnimationStateMachineComponent>(entity);
                machine = AnimationSystem::CreateDefaultStateMachine(
                    loaded.animationNames);
            } else {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                if (machine.states.empty()) {
                    machine = AnimationSystem::CreateDefaultStateMachine(
                        loaded.animationNames);
                }
            }
        }
        return true;
    };

    auto resolveSkeletalClip = [this](const std::string& path,
                                      const sol::object& value,
                                      int& outClip) -> bool {
        if (!m_assets) return false;
        auto loaded = m_assets->LoadModel(path);
        if (!loaded.hasAnimation || loaded.animationNames.empty()) return false;

        int index = 0; // Lua-facing clip indices are 1-based.
        if (value.get_type() == sol::type::number) {
            index = static_cast<int>(value.as<double>()) - 1;
        } else if (value.get_type() == sol::type::string) {
            const std::string name = value.as<std::string>();
            bool found = false;
            for (size_t i = 0; i < loaded.animationNames.size(); ++i) {
                if (loaded.animationNames[i] == name) {
                    index = static_cast<int>(i);
                    found = true;
                    break;
                }
            }
            if (!found) return false;
        }
        index = std::clamp(index, 0,
                           static_cast<int>(loaded.animationNames.size()) - 1);
        outClip = index;
        return true;
    };

    auto playSkeletalModel = [this, applySkeletalModel, resolveSkeletalClip](
                                 entt::entity entity,
                                 const std::string& path,
                                 const sol::object& clipValue) -> bool {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        if (!applySkeletalModel(entity, path)) return false;

        int clip = 0;
        if (!resolveSkeletalClip(path, clipValue, clip)) return false;

        auto& registry = m_scene->Registry();
        auto* skeleton = registry.try_get<SkeletalComponent>(entity);
        if (!skeleton) return false;
        skeleton->isAnimated = true;
        skeleton->activeAnimation = clip;
        skeleton->currentTime = 0.0f;

        if (auto* machine =
                registry.try_get<AnimationStateMachineComponent>(entity)) {
            int stateIndex = -1;
            for (size_t i = 0; i < machine->states.size(); ++i) {
                if (machine->states[i].clip == clip) {
                    stateIndex = static_cast<int>(i);
                    break;
                }
            }
            if (stateIndex >= 0) {
                machine->enabled = true;
                AnimationSystem::SetState(*machine,
                                          machine->states[stateIndex].name);
            } else {
                // Custom state machines may not have a state for this clip.
                // Fall back to direct clip playback.
                machine->enabled = false;
            }
        }
        return true;
    };

    auto playSkeletalClip = [this, playSkeletalModel](entt::entity entity,
                                                      const sol::object& clipValue) {
        if (!m_assets || !m_scene || entity == entt::null) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        if (!skeleton || skeleton->sourcePath.empty()) return false;
        return playSkeletalModel(entity, skeleton->sourcePath, clipValue);
    };

    // Shared metatable for model objects returned by animation.model().
    m_skeletalMeta = m_lua.create_table();
    sol::table modelMethods = m_lua.create_table();
    modelMethods["clip_count"] = [](sol::table self) -> int {
        const sol::object count = self.get<sol::object>("num_clips");
        return count.get_type() == sol::type::number
            ? static_cast<int>(count.as<double>())
            : 0;
    };
    modelMethods["clip_name"] = [](sol::table self, int index) -> std::string {
        const sol::object clipsValue = self.get<sol::object>("clips");
        if (clipsValue.get_type() != sol::type::table) return {};
        sol::table clips = clipsValue.as<sol::table>();
        const sol::object name = clips.get<sol::object>(index);
        return name.get_type() == sol::type::string
            ? name.as<std::string>()
            : std::string{};
    };
    modelMethods["duration"] = [](sol::table self, int index) -> float {
        const sol::object durationsValue = self.get<sol::object>("durations");
        if (durationsValue.get_type() != sol::type::table) return 0.0f;
        sol::table durations = durationsValue.as<sol::table>();
        const sol::object value = durations.get<sol::object>(index);
        return value.get_type() == sol::type::number
            ? static_cast<float>(value.as<double>())
            : 0.0f;
    };
    modelMethods["spawn"] = [this, skeletalPath, spawnSkeletalModel](
                                sol::table self,
                                sol::optional<std::string> name,
                                sol::optional<float> x,
                                sol::optional<float> y,
                                sol::optional<float> z) -> sol::object {
        return spawnSkeletalModel(skeletalPath(sol::object(self)),
                                  name.value_or(""),
                                  {x.value_or(0.0f), y.value_or(0.0f),
                                   z.value_or(0.0f)});
    };
    modelMethods["apply"] = [this, resolve, skeletalPath, applySkeletalModel](
                                sol::table self, sol::object entity) {
        return applySkeletalModel(resolve(entity),
                                  skeletalPath(sol::object(self)));
    };
    modelMethods["play"] = [this, resolve, skeletalPath, playSkeletalModel](
                               sol::table self, sol::object entity,
                               sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalModel(resolve(entity), skeletalPath(sol::object(self)),
                                 clip);
    };
    m_skeletalMeta[sol::meta_function::index] = modelMethods;
    m_skeletalMeta[sol::meta_function::to_string] =
        [](sol::table self) -> std::string {
            const sol::object path = self.get<sol::object>("path");
            const sol::object count = self.get<sol::object>("num_clips");
            return std::string("SkeletalModel(") +
                   (path.get_type() == sol::type::string
                        ? path.as<std::string>()
                        : std::string("?")) +
                   ", clips=" +
                   std::to_string(count.get_type() == sol::type::number
                                      ? static_cast<int>(count.as<double>())
                                      : 0) + ")";
        };

    animation["model"] = [makeSkeletalModel](const std::string& path) {
        return makeSkeletalModel(path);
    };
    animation["load"] = [makeSkeletalModel](const std::string& path) {
        return makeSkeletalModel(path);
    };
    animation["spawn"] = [this, skeletalPath, spawnSkeletalModel](
                             sol::object modelValue,
                             sol::optional<std::string> name,
                             sol::optional<float> x,
                             sol::optional<float> y,
                             sol::optional<float> z) -> sol::object {
        const std::string path = skeletalPath(modelValue);
        if (path.empty()) return sol::make_object(m_lua, sol::nil);
        return spawnSkeletalModel(path, name.value_or(""),
                                  {x.value_or(0.0f), y.value_or(0.0f),
                                   z.value_or(0.0f)});
    };
    animation["apply"] = [this, resolve, skeletalPath, applySkeletalModel](
                             sol::object entity, sol::object modelValue) {
        return applySkeletalModel(resolve(entity), skeletalPath(modelValue));
    };
    animation["play"] = [this, resolve, skeletalPath, playSkeletalModel](
                            sol::object entity, sol::object modelValue,
                            sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalModel(resolve(entity), skeletalPath(modelValue),
                                 clip);
    };
    animation["play_clip"] = [this, resolve, playSkeletalClip](
                                 sol::object entity,
                                 sol::variadic_args args) {
        sol::object clip;
        if (args.size() > 0) clip = args[0];
        return playSkeletalClip(resolve(entity), clip);
    };
    animation["stop"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null || !m_scene) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        if (!skeleton) return false;
        skeleton->isAnimated = false;
        return true;
    };
    animation["is_playing"] = [this, resolve](sol::object value) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null || !m_scene) return false;
        auto* skeleton = m_scene->Registry().try_get<SkeletalComponent>(entity);
        return skeleton && skeleton->isAnimated;
    };

    // Legacy aliases for simple scripts.
    m_lua.set_function("load_skeletal_model", [makeSkeletalModel](
                                                   const std::string& path) {
        return makeSkeletalModel(path);
    });

    // ------------------------------------------------------------------
    // AI and navigation (Detour/Recast through host callbacks).
    // ------------------------------------------------------------------
    auto navAgent = [this, resolve](sol::object value) -> NavAgentComponent* {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return nullptr;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<NavAgentComponent>(entity)) return nullptr;
        return &registry.get<NavAgentComponent>(entity);
    };

    const auto setAITarget = [navAgent, readVec3Args](
                                 sol::object value, sol::variadic_args args) {
        NavAgentComponent* agent = navAgent(value);
        glm::vec3 target{0.0f};
        if (!agent || !readVec3Args(args, target)) return false;
        agent->targetPosition = target;
        agent->hasTarget = true;
        return true;
    };
    const auto isPathDone = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        return !agent || agent->path.empty();
    };
    const auto setAISpeed = [navAgent](sol::object value, float speed) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->speed = speed;
        return true;
    };
    const auto getAISpeed = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        return agent ? agent->speed : 0.0f;
    };
    const auto clearAITarget = [navAgent](sol::object value) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->hasTarget = false;
        agent->path.clear();
        agent->currentWaypoint = 0;
        return true;
    };
    const auto setNavStoppingDistance = [navAgent](sol::object value,
                                                    float distance) {
        NavAgentComponent* agent = navAgent(value);
        if (!agent) return false;
        agent->stoppingDistance = std::max(distance, 0.0f);
        return true;
    };

    ai["set_target"] = setAITarget;
    ai["path_done"] = isPathDone;
    ai["is_path_done"] = isPathDone;
    ai["set_speed"] = setAISpeed;
    ai["speed"] = getAISpeed;
    ai["clear_target"] = clearAITarget;
    ai["set_stopping_distance"] = setNavStoppingDistance;
    m_lua.set_function("set_ai_target", setAITarget);
    m_lua.set_function("is_path_done", isPathDone);
    m_lua.set_function("set_ai_speed", setAISpeed);

    navigation["find_path"] = [this, readVec3, makeVec3](
                                  sol::object startValue,
                                  sol::object endValue) -> sol::object {
        glm::vec3 start{0.0f};
        glm::vec3 end{0.0f};
        if (!m_findPath || !readVec3(startValue, start) ||
            !readVec3(endValue, end)) {
            return sol::make_object(m_lua, sol::nil);
        }
        std::vector<glm::vec3> path;
        if (!m_findPath(start, end, path)) {
            return sol::make_object(m_lua, sol::nil);
        }
        sol::table result = m_lua.create_table();
        int index = 1;
        for (const glm::vec3& point : path) {
            result[index++] = makeVec3(point);
        }
        return sol::make_object(m_lua, result);
    };
    navigation["build"] = [this]() {
        return m_buildNavMesh ? m_buildNavMesh() : false;
    };

    // ------------------------------------------------------------------
    // UI runtime access and event handler registration.
    // ------------------------------------------------------------------
    auto findCanvas = [this](const std::string& id) -> std::pair<UICanvas*, entt::entity> {
        if (!m_scene) return {nullptr, entt::null};
        auto view = m_scene->Registry().view<UICanvasComponent>();
        for (entt::entity entity : view) {
            UICanvas& canvas = view.get<UICanvasComponent>(entity).canvas;
            if (id.empty() || canvas.canvasId == id) return {&canvas, entity};
        }
        return {nullptr, entt::null};
    };

    auto findWidget = [](UICanvas& canvas, const std::string& id) -> UIWidget* {
        for (UIWidget& widget : canvas.widgets) {
            if (widget.id == id) return &widget;
        }
        return nullptr;
    };

    auto widgetToTable = [this, makeVec2, makeVec4](const UIWidget& widget) {
        sol::table table = m_lua.create_table();
        table["id"] = widget.id;
        table["label"] = widget.label;
        table["type"] = WidgetTypeName(widget.type);
        table["visible"] = widget.visible;
        table["enabled"] = widget.enabled;
        table["interactable"] = widget.interactable;
        table["opacity"] = widget.opacity;
        table["position"] = makeVec2(widget.position);
        table["size"] = makeVec2(widget.size);
        table["value"] = widget.sliderValue;
        table["slider_step"] = widget.sliderStep;
        table["checked"] = widget.checked;
        table["font_size"] = widget.fontSize;
        table["wrap_width"] = widget.textWrapWidth;
        table["text_align"] =
            widget.textAlign == UITextAlign::Center ? "center"
            : widget.textAlign == UITextAlign::Right ? "right"
                                                     : "left";
        table["use_color"] = widget.useColor;
        table["color"] = makeVec4(widget.color);
        table["use_border_color"] = widget.useBorderColor;
        table["border_color"] = makeVec4(widget.borderColor);
        table["tooltip"] = widget.tooltip;
        table["image"] = widget.imagePath;
        table["selected_option"] = widget.selectedOption;
        sol::table options = m_lua.create_table();
        for (size_t i = 0; i < widget.options.size(); ++i) {
            options[i + 1] = widget.options[i];
        }
        table["options"] = options;
        table["on_click"] = widget.onClick;
        table["on_hover"] = widget.onHover;
        table["on_show"] = widget.onShow;
        return table;
    };

    auto applyWidgetProps = [readNumber, readBool, readString, readVec2,
                             readVec4](UIWidget& widget,
                                       const sol::table& props) {
        std::string text;
        bool boolean = false;
        float number = 0.0f;
        glm::vec2 vector2{0.0f};
        glm::vec4 color{1.0f};

        if (readString(props.get<sol::object>("label"), text)) widget.label = text;
        if (readBool(props.get<sol::object>("visible"), boolean)) widget.visible = boolean;
        if (readBool(props.get<sol::object>("enabled"), boolean)) widget.enabled = boolean;
        if (readBool(props.get<sol::object>("interactable"), boolean)) {
            widget.interactable = boolean;
        }
        if (readNumber(props.get<sol::object>("opacity"), number)) {
            widget.opacity = std::clamp(number, 0.0f, 1.0f);
        }
        if (readNumber(props.get<sol::object>("value"), number)) widget.sliderValue = number;
        if (readNumber(props.get<sol::object>("slider_step"), number)) {
            widget.sliderStep = std::max(0.0f, number);
        }
        if (readBool(props.get<sol::object>("checked"), boolean)) widget.checked = boolean;
        if (readNumber(props.get<sol::object>("font_size"), number)) {
            widget.fontSize = std::max(0.0f, number);
        }
        if (readNumber(props.get<sol::object>("wrap_width"), number)) {
            widget.textWrapWidth = std::max(0.0f, number);
        }
        if (readString(props.get<sol::object>("text_align"), text)) {
            if (text == "center") widget.textAlign = UITextAlign::Center;
            else if (text == "right") widget.textAlign = UITextAlign::Right;
            else widget.textAlign = UITextAlign::Left;
        }
        if (readNumber(props.get<sol::object>("selected_option"), number)) {
            widget.selectedOption = static_cast<int>(number);
        }
        if (readString(props.get<sol::object>("tooltip"), text)) {
            widget.tooltip = text;
        }
        if (readString(props.get<sol::object>("image"), text)) widget.imagePath = text;
        if (readString(props.get<sol::object>("on_click"), text)) widget.onClick = text;
        if (readString(props.get<sol::object>("on_hover"), text)) widget.onHover = text;
        if (readString(props.get<sol::object>("on_show"), text)) widget.onShow = text;
        const sol::object options =
            props.get<sol::object>("options");
        if (options.get_type() == sol::type::table) {
            widget.options.clear();
            const sol::table table = options.as<sol::table>();
            for (size_t i = 1; i <= table.size(); ++i) {
                const sol::object value = table[i];
                if (value.get_type() == sol::type::string) {
                    widget.options.push_back(value.as<std::string>());
                }
            }
        }
        if (readBool(props.get<sol::object>("use_color"), boolean)) {
            widget.useColor = boolean;
        }
        if (readBool(props.get<sol::object>("use_border_color"), boolean)) {
            widget.useBorderColor = boolean;
        }
        if (readVec4(props.get<sol::object>("border_color"), color)) {
            widget.borderColor = color;
        }
        if (readNumber(props.get<sol::object>("z_order"), number)) {
            widget.zOrder = static_cast<int>(number);
        }
        if (readNumber(props.get<sol::object>("parent"), number)) {
            widget.parent = static_cast<int>(number);
        }
        if (readVec2(props.get<sol::object>("position"), vector2)) widget.position = vector2;
        if (readVec2(props.get<sol::object>("size"), vector2)) widget.size = vector2;
        if (readVec4(props.get<sol::object>("color"), color)) widget.color = color;
    };

    ui["find_canvas"] = [this, findCanvas, makeEntity](const std::string& id) {
        const auto [canvas, entity] = findCanvas(id);
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        return makeEntity(static_cast<uint32_t>(entity));
    };
    ui["canvases"] = [this, findCanvas]() {
        sol::table result = m_lua.create_table();
        if (!m_scene) return result;
        int index = 1;
        auto view = m_scene->Registry().view<UICanvasComponent>();
        for (entt::entity entity : view) {
            const UICanvas& canvas = view.get<UICanvasComponent>(entity).canvas;
            sol::table entry = m_lua.create_table();
            entry["entity"] = static_cast<uint32_t>(entity);
            entry["id"] = canvas.canvasId;
            entry["title"] = canvas.title;
            entry["visible"] = canvas.visible;
            entry["widgets"] = static_cast<int>(canvas.widgets.size());
            result[index++] = entry;
        }
        return result;
    };
    ui["canvas"] = [this, findCanvas](const std::string& id) -> sol::object {
        const auto [canvas, entity] = findCanvas(id);
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        sol::table table = m_lua.create_table();
        table["entity"] = static_cast<uint32_t>(entity);
        table["id"] = canvas->canvasId;
        table["title"] = canvas->title;
        table["visible"] = canvas->visible;
        table["widgets"] = static_cast<int>(canvas->widgets.size());
        return sol::make_object(m_lua, table);
    };
    ui["widget"] = [this, findCanvas, findWidget, widgetToTable](
                       const std::string& canvasId,
                       const std::string& widgetId) -> sol::object {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return sol::make_object(m_lua, sol::nil);
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return sol::make_object(m_lua, sol::nil);
        return sol::make_object(m_lua, widgetToTable(*widget));
    };
    ui["set_widget"] = [findCanvas, findWidget, applyWidgetProps](
                           const std::string& canvasId,
                           const std::string& widgetId,
                           sol::table props) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return false;
        applyWidgetProps(*widget, props);
        return true;
    };
    ui["set_value"] = [findCanvas, findWidget](const std::string& canvasId,
                                               const std::string& widgetId,
                                               float value) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        UIWidget* widget = findWidget(*canvas, widgetId);
        if (!widget) return false;
        widget->sliderValue = value;
        return true;
    };
    ui["value"] = [findCanvas, findWidget](const std::string& canvasId,
                                           const std::string& widgetId) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return 0.0f;
        UIWidget* widget = findWidget(*canvas, widgetId);
        return widget ? widget->sliderValue : 0.0f;
    };
    ui["show_canvas"] = [findCanvas](const std::string& canvasId, bool visible) {
        const auto [canvas, entity] = findCanvas(canvasId);
        (void)entity;
        if (!canvas) return false;
        canvas->visible = visible;
        return true;
    };
    ui["on"] = [this](const std::string& name, sol::protected_function function) {
        if (!function.valid()) return false;
        m_uiHandlers[name] = function;
        return true;
    };
    ui["off"] = [this](const std::string& name) {
        return m_uiHandlers.erase(name) > 0;
    };
    ui["call"] = [this](const std::string& name,
                        sol::optional<std::string> widgetId,
                        sol::optional<float> value,
                        sol::optional<bool> checked) {
        return CallUIHandler(name, widgetId.value_or(""), value.value_or(0.0f),
                             checked.value_or(false));
    };

    // ------------------------------------------------------------------
    // Particles.
    // ------------------------------------------------------------------
    auto applyParticleProps = [readNumber, readBool, readVec3, readVec4](
                                  ParticleEmitterComponent& emitter,
                                  const sol::table& props) {
        bool boolean = false;
        float number = 0.0f;
        glm::vec3 vector3{0.0f};
        glm::vec4 color{1.0f};

        if (readBool(props.get<sol::object>("enabled"), boolean)) emitter.enabled = boolean;
        if (readNumber(props.get<sol::object>("emit_rate"), number)) emitter.emitRate = number;
        if (readNumber(props.get<sol::object>("lifetime"), number)) emitter.lifetime = number;
        if (readNumber(props.get<sol::object>("lifetime_variance"), number)) {
            emitter.lifetimeVariance = number;
        }
        if (readNumber(props.get<sol::object>("start_size"), number)) emitter.startSize = number;
        if (readNumber(props.get<sol::object>("end_size"), number)) emitter.endSize = number;
        if (readNumber(props.get<sol::object>("velocity_variance"), number)) {
            emitter.velocityVariance = number;
        }
        if (readNumber(props.get<sol::object>("spawn_radius"), number)) emitter.spawnRadius = number;
        if (readNumber(props.get<sol::object>("drag"), number)) emitter.drag = number;
        if (readNumber(props.get<sol::object>("max_particles"), number)) {
            emitter.maxParticles = static_cast<uint32_t>(
                std::max(0.0f, number));
        }
        if (readVec3(props.get<sol::object>("velocity"), vector3)) emitter.velocity = vector3;
        if (readVec3(props.get<sol::object>("spawn_box"), vector3)) emitter.spawnBoxSize = vector3;
        if (readVec3(props.get<sol::object>("gravity"), vector3)) emitter.gravity = vector3;
        if (readVec4(props.get<sol::object>("start_color"), color)) emitter.startColor = color;
        if (readVec4(props.get<sol::object>("end_color"), color)) emitter.endColor = color;
    };

    auto particleToTable = [this, makeVec3, makeVec4](const ParticleEmitterComponent& emitter) {
        sol::table table = m_lua.create_table();
        table["enabled"] = emitter.enabled;
        table["emit_rate"] = emitter.emitRate;
        table["lifetime"] = emitter.lifetime;
        table["lifetime_variance"] = emitter.lifetimeVariance;
        table["start_size"] = emitter.startSize;
        table["end_size"] = emitter.endSize;
        table["velocity"] = makeVec3(emitter.velocity);
        table["velocity_variance"] = emitter.velocityVariance;
        table["spawn_radius"] = emitter.spawnRadius;
        table["spawn_box"] = makeVec3(emitter.spawnBoxSize);
        table["gravity"] = makeVec3(emitter.gravity);
        table["drag"] = emitter.drag;
        table["max_particles"] = static_cast<double>(emitter.maxParticles);
        table["start_color"] = makeVec4(emitter.startColor);
        table["end_color"] = makeVec4(emitter.endColor);
        return table;
    };

    particles["create"] = [this, makeEntity, applyParticleProps](
                              const std::string& name, float x, float y, float z,
                              sol::optional<sol::table> props) -> sol::object {
        if (!m_scene) return sol::make_object(m_lua, sol::nil);
        Entity entity = m_scene->CreateEntity(name);
        entity.AddComponent<LuaEntityComponent>();
        auto& transform = entity.AddComponent<TransformComponent>();
        transform.position = {x, y, z};
        auto& emitter = entity.AddComponent<ParticleEmitterComponent>();
        if (props) applyParticleProps(emitter, *props);
        return makeEntity(static_cast<uint32_t>(static_cast<entt::entity>(entity)));
    };
    particles["set"] = [this, resolve, applyParticleProps](sol::object value,
                                                           sol::table props) {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return false;
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParticleEmitterComponent>(entity)) return false;
        applyParticleProps(registry.get<ParticleEmitterComponent>(entity), props);
        return true;
    };
    particles["get"] = [this, resolve, particleToTable](sol::object value) -> sol::object {
        const entt::entity entity = resolve(value);
        if (entity == entt::null) return sol::make_object(m_lua, sol::nil);
        auto& registry = m_scene->Registry();
        if (!registry.all_of<ParticleEmitterComponent>(entity)) {
            return sol::make_object(m_lua, sol::nil);
        }
        return sol::make_object(m_lua,
                                particleToTable(registry.get<ParticleEmitterComponent>(entity)));
    };

    // ------------------------------------------------------------------
    // Engine / lifecycle.
    // ------------------------------------------------------------------
    engine["version"] = "0.3.0";
    engine["time"] = [this]() { return m_time; };
    engine["delta_time"] = [this]() { return static_cast<double>(m_deltaTime); };
    engine["frame"] = [this]() { return static_cast<double>(m_frame); };
    engine["quit"] = [this]() {
        if (m_requestExit) {
            m_requestExit();
            return true;
        }
        return false;
    };
    engine["log"] = logFn;
    engine["warn"] = warnFn;
    engine["error"] = errorFn;

    // ------------------------------------------------------------------
    // Gameplay camera mode. The host owns the actual camera; these calls
    // only switch between the player's third- and first-person views.
    // ------------------------------------------------------------------
    auto setCameraMode = [this](sol::object value) -> bool {
        int mode = -1;
        if (value.get_type() == sol::type::number) {
            const int numeric = value.as<int>();
            if (numeric == GALESHI_CAMERA_THIRD_PERSON ||
                numeric == GALESHI_CAMERA_FIRST_PERSON) {
                mode = numeric;
            }
        } else if (value.get_type() == sol::type::string) {
            const std::string name = NormalizeKey(value.as<std::string>());
            if (name == "third" || name == "third_person" ||
                name == "thirdperson") {
                mode = GALESHI_CAMERA_THIRD_PERSON;
            } else if (name == "first" || name == "first_person" ||
                       name == "firstperson") {
                mode = GALESHI_CAMERA_FIRST_PERSON;
            }
        }
        if (!m_setCameraMode || mode < 0) return false;
        return m_setCameraMode(mode);
    };
    const auto getCameraMode = [this]() -> int {
        return m_getCameraMode
            ? m_getCameraMode()
            : static_cast<int>(GALESHI_CAMERA_THIRD_PERSON);
    };
    const auto cameraForward = [this, makeVec3]() {
        return makeVec3(m_getCameraForward
            ? m_getCameraForward()
            : glm::vec3(0.0f, 0.0f, -1.0f));
    };
    const auto cameraRight = [this, makeVec3]() {
        return makeVec3(m_getCameraRight
            ? m_getCameraRight()
            : glm::vec3(1.0f, 0.0f, 0.0f));
    };
    const auto cameraMovement =
        [this, makeVec3](float horizontal, float vertical) {
            const glm::vec3 forward = m_getCameraForward
                ? m_getCameraForward()
                : glm::vec3(0.0f, 0.0f, -1.0f);
            const glm::vec3 right = m_getCameraRight
                ? m_getCameraRight()
                : glm::vec3(1.0f, 0.0f, 0.0f);
            glm::vec3 movement = forward * vertical + right * horizontal;
            const float length = glm::length(movement);
            if (length > 1.0f) movement /= length;
            return makeVec3(movement);
        };
    camera["THIRD_PERSON"] =
        static_cast<int>(GALESHI_CAMERA_THIRD_PERSON);
    camera["FIRST_PERSON"] =
        static_cast<int>(GALESHI_CAMERA_FIRST_PERSON);
    camera["set_mode"] = setCameraMode;
    camera["mode"] = getCameraMode;
    camera["forward"] = cameraForward;
    camera["right"] = cameraRight;
    camera["movement"] = cameraMovement;
    m_lua.set_function("set_camera_mode", setCameraMode);
    m_lua.set_function("get_camera_mode", getCameraMode);
    m_lua.set_function("get_camera_forward", cameraForward);
    m_lua.set_function("get_camera_right", cameraRight);
    m_lua.set_function("get_camera_movement", cameraMovement);

    const auto playerStrafing = [this]() {
        return m_getPlayerStrafing ? m_getPlayerStrafing() : false;
    };
    const auto setPlayerStrafing = [this](bool enabled) {
        if (!m_setPlayerStrafing) return false;
        m_setPlayerStrafing(enabled);
        return true;
    };
    player["strafing"] = playerStrafing;
    player["set_strafing"] = setPlayerStrafing;
    m_lua.set_function("is_player_strafing", playerStrafing);
    m_lua.set_function("set_player_strafing", setPlayerStrafing);

    const auto compileShader = [this](const std::string& path) {
        if (!m_compileShader) {
            m_shaderError = "No shader compiler is bound by the host.";
            return false;
        }
        std::string error;
        const bool ok = m_compileShader(path, error);
        m_shaderError = ok ? std::string{} : error;
        return ok;
    };
    const auto shaderError = [this]() { return m_shaderError; };
    shader["compile"] = compileShader;
    shader["error"] = shaderError;
    m_lua.set_function("compile_shader", compileShader);
    m_lua.set_function("get_shader_error", shaderError);

    // Explicitly release the temporary tables; all bound functions are owned
    // by the Lua state / m_api now.
    (void)world;
    (void)input;
    (void)physics;
    (void)audio;
    (void)animation;
    (void)ai;
    (void)navigation;
    (void)ui;
    (void)particles;
    (void)engine;
    (void)camera;
    (void)player;
    (void)shader;
}

CppWorkerPacket ScriptEngine::MakeWorkerResponse(
    const CppWorkerPacket& request) {
    CppWorkerPacket response;
    response.type = static_cast<uint32_t>(CppWorkerMessage::Response);
    response.id = request.id;
    response.entity = UINT32_MAX;
    const auto kind =
        static_cast<CppWorkerRequest>(request.kind);

    auto canvasByEntity = [&](uint32_t handle) -> UICanvas* {
        if (!m_scene) return nullptr;
        auto& registry = m_scene->Registry();
        const entt::entity entity = static_cast<entt::entity>(handle);
        if (!registry.valid(entity) ||
            !registry.all_of<UICanvasComponent>(entity)) {
            return nullptr;
        }
        return &registry.get<UICanvasComponent>(entity).canvas;
    };
    auto widgetByEntity = [&](uint32_t handle,
                              const char* widgetId) -> UIWidget* {
        UICanvas* canvas = canvasByEntity(handle);
        if (!canvas || !widgetId) return nullptr;
        return FindWidgetById(*canvas, widgetId);
    };

    switch (kind) {
    case CppWorkerRequest::KeyDown:
        response.valid = m_isKeyDown && m_isKeyDown(request.text)
            ? 1u : 0u;
        break;
    case CppWorkerRequest::Axis:
        response.x = m_getAxis ? m_getAxis(request.text) : 0.0f;
        break;
    case CppWorkerRequest::FindEntity:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<NameComponent>();
            for (auto entity : view) {
                if (view.get<NameComponent>(entity).name == request.text) {
                    response.entity = static_cast<uint32_t>(entity);
                    response.valid = 1;
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::EntityValid:
        response.valid = m_scene &&
            m_scene->Registry().valid(
                static_cast<entt::entity>(request.entity)) ? 1u : 0u;
        break;
    case CppWorkerRequest::GetPosition:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const GaleshiScriptVec3 value = ToScriptVec3(
                    registry.get<TransformComponent>(entity).position);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPosition:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                registry.get<TransformComponent>(entity).position =
                    FromScriptVec3({request.x, request.y, request.z});
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetVelocity:
        if (m_getVelocity) {
            const GaleshiScriptVec3 value =
                ToScriptVec3(m_getVelocity(request.entity));
            response.x = value.x;
            response.y = value.y;
            response.z = value.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::SetVelocity:
        if (m_setVelocity) {
            m_setVelocity(
                request.entity,
                FromScriptVec3({request.x, request.y, request.z}));
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::SetCameraMode:
        if (m_setCameraMode) {
            response.valid = m_setCameraMode(
                static_cast<int>(request.entity)) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::GetCameraMode:
        if (m_getCameraMode) {
            response.entity =
                static_cast<uint32_t>(m_getCameraMode());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetCameraForward:
        if (m_getCameraForward) {
            const GaleshiScriptVec3 value =
                ToScriptVec3(m_getCameraForward());
            response.x = value.x;
            response.y = value.y;
            response.z = value.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetCameraRight:
        if (m_getCameraRight) {
            const GaleshiScriptVec3 value =
                ToScriptVec3(m_getCameraRight());
            response.x = value.x;
            response.y = value.y;
            response.z = value.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetPlayerStrafing:
        if (m_getPlayerStrafing) {
            response.valid = m_getPlayerStrafing() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetPlayerStrafing:
        if (m_setPlayerStrafing) {
            m_setPlayerStrafing(request.entity != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::CompileShader:
        if (m_compileShader) {
            std::string error;
            response.valid =
                m_compileShader(request.text, error) ? 1u : 0u;
            std::snprintf(response.text, sizeof(response.text), "%s",
                          error.c_str());
        }
        break;
    case CppWorkerRequest::CreateEntity:
        if (m_scene) {
            const std::string name =
                request.text[0] ? request.text : "Entity";
            const Entity entity = m_scene->CreateEntity(name);
            response.entity =
                static_cast<uint32_t>(entity.Handle());
            response.valid = entity.Handle() != entt::null ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::DestroyEntity:
        if (m_scene) {
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (m_scene->Registry().valid(entity)) {
                DestroyEntity(entity);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetName:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<NameComponent>(entity)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    registry.get<NameComponent>(entity).name.c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetName:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                if (registry.all_of<NameComponent>(entity)) {
                    registry.get<NameComponent>(entity).name =
                        request.text;
                } else {
                    registry.emplace<NameComponent>(
                        entity, NameComponent{request.text});
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetRotation:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const glm::quat rotation =
                    registry.get<TransformComponent>(entity).rotation;
                response.x = rotation.x;
                response.y = rotation.y;
                response.z = rotation.z;
                response.w = rotation.w;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetRotation:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                glm::quat rotation(
                    request.w, request.x, request.y, request.z);
                if (glm::length(rotation) > 1e-6f) {
                    rotation = glm::normalize(rotation);
                } else {
                    rotation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
                }
                registry.get<TransformComponent>(entity).rotation =
                    rotation;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetScale:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                const glm::vec3 scale =
                    registry.get<TransformComponent>(entity).scale;
                response.x = scale.x;
                response.y = scale.y;
                response.z = scale.z;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetScale:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                registry.get<TransformComponent>(entity).scale =
                    glm::vec3(request.x, request.y, request.z);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetParent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                response.valid = 1;
                response.entity = UINT32_MAX;
                if (registry.all_of<ParentComponent>(entity)) {
                    const entt::entity parent =
                        registry.get<ParentComponent>(entity).parent;
                    if (parent != entt::null) {
                        response.entity =
                            static_cast<uint32_t>(parent);
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::SetParent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity)) break;
            if (request.other == UINT32_MAX) {
                registry.remove<ParentComponent>(entity);
                response.valid = 1;
                break;
            }
            const entt::entity parent =
                static_cast<entt::entity>(request.other);
            if (!registry.valid(parent) || parent == entity) break;
            // Reject cycles: the requested parent cannot already be a
            // descendant of this entity.
            entt::entity cursor = parent;
            bool cycle = false;
            for (int depth = 0; depth < 256 && cursor != entt::null;
                 ++depth) {
                if (cursor == entity) {
                    cycle = true;
                    break;
                }
                if (!registry.all_of<ParentComponent>(cursor)) break;
                cursor = registry.get<ParentComponent>(cursor).parent;
            }
            if (cycle) break;
            registry.emplace_or_replace<ParentComponent>(
                entity, ParentComponent{parent});
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::HasComponent:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (!registry.valid(entity)) break;
            const std::string key = NormalizeKey(request.text);
            response.valid =
                (key == "name" &&
                 registry.all_of<NameComponent>(entity)) ||
                (key == "transform" &&
                 registry.all_of<TransformComponent>(entity)) ||
                (key == "parent" &&
                 registry.all_of<ParentComponent>(entity)) ||
                (key == "mesh" &&
                 registry.all_of<MeshComponent>(entity)) ||
                (key == "material" &&
                 registry.all_of<MaterialComponent>(entity)) ||
                (key == "model" &&
                 registry.all_of<ModelComponent>(entity)) ||
                (key == "skeletal" &&
                 registry.all_of<SkeletalComponent>(entity)) ||
                (key == "camera" &&
                 registry.all_of<CameraComponent>(entity)) ||
                (key == "directional_light" &&
                 registry.all_of<DirectionalLightComponent>(entity)) ||
                (key == "point_light" &&
                 registry.all_of<PointLightComponent>(entity)) ||
                (key == "spot_light" &&
                 registry.all_of<SpotLightComponent>(entity)) ||
                (key == "rigid_body" &&
                 registry.all_of<RigidBodyComponent>(entity)) ||
                (key == "rigidbody" &&
                 registry.all_of<RigidBodyComponent>(entity)) ||
                (key == "collider" &&
                 registry.all_of<ColliderComponent>(entity)) ||
                (key == "landscape" &&
                 registry.all_of<LandscapeComponent>(entity)) ||
                (key == "vegetation" &&
                 registry.all_of<VegetationComponent>(entity)) ||
                (key == "nav_agent" &&
                 registry.all_of<NavAgentComponent>(entity)) ||
                (key == "soft_body" &&
                 registry.all_of<SoftBodyComponent>(entity)) ||
                (key == "ai_controller" &&
                 registry.all_of<AIControllerComponent>(entity)) ||
                (key == "player_tag" &&
                 registry.all_of<PlayerTagComponent>(entity)) ||
                (key == "perception" &&
                 registry.all_of<PerceptionComponent>(entity)) ||
                (key == "behavior_tree" &&
                 registry.all_of<BehaviorTreeComponent>(entity)) ||
                (key == "audio_source" &&
                 registry.all_of<AudioSourceComponent>(entity)) ||
                (key == "audio_listener" &&
                 registry.all_of<AudioListenerComponent>(entity)) ||
                (key == "post_process_volume" &&
                 registry.all_of<PostProcessVolumeComponent>(entity)) ||
                (key == "particle_emitter" &&
                 registry.all_of<ParticleEmitterComponent>(entity)) ||
                (key == "water" &&
                 registry.all_of<WaterComponent>(entity)) ||
                (key == "groom" &&
                 registry.all_of<GroomComponent>(entity)) ||
                (key == "groom_hair" &&
                 registry.all_of<GroomComponent>(entity)) ||
                (key == "ui_canvas" &&
                 registry.all_of<UICanvasComponent>(entity)) ||
                (key == "animation_state_machine" &&
                 registry.all_of<AnimationStateMachineComponent>(entity))
                ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::GetAxisRaw:
        response.x = m_getAxisRaw ? m_getAxisRaw(request.text) : 0.0f;
        break;
    case CppWorkerRequest::MouseButtonDown:
        response.valid = m_isMouseButtonDown &&
            m_isMouseButtonDown(request.text) ? 1u : 0u;
        break;
    case CppWorkerRequest::GetMousePosition:
        if (m_getMousePosition) {
            const glm::vec2 value = m_getMousePosition();
            response.x = value.x;
            response.y = value.y;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetMouseDelta:
        if (m_getMouseDelta) {
            const glm::vec2 value = m_getMouseDelta();
            response.x = value.x;
            response.y = value.y;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetMouseWheel:
        if (m_getMouseWheel) {
            response.x = m_getMouseWheel();
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AttachBox:
    case CppWorkerRequest::AttachSphere:
    case CppWorkerRequest::AttachCapsule:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<TransformComponent>(entity)) {
                auto& transform =
                    registry.get<TransformComponent>(entity);
                if (registry.all_of<RigidBodyComponent>(entity)) {
                    auto& oldBody =
                        registry.get<RigidBodyComponent>(entity);
                    if (oldBody.bodyId != 0xFFFFFFFFu &&
                        m_physics.destroyBody) {
                        m_physics.destroyBody(oldBody.bodyId);
                    }
                }
                auto& body = registry.emplace_or_replace<
                    RigidBodyComponent>(entity);
                body.motionType = request.valid
                    ? RigidBodyComponent::MotionType::Static
                    : RigidBodyComponent::MotionType::Dynamic;
                body.mass = request.valid ? 0.0f : 1.0f;
                body.useGravity = request.valid == 0;
                auto& collider = registry.emplace_or_replace<
                    ColliderComponent>(entity);
                collider.friction = 0.5f;

                if (kind == CppWorkerRequest::AttachBox) {
                    const glm::vec3 halfExtents(
                        request.x, request.y, request.z);
                    transform.scale = halfExtents * 2.0f;
                    collider.shapeType = ColliderShapeType::Box;
                    collider.halfExtents = halfExtents;
                    if (m_physics.createBox) {
                        body.bodyId = m_physics.createBox(
                            halfExtents, transform.position, entity,
                            request.valid != 0);
                    }
                } else if (kind == CppWorkerRequest::AttachSphere) {
                    const float radius = request.x;
                    transform.scale =
                        glm::vec3(radius * 2.0f);
                    collider.shapeType =
                        ColliderShapeType::Sphere;
                    collider.radius = radius;
                    if (m_physics.createSphere) {
                        body.bodyId = m_physics.createSphere(
                            radius, transform.position, entity,
                            request.valid != 0);
                    }
                } else {
                    const float halfHeight = request.x;
                    const float radius = request.y;
                    transform.scale = glm::vec3(
                        radius * 2.0f,
                        (halfHeight + radius) * 2.0f,
                        radius * 2.0f);
                    collider.shapeType =
                        ColliderShapeType::Capsule;
                    collider.radius = radius;
                    collider.halfHeight = halfHeight;
                    if (m_physics.createCapsule) {
                        body.bodyId = m_physics.createCapsule(
                            halfHeight, radius, transform.position,
                            entity, request.valid != 0);
                    }
                }
                response.valid =
                    body.bodyId != 0xFFFFFFFFu ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::DestroyBody:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<RigidBodyComponent>(entity)) {
                auto& body = registry.get<RigidBodyComponent>(entity);
                if (body.bodyId != 0xFFFFFFFFu) {
                    if (m_physics.destroyBody) {
                        m_physics.destroyBody(body.bodyId);
                    }
                    body.bodyId = 0xFFFFFFFFu;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::ApplyImpulse:
        if (m_applyImpulse && HasPhysicsBody(request.entity)) {
            m_applyImpulse(
                request.entity,
                FromScriptVec3({request.x, request.y, request.z}));
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::BuildNavMesh:
        if (m_buildNavMesh) {
            response.valid = m_buildNavMesh() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::FindPath:
        if (m_findPath) {
            m_cppNavigationPath.clear();
            const glm::vec3 start(request.x, request.y, request.z);
            const glm::vec3 end(request.x2, request.y2, request.z2);
            response.valid = m_findPath(
                start, end, m_cppNavigationPath) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::PathPointCount:
        response.entity = static_cast<uint32_t>(
            m_cppNavigationPath.size());
        response.valid = 1;
        break;
    case CppWorkerRequest::PathPoint:
        if (request.entity < m_cppNavigationPath.size()) {
            const glm::vec3& point =
                m_cppNavigationPath[request.entity];
            response.x = point.x;
            response.y = point.y;
            response.z = point.z;
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::PlaySound:
        if (m_playSound) {
            m_playSound(request.text, request.valid != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::StopSound:
        if (m_stopSound) {
            m_stopSound(request.text);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::IsSoundPlaying:
        if (m_isSoundPlaying) {
            response.valid =
                m_isSoundPlaying(request.text) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::StopAllSounds:
        if (m_stopAllSounds) {
            m_stopAllSounds();
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AttachAudioSource:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                if (m_stopAudioSource) {
                    m_stopAudioSource(request.entity);
                }
                auto& source = registry.emplace_or_replace<
                    AudioSourceComponent>(entity);
                source.soundPath = request.text;
                source.loop = (request.valid & 1u) != 0;
                source.spatial = (request.valid & 2u) != 0;
                source.autoplay = false;
                source.playing = false;
                const int bus = static_cast<int>(request.other);
                source.bus = static_cast<AudioBus>(
                    std::clamp(bus, 0, kAudioBusCount - 1));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::PlaySource:
        if (m_playAudioSource) {
            response.valid =
                m_playAudioSource(request.entity) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::StopSource:
        if (m_stopAudioSource) {
            m_stopAudioSource(request.entity);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::IsSourcePlaying:
        if (m_isAudioSourcePlaying) {
            response.valid =
                m_isAudioSourcePlaying(request.entity) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::SetPostProcessFlag:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessBools, key)) {
                settings.*(field->member) = request.valid != 0;
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessFlag:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessBools, key)) {
                response.entity =
                    settings.*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessFloat:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessFloats, key)) {
                settings.*(field->member) = request.x;
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessFloat:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessFloats, key)) {
                response.x = settings.*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessInt:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            const int value = static_cast<int>(request.valid);
            if (key == "ao_mode") {
                settings.aoMode = static_cast<AmbientOcclusionMode>(
                    std::clamp(value, 0, 2));
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            } else if (const auto* field = FindPostProcessField(
                           kPostProcessInts, key)) {
                settings.*(field->member) = value;
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessInt:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (key == "ao_mode") {
                response.entity = static_cast<uint32_t>(
                    settings.aoMode);
                response.valid = 1;
            } else if (const auto* field = FindPostProcessField(
                           kPostProcessInts, key)) {
                response.entity = static_cast<uint32_t>(
                    settings.*(field->member));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessVec3:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessVec3s, key)) {
                settings.*(field->member) =
                    glm::vec3(request.x, request.y, request.z);
                m_scene->SetHasGlobalPostProcess(true);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetPostProcessVec3:
        if (m_scene) {
            PostProcessSettings& settings =
                m_scene->GlobalPostProcess();
            const std::string key = NormalizeKey(request.text);
            if (const auto* field = FindPostProcessField(
                    kPostProcessVec3s, key)) {
                const glm::vec3 value = settings.*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetPostProcessEnabled:
        if (m_scene) {
            m_scene->SetHasGlobalPostProcess(request.valid != 0);
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::GetPostProcessEnabled:
        if (m_scene) {
            response.valid =
                m_scene->HasGlobalPostProcess() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::FindUICanvas:
        if (m_scene) {
            auto view = m_scene->Registry().view<UICanvasComponent>();
            for (entt::entity entity : view) {
                if (view.get<UICanvasComponent>(entity).canvas.canvasId ==
                    request.text) {
                    response.entity = static_cast<uint32_t>(entity);
                    response.valid = 1;
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::CreateUICanvas:
        if (m_scene) {
            UICanvas* existing = FindCanvasById(*m_scene, request.text);
            if (existing) {
                auto view =
                    m_scene->Registry().view<UICanvasComponent>();
                for (entt::entity entity : view) {
                    if (&view.get<UICanvasComponent>(entity).canvas ==
                        existing) {
                        response.entity =
                            static_cast<uint32_t>(entity);
                        response.valid = 1;
                        break;
                    }
                }
            } else {
                const std::string canvasId = request.text;
                const std::string title =
                    request.text2[0] ? request.text2 : canvasId;
                Entity entity = m_scene->CreateEntity(title);
                auto& component =
                    entity.AddComponent<UICanvasComponent>();
                component.canvas.canvasId = canvasId;
                component.canvas.title = title;
                response.entity =
                    static_cast<uint32_t>(entity.Handle());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::RemoveUICanvas:
        if (m_scene) {
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (m_scene->Registry().valid(entity) &&
                m_scene->Registry().all_of<UICanvasComponent>(entity)) {
                DestroyEntity(entity);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasFlag:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasBools, key)) {
                canvas->*(field->member) = request.valid != 0;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasFlag:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasBools, key)) {
                response.entity =
                    canvas->*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasFloat:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasFloats, key)) {
                canvas->*(field->member) = request.x;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasFloat:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasFloats, key)) {
                response.x = canvas->*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasString:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasStrings, key)) {
                canvas->*(field->member) = request.text2;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasString:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasStrings, key)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    (canvas->*(field->member)).c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUICanvasVec2:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasVec2s, key)) {
                canvas->*(field->member) =
                    glm::vec2(request.x, request.y);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUICanvasVec2:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            const std::string key = NormalizeKey(request.text);
            if (const auto* field =
                    FindPostProcessField(kCanvasVec2s, key)) {
                const glm::vec2 value =
                    canvas->*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::FindUIWidget:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            UICanvas* canvas = canvasByEntity(request.entity);
            response.entity = static_cast<uint32_t>(
                widget - canvas->widgets.data());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AddUIWidget:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            if (!FindWidgetById(*canvas, request.text)) {
                UIWidget widget;
                widget.id = request.text;
                widget.label = request.text;
                widget.type = UIWidgetTypeFromName(request.text2);
                widget.zOrder =
                    static_cast<int>(canvas->widgets.size());
                widget.size = {160.0f, 32.0f};
                canvas->widgets.push_back(std::move(widget));
                response.entity = static_cast<uint32_t>(
                    canvas->widgets.size() - 1);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::RemoveUIWidget:
        if (UICanvas* canvas = canvasByEntity(request.entity)) {
            for (size_t i = 0; i < canvas->widgets.size(); ++i) {
                if (canvas->widgets[i].id != request.text) continue;
                canvas->widgets.erase(
                    canvas->widgets.begin() +
                    static_cast<std::ptrdiff_t>(i));
                for (UIWidget& widget : canvas->widgets) {
                    if (widget.parent == static_cast<int>(i)) {
                        widget.parent = -1;
                    } else if (widget.parent >
                               static_cast<int>(i)) {
                        --widget.parent;
                    }
                }
                response.valid = 1;
                break;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetFlag:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIBools, key)) {
                widget->*(field->member) = request.valid != 0;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetFlag:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIBools, key)) {
                response.entity =
                    widget->*(field->member) ? 1u : 0u;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetFloat:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIFloats, key)) {
                widget->*(field->member) = request.x;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetFloat:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIFloats, key)) {
                response.x = widget->*(field->member);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetInt:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            const int value = static_cast<int>(request.valid);
            if (key == "text_align") {
                widget->textAlign = static_cast<UITextAlign>(
                    std::clamp(value, 0, 2));
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIInts, key)) {
                widget->*(field->member) = value;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetInt:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (key == "text_align") {
                response.entity = static_cast<uint32_t>(
                    widget->textAlign);
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIInts, key)) {
                response.entity = static_cast<uint32_t>(
                    widget->*(field->member));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetString:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string combined = request.text2;
            const std::size_t split = combined.find('\n');
            const std::string key = NormalizeKey(
                combined.substr(0, split));
            const std::string value = split == std::string::npos
                ? std::string{}
                : combined.substr(split + 1);
            if (key == "text_align") {
                const std::string align = NormalizeKey(value);
                widget->textAlign = align == "center"
                    ? UITextAlign::Center
                    : align == "right" ? UITextAlign::Right
                                       : UITextAlign::Left;
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIStrings, key)) {
                widget->*(field->member) = value;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetString:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (key == "text_align") {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    widget->textAlign == UITextAlign::Center
                        ? "center"
                        : widget->textAlign == UITextAlign::Right
                              ? "right"
                              : "left");
                response.valid = 1;
            } else if (const auto* field =
                           FindPostProcessField(kUIStrings, key)) {
                std::snprintf(
                    response.text, sizeof(response.text), "%s",
                    (widget->*(field->member)).c_str());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetVec2:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec2s, key)) {
                widget->*(field->member) =
                    glm::vec2(request.x, request.y);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetVec2:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec2s, key)) {
                const glm::vec2 value =
                    widget->*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetUIWidgetVec4:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec4s, key)) {
                widget->*(field->member) = glm::vec4(
                    request.x, request.y, request.z, request.w);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetUIWidgetVec4:
        if (UIWidget* widget =
                widgetByEntity(request.entity, request.text)) {
            const std::string key = NormalizeKey(request.text2);
            if (const auto* field =
                    FindPostProcessField(kUIVec4s, key)) {
                const glm::vec4 value =
                    widget->*(field->member);
                response.x = value.x;
                response.y = value.y;
                response.z = value.z;
                response.w = value.w;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::PollUIEvent:
        if (!m_cppUIEvents.empty()) {
            const UIEvent event = std::move(m_cppUIEvents.front());
            m_cppUIEvents.pop_front();
            response.entity = static_cast<uint32_t>(
                event.canvasEntity);
            response.other =
                static_cast<uint32_t>(event.type);
            response.x = event.value;
            response.w = event.checked ? 1.0f : 0.0f;
            std::snprintf(
                response.text, sizeof(response.text), "%s",
                event.canvasId.c_str());
            std::snprintf(
                response.text2, sizeof(response.text2), "%s",
                event.widgetId.c_str());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::IsPointerOverUI:
        if (m_isPointerOverUI) {
            response.valid =
                m_isPointerOverUI() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::FindLandscape:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeSampleHeight:
        if (m_scene) {
            auto view = m_scene->Registry().view<LandscapeComponent>();
            if (!view.empty()) {
                const auto& landscape =
                    view.get<LandscapeComponent>(*view.begin());
                response.x = LandscapeMesh::SampleHeight(
                    landscape, request.x, request.y);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::LandscapeSetHeight:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                if (landscape.sizeX >= 2 && landscape.sizeZ >= 2) {
                    const float gx =
                        request.x / std::max(0.001f, landscape.scale) +
                        landscape.sizeX * 0.5f;
                    const float gz =
                        request.y / std::max(0.001f, landscape.scale) +
                        landscape.sizeZ * 0.5f;
                    const int x = std::clamp(
                        static_cast<int>(std::lround(gx)), 0,
                        landscape.sizeX - 1);
                    const int z = std::clamp(
                        static_cast<int>(std::lround(gz)), 0,
                        landscape.sizeZ - 1);
                    const size_t index =
                        static_cast<size_t>(z) * landscape.sizeX + x;
                    if (index < landscape.heights.size()) {
                        landscape.heights[index] = request.z;
                        if (registry.all_of<MeshComponent>(entity)) {
                            auto& mesh =
                                registry.get<MeshComponent>(entity);
                            if (mesh.mesh) {
                                LandscapeMesh::UpdateMesh(
                                    landscape, *mesh.mesh);
                            }
                        }
                        if (registry.all_of<VegetationComponent>(
                                entity)) {
                            registry.get<VegetationComponent>(entity)
                                .dirty = true;
                        }
                        response.valid = 1;
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeApplyBrush:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                LandscapeBrushSettings settings;
                const int tool = static_cast<int>(request.valid);
                settings.tool = static_cast<LandscapeBrush>(
                    std::clamp(tool, 0, 6));
                settings.radius = request.z;
                settings.strength = request.w;
                settings.falloff = request.x2;
                settings.flattenHeight = 0.0f;
                settings.noiseFrequency = 0.15f;
                settings.terraceStep = 1.0f;
                const int changed = LandscapeMesh::ApplyBrush(
                    landscape, settings, request.x, request.y,
                    request.y2);
                if (changed > 0) {
                    if (registry.all_of<MeshComponent>(entity)) {
                        auto& mesh =
                            registry.get<MeshComponent>(entity);
                        if (mesh.mesh) {
                            LandscapeMesh::UpdateMesh(
                                landscape, *mesh.mesh);
                        }
                    }
                    if (registry.all_of<VegetationComponent>(entity)) {
                        registry.get<VegetationComponent>(entity)
                            .dirty = true;
                    }
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::LandscapeResetHeights:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            auto view = registry.view<LandscapeComponent>();
            if (!view.empty()) {
                const entt::entity entity = *view.begin();
                auto& landscape = view.get<LandscapeComponent>(entity);
                LandscapeMesh::ResetHeights(landscape);
                if (registry.all_of<MeshComponent>(entity)) {
                    auto& mesh = registry.get<MeshComponent>(entity);
                    if (mesh.mesh) {
                        LandscapeMesh::UpdateMesh(landscape, *mesh.mesh);
                    }
                }
                if (registry.all_of<VegetationComponent>(entity)) {
                    registry.get<VegetationComponent>(entity).dirty =
                        true;
                }
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::FindVegetation:
        if (m_scene) {
            auto view = m_scene->Registry().view<VegetationComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::VegetationRegenerate:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<LandscapeComponent,
                                VegetationComponent>(entity)) {
                VegetationGenerator::Generate(
                    registry.get<LandscapeComponent>(entity),
                    registry.get<VegetationComponent>(entity));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::VegetationClear:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                VegetationGenerator::Clear(
                    registry.get<VegetationComponent>(entity));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::VegetationSetBool:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationBools, key)) {
                    vegetation.*(field->member) =
                        request.valid != 0;
                    vegetation.dirty = true;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationGetBool:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationBools, key)) {
                    response.entity =
                        vegetation.*(field->member) ? 1u : 0u;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationSetInt:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (key == "seed") {
                    vegetation.seed =
                        static_cast<uint32_t>(request.valid);
                    vegetation.dirty = true;
                    response.valid = 1;
                } else if (const auto* field =
                               FindPostProcessField(
                                   kVegetationInts, key)) {
                    vegetation.*(field->member) =
                        static_cast<int>(request.valid);
                    vegetation.dirty = true;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationGetInt:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (key == "seed") {
                    response.entity = vegetation.seed;
                    response.valid = 1;
                } else if (const auto* field =
                               FindPostProcessField(
                                   kVegetationInts, key)) {
                    response.entity = static_cast<uint32_t>(
                        vegetation.*(field->member));
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationSetFloat:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationFloats, key)) {
                    vegetation.*(field->member) = request.x;
                    vegetation.dirty = true;
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationGetFloat:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                const std::string key = NormalizeKey(request.text);
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                if (const auto* field = FindPostProcessField(
                        kVegetationFloats, key)) {
                    response.x = vegetation.*(field->member);
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::VegetationInstanceCount:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<VegetationComponent>(entity)) {
                auto& vegetation =
                    registry.get<VegetationComponent>(entity);
                const bool grass = (request.valid & 1u) != 0;
                const bool painted = (request.valid & 2u) != 0;
                size_t count = 0;
                if (grass) {
                    count = painted
                        ? vegetation.paintedGrassInstances.size()
                        : vegetation.grassInstances.size();
                } else {
                    count = painted
                        ? vegetation.paintedTreeInstances.size()
                        : vegetation.treeInstances.size();
                    for (const auto& type : vegetation.treeTypes) {
                        count += painted
                            ? type.paintedInstances.size()
                            : type.generatedInstances.size();
                    }
                }
                response.entity = static_cast<uint32_t>(
                    std::min<size_t>(count, UINT32_MAX));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetAnimationState:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                response.valid = AnimationSystem::SetState(
                    registry.get<AnimationStateMachineComponent>(entity),
                    request.text) ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::SetAnimationParameter:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                AnimationSystem::SetParameter(
                    machine, request.text, request.x);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::GetAnimationParameter:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                response.x = AnimationSystem::GetParameter(
                    machine, request.text);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::TriggerAnimationParameter:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<AnimationStateMachineComponent>(
                    entity)) {
                auto& machine =
                    registry.get<AnimationStateMachineComponent>(entity);
                AnimationSystem::Trigger(machine, request.text);
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::PlayAnimationClip:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                auto& skeletal =
                    registry.get<SkeletalComponent>(entity);
                skeletal.activeAnimation =
                    static_cast<int>(request.valid);
                skeletal.currentTime = 0.0f;
                skeletal.isAnimated = true;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::StopAnimation:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                auto& skeletal =
                    registry.get<SkeletalComponent>(entity);
                skeletal.isAnimated = false;
                skeletal.currentTime = 0.0f;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetAnimationSpeed:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                bool applied = false;
                if (registry.all_of<SkeletalComponent>(entity)) {
                    registry.get<SkeletalComponent>(entity)
                        .animationSpeed = request.x;
                    applied = true;
                }
                if (registry.all_of<AnimationStateMachineComponent>(
                        entity)) {
                    registry.get<AnimationStateMachineComponent>(entity)
                        .playbackSpeed = request.x;
                    applied = true;
                }
                response.valid = applied ? 1u : 0u;
            }
        }
        break;
    case CppWorkerRequest::GetAnimationSpeed:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                response.x =
                    registry.get<SkeletalComponent>(entity)
                        .animationSpeed;
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::AnimationClipCount:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                size_t count = 0;
                if (m_assets &&
                    registry.all_of<SkeletalComponent>(entity)) {
                    const auto& skeletal =
                        registry.get<SkeletalComponent>(entity);
                    if (!skeletal.sourcePath.empty()) {
                        const auto model =
                            m_assets->LoadModel(
                                skeletal.sourcePath);
                        count = model.animationNames.size();
                    }
                }
                if (count == 0 &&
                    registry.all_of<AnimationStateMachineComponent>(
                        entity)) {
                    count = registry
                                .get<AnimationStateMachineComponent>(
                                    entity)
                                .states.size();
                }
                response.entity = static_cast<uint32_t>(
                    std::min<size_t>(count, UINT32_MAX));
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::AnimationClipName:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity)) {
                const uint32_t index = request.valid;
                std::string name;
                if (m_assets &&
                    registry.all_of<SkeletalComponent>(entity)) {
                    const auto& skeletal =
                        registry.get<SkeletalComponent>(entity);
                    if (!skeletal.sourcePath.empty()) {
                        const auto model =
                            m_assets->LoadModel(
                                skeletal.sourcePath);
                        if (index < model.animationNames.size()) {
                            name = model.animationNames[index];
                        }
                    }
                }
                if (name.empty() &&
                    registry.all_of<AnimationStateMachineComponent>(
                        entity)) {
                    const auto& machine =
                        registry.get<AnimationStateMachineComponent>(
                            entity);
                    if (index < machine.states.size()) {
                        name = machine.states[index].name;
                    }
                }
                if (!name.empty()) {
                    std::snprintf(
                        response.text, sizeof(response.text), "%s",
                        name.c_str());
                    response.valid = 1;
                }
            }
        }
        break;
    case CppWorkerRequest::AnimationClipDuration:
        if (m_scene && m_assets) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<SkeletalComponent>(entity)) {
                const auto& skeletal =
                    registry.get<SkeletalComponent>(entity);
                if (!skeletal.sourcePath.empty()) {
                    const auto model =
                        m_assets->LoadModel(skeletal.sourcePath);
                    const uint32_t index = request.valid;
                    if (index < model.animationDurations.size()) {
                        response.x = model.animationDurations[index];
                        response.valid = 1;
                    }
                }
            }
        }
        break;
    case CppWorkerRequest::AssetExists:
        if (m_assets) {
            const std::string path =
                m_assets->ResolvePath(request.text);
            std::error_code ec;
            response.valid =
                std::filesystem::exists(path, ec) ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::AssetResolvePath:
        if (m_assets) {
            const std::string path =
                m_assets->ResolvePath(request.text);
            std::snprintf(response.text, sizeof(response.text), "%s",
                          path.c_str());
            response.valid = 1;
        }
        break;
    case CppWorkerRequest::AssetLoadModel:
        if (m_assets) {
            const auto model = m_assets->LoadModel(request.text);
            response.valid =
                !model.meshes.empty() ? 1u : 0u;
        }
        break;
    case CppWorkerRequest::FindParticleEmitter:
        if (m_scene) {
            auto view =
                m_scene->Registry().view<ParticleEmitterComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetParticleFlag:
    case CppWorkerRequest::SetParticleFloat:
    case CppWorkerRequest::SetParticleInt:
    case CppWorkerRequest::SetParticleVec3:
    case CppWorkerRequest::SetParticleVec4:
    case CppWorkerRequest::GetParticleFlag:
    case CppWorkerRequest::GetParticleFloat:
    case CppWorkerRequest::GetParticleInt:
    case CppWorkerRequest::GetParticleVec3:
    case CppWorkerRequest::GetParticleVec4:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<ParticleEmitterComponent>(entity)) {
                auto& particle =
                    registry.get<ParticleEmitterComponent>(entity);
                const std::string key = NormalizeKey(request.text);
                switch (kind) {
                case CppWorkerRequest::SetParticleFlag:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleBools, key)) {
                        particle.*(field->member) =
                            request.valid != 0;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleFlag:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleBools, key)) {
                        response.entity =
                            particle.*(field->member) ? 1u : 0u;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleFloat:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleFloats, key)) {
                        particle.*(field->member) = request.x;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleFloat:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleFloats, key)) {
                        response.x = particle.*(field->member);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleInt:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleInts, key)) {
                        particle.*(field->member) = request.valid;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleInt:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleInts, key)) {
                        response.entity = particle.*(field->member);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleVec3:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec3s, key)) {
                        particle.*(field->member) =
                            glm::vec3(request.x, request.y,
                                      request.z);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleVec3:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec3s, key)) {
                        const glm::vec3 value =
                            particle.*(field->member);
                        response.x = value.x;
                        response.y = value.y;
                        response.z = value.z;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetParticleVec4:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec4s, key)) {
                        particle.*(field->member) = glm::vec4(
                            request.x, request.y, request.z,
                            request.w);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetParticleVec4:
                    if (const auto* field =
                            FindPostProcessField(
                                kParticleVec4s, key)) {
                        const glm::vec4 value =
                            particle.*(field->member);
                        response.x = value.x;
                        response.y = value.y;
                        response.z = value.z;
                        response.w = value.w;
                        response.valid = 1;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    case CppWorkerRequest::FindGroom:
        if (m_scene) {
            auto view = m_scene->Registry().view<GroomComponent>();
            if (!view.empty()) {
                response.entity = static_cast<uint32_t>(*view.begin());
                response.valid = 1;
            }
        }
        break;
    case CppWorkerRequest::SetGroomFlag:
    case CppWorkerRequest::GetGroomFlag:
    case CppWorkerRequest::SetGroomFloat:
    case CppWorkerRequest::GetGroomFloat:
    case CppWorkerRequest::SetGroomInt:
    case CppWorkerRequest::GetGroomInt:
    case CppWorkerRequest::SetGroomVec3:
    case CppWorkerRequest::GetGroomVec3:
    case CppWorkerRequest::SetGroomString:
    case CppWorkerRequest::GetGroomString:
        if (m_scene) {
            auto& registry = m_scene->Registry();
            const entt::entity entity =
                static_cast<entt::entity>(request.entity);
            if (registry.valid(entity) &&
                registry.all_of<GroomComponent>(entity)) {
                auto& groom = registry.get<GroomComponent>(entity);
                const std::string key = NormalizeKey(request.text);
                switch (kind) {
                case CppWorkerRequest::SetGroomFlag:
                    if (const auto* field =
                            FindPostProcessField(kGroomBools, key)) {
                        groom.*(field->member) =
                            request.valid != 0;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomFlag:
                    if (const auto* field =
                            FindPostProcessField(kGroomBools, key)) {
                        response.entity =
                            groom.*(field->member) ? 1u : 0u;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomFloat:
                    if (const auto* field =
                            FindPostProcessField(kGroomFloats, key)) {
                        groom.*(field->member) = request.x;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomFloat:
                    if (const auto* field =
                            FindPostProcessField(kGroomFloats, key)) {
                        response.x = groom.*(field->member);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomInt:
                    if (key == "seed") {
                        groom.seed = request.valid;
                        response.valid = 1;
                    } else if (const auto* field =
                                   FindPostProcessField(
                                       kGroomInts, key)) {
                        groom.*(field->member) =
                            static_cast<int>(request.valid);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomInt:
                    if (key == "seed") {
                        response.entity = groom.seed;
                        response.valid = 1;
                    } else if (const auto* field =
                                   FindPostProcessField(
                                       kGroomInts, key)) {
                        response.entity = static_cast<uint32_t>(
                            groom.*(field->member));
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomVec3:
                    if (const auto* field =
                            FindPostProcessField(kGroomVec3s, key)) {
                        groom.*(field->member) = glm::vec3(
                            request.x, request.y, request.z);
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomVec3:
                    if (const auto* field =
                            FindPostProcessField(kGroomVec3s, key)) {
                        const glm::vec3 value =
                            groom.*(field->member);
                        response.x = value.x;
                        response.y = value.y;
                        response.z = value.z;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::SetGroomString:
                    if (const auto* field =
                            FindPostProcessField(kGroomStrings, key)) {
                        groom.*(field->member) = request.text2;
                        response.valid = 1;
                    }
                    break;
                case CppWorkerRequest::GetGroomString:
                    if (const auto* field =
                            FindPostProcessField(kGroomStrings, key)) {
                        std::snprintf(
                            response.text, sizeof(response.text), "%s",
                            (groom.*(field->member)).c_str());
                        response.valid = 1;
                    }
                    break;
                default:
                    break;
                }
            }
        }
        break;
    }
    return response;
}

bool ScriptEngine::HandleWorkerPacket(CppWorkerPacket& packet) {
    const auto type = static_cast<CppWorkerMessage>(packet.type);
    if (type == CppWorkerMessage::Log) {
        AddLog(packet.text);
        return true;
    }
    if (type == CppWorkerMessage::Request) {
        CppWorkerPacket response = MakeWorkerResponse(packet);
        return WriteWorkerPacket(m_cppWorkerWrite, response);
    }
    return true;
}

bool ScriptEngine::StartCppWorker() {
    StopCppWorker();
    if (m_cppModules.empty()) return true;

#if !defined(__unix__) && !defined(__APPLE__)
    m_cppBuildErrors += "C++ script worker is only supported on Unix.\n";
    return false;
#else
    std::error_code ec;
    std::filesystem::path workerPath;
#if defined(__linux__)
    const std::filesystem::path executable =
        std::filesystem::read_symlink("/proc/self/exe", ec);
    if (!ec) {
        workerPath = executable.parent_path() / "GaleshiScriptWorker";
    }
#endif
    if (workerPath.empty() || !std::filesystem::exists(workerPath, ec)) {
        workerPath = std::filesystem::current_path() / "GaleshiScriptWorker";
    }
    if (!std::filesystem::exists(workerPath, ec)) {
        m_cppBuildErrors +=
            "C++ script worker executable was not found: " +
            workerPath.string() + "\n";
        AddLog("C++ script worker executable was not found: " +
               workerPath.string());
        return false;
    }

    int toWorker[2] = {-1, -1};
    int fromWorker[2] = {-1, -1};
    if (pipe(toWorker) != 0 || pipe(fromWorker) != 0) {
        if (toWorker[0] >= 0) { close(toWorker[0]); close(toWorker[1]); }
        if (fromWorker[0] >= 0) { close(fromWorker[0]); close(fromWorker[1]); }
        m_cppBuildErrors += "Could not create C++ worker pipes.\n";
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(toWorker[0]); close(toWorker[1]);
        close(fromWorker[0]); close(fromWorker[1]);
        m_cppBuildErrors += "Could not start C++ script worker.\n";
        return false;
    }
    if (pid == 0) {
#if defined(__linux__)
        // Do not leave an orphan worker behind if the editor exits abruptly.
        prctl(PR_SET_PDEATHSIG, SIGKILL);
#endif
        rlimit coreLimit{0, 0};
        setrlimit(RLIMIT_CORE, &coreLimit);
        rlimit memoryLimit{2ull * 1024ull * 1024ull * 1024ull,
                           2ull * 1024ull * 1024ull * 1024ull};
        setrlimit(RLIMIT_AS, &memoryLimit);

        close(toWorker[1]);
        close(fromWorker[0]);
        dup2(toWorker[0], STDIN_FILENO);
        dup2(fromWorker[1], STDOUT_FILENO);
        close(toWorker[0]);
        close(fromWorker[1]);
        execl(workerPath.string().c_str(), "GaleshiScriptWorker",
              static_cast<char*>(nullptr));
        _exit(127);
    }

    close(toWorker[0]);
    close(fromWorker[1]);
    m_cppWorkerPid = static_cast<int>(pid);
    m_cppWorkerWrite = toWorker[1];
    m_cppWorkerRead = fromWorker[0];
    m_cppWorkerReady = false;

    // LOAD
    for (const auto& [path, module] : m_cppModules) {
        CppWorkerPacket load;
        load.type = static_cast<uint32_t>(CppWorkerMessage::Load);
        std::snprintf(load.text, sizeof(load.text), "%s",
                      module.libraryPath.c_str());
        if (!WriteWorkerPacket(m_cppWorkerWrite, load)) {
            StopCppWorker("C++ worker exited while loading modules.");
            return false;
        }
    }
    CppWorkerPacket start;
    start.type = static_cast<uint32_t>(CppWorkerMessage::Start);
    if (!WriteWorkerPacket(m_cppWorkerWrite, start)) {
        StopCppWorker("C++ worker exited while starting.");
        return false;
    }

    bool loadFailed = false;
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            StopCppWorker("C++ worker startup timed out.");
            return false;
        }
        pollfd descriptor{m_cppWorkerRead, POLLIN, 0};
        const int ready = poll(&descriptor, 1,
                               static_cast<int>(remaining.count()));
        if (ready == 0) {
            StopCppWorker("C++ worker startup timed out.");
            return false;
        }
        if (ready < 0) {
            StopCppWorker("C++ worker poll failed during startup.");
            return false;
        }
        CppWorkerPacket packet;
        if (!ReadWorkerPacket(m_cppWorkerRead, packet)) {
            StopCppWorker("C++ worker exited during startup.");
            return false;
        }
        const auto type = static_cast<CppWorkerMessage>(packet.type);
        if (type == CppWorkerMessage::Ready) {
            m_cppWorkerReady = true;
            m_cppBuildOutput += "C++ worker ready.\n";
            return !loadFailed;
        }
        if (type == CppWorkerMessage::LoadResult) {
            if (!packet.valid) {
                loadFailed = true;
                m_cppBuildErrors += "C++ worker load error: " +
                    std::string(packet.text) + "\n";
                AddLog("C++ worker load error: " + std::string(packet.text));
            }
            continue;
        }
        if (!HandleWorkerPacket(packet)) {
            StopCppWorker("C++ worker communication failed during startup.");
            return false;
        }
    }
#endif
}

void ScriptEngine::StopCppWorker(const std::string& reason) {
#if defined(__unix__) || defined(__APPLE__)
    if (m_cppWorkerWrite >= 0) {
        CppWorkerPacket quit;
        quit.type = static_cast<uint32_t>(CppWorkerMessage::Quit);
        WriteWorkerPacket(m_cppWorkerWrite, quit);
        close(m_cppWorkerWrite);
        m_cppWorkerWrite = -1;
    }
    if (m_cppWorkerRead >= 0) {
        close(m_cppWorkerRead);
        m_cppWorkerRead = -1;
    }
    if (m_cppWorkerPid > 0) {
        kill(static_cast<pid_t>(m_cppWorkerPid), SIGKILL);
        int status = 0;
        waitpid(static_cast<pid_t>(m_cppWorkerPid), &status, 0);
        m_cppWorkerPid = -1;
    }
#endif
    m_cppWorkerReady = false;
    if (!reason.empty()) {
        m_cppBuildErrors += reason + "\n";
        m_cppBuildOutput += reason + "\n";
        AddLog(reason);
    }
}

void ScriptEngine::BindFunctions() {
    BuildApi();
}

bool ScriptEngine::HasPhysicsBody(uint32_t entityId) const {
    if (!m_scene) return false;
    const entt::entity entity = static_cast<entt::entity>(entityId);
    auto& registry = m_scene->Registry();
    if (!registry.valid(entity) ||
        !registry.all_of<RigidBodyComponent>(entity)) {
        return false;
    }
    return registry.get<RigidBodyComponent>(entity).bodyId != 0xFFFFFFFFu;
}

void ScriptEngine::DestroyEntity(entt::entity entity) {
    if (!m_scene || !m_scene->Registry().valid(entity)) return;
    auto& registry = m_scene->Registry();

    if (registry.all_of<RigidBodyComponent>(entity)) {
        auto& body = registry.get<RigidBodyComponent>(entity);
        if (body.bodyId != 0xFFFFFFFFu && m_physics.destroyBody) {
            m_physics.destroyBody(body.bodyId);
        }
    }
    if (registry.all_of<SoftBodyComponent>(entity)) {
        auto& softBody = registry.get<SoftBodyComponent>(entity);
        if (softBody.softBodyId != 0xFFFFFFFFu && m_physics.destroyBody) {
            m_physics.destroyBody(softBody.softBodyId);
        }
    }

    registry.destroy(entity);
}

void ScriptEngine::BindECS(Scene& scene) {
    BuildApi();
    m_scene = &scene;
}

void ScriptEngine::BindAssets(AssetManager* assets) {
    BuildApi();
    m_assets = assets;
}

void ScriptEngine::BindInput(std::function<bool(const std::string&)> isKeyDown) {
    BuildApi();
    m_isKeyDown = std::move(isKeyDown);
}

void ScriptEngine::BindInputAxes(
    std::function<float(const std::string&)> getAxis,
    std::function<float(const std::string&)> getAxisRaw,
    std::function<glm::vec2()> getMousePosition,
    std::function<glm::vec2()> getMouseDelta,
    std::function<float()> getMouseWheel,
    std::function<bool(const std::string&)> isMouseButtonDown) {
    BuildApi();
    m_getAxis = std::move(getAxis);
    m_getAxisRaw = std::move(getAxisRaw);
    m_getMousePosition = std::move(getMousePosition);
    m_getMouseDelta = std::move(getMouseDelta);
    m_getMouseWheel = std::move(getMouseWheel);
    m_isMouseButtonDown = std::move(isMouseButtonDown);
}

void ScriptEngine::BindVelocity(
    std::function<glm::vec3(uint32_t)> getVel,
    std::function<void(uint32_t, const glm::vec3&)> setVel) {
    BuildApi();
    m_getVelocity = std::move(getVel);
    m_setVelocity = std::move(setVel);
}

void ScriptEngine::BindAudio(
    std::function<void(const std::string&, bool)> playSound,
    std::function<void(const std::string&)> stopSound,
    std::function<bool(const std::string&)> isSoundPlaying,
    std::function<void()> stopAllSounds) {
    BuildApi();
    m_playSound = std::move(playSound);
    m_stopSound = std::move(stopSound);
    m_isSoundPlaying = std::move(isSoundPlaying);
    m_stopAllSounds = std::move(stopAllSounds);
}

void ScriptEngine::BindAudio(std::function<void(const std::string&)> playSound) {
    BuildApi();
    m_playSound = [play = std::move(playSound)](const std::string& path, bool) {
        if (play) play(path);
    };
}

void ScriptEngine::BindAudio3D(
    std::function<bool(uint32_t)> playSource,
    std::function<void(uint32_t)> stopSource,
    std::function<bool(uint32_t)> isPlayingSource) {
    BuildApi();
    m_playAudioSource = std::move(playSource);
    m_stopAudioSource = std::move(stopSource);
    m_isAudioSourcePlaying = std::move(isPlayingSource);
}

void ScriptEngine::PushUIEvent(const UIEvent& event) {
    constexpr std::size_t kMaxQueuedUIEvents = 256;
    if (m_cppUIEvents.size() >= kMaxQueuedUIEvents) {
        m_cppUIEvents.pop_front();
    }
    m_cppUIEvents.push_back(event);
}

void ScriptEngine::BindUIState(std::function<bool()> pointerOverUI) {
    BuildApi();
    m_isPointerOverUI = std::move(pointerOverUI);
}

void ScriptEngine::BindPhysics(const PhysicsBindings& bindings) {
    BuildApi();
    m_physics = bindings;
}

void ScriptEngine::BindPhysics(
    std::function<uint32_t(const glm::vec3&, const glm::vec3&, entt::entity)>
        createBox) {
    BuildApi();
    PhysicsBindings bindings;
    if (createBox) {
        bindings.createBox = [createBox = std::move(createBox)](
                                 const glm::vec3& halfExtents,
                                 const glm::vec3& position,
                                 entt::entity entity,
                                 bool) {
            return createBox(halfExtents, position, entity);
        };
    }
    m_physics = std::move(bindings);
}

void ScriptEngine::BindImpulse(
    std::function<void(uint32_t, const glm::vec3&)> applyImpulse) {
    BuildApi();
    m_applyImpulse = std::move(applyImpulse);
}

void ScriptEngine::BindAI() {
    BuildApi();
}

void ScriptEngine::BindNavigation(
    std::function<bool(const glm::vec3&, const glm::vec3&,
                       std::vector<glm::vec3>&)> findPath,
    std::function<bool()> buildNavMesh) {
    BuildApi();
    m_findPath = std::move(findPath);
    m_buildNavMesh = std::move(buildNavMesh);
}

void ScriptEngine::BindEngine(std::function<void()> requestExit) {
    BuildApi();
    m_requestExit = std::move(requestExit);
}

void ScriptEngine::BindCamera(std::function<bool(int)> setMode,
                              std::function<int()> getMode,
                              std::function<glm::vec3()> getForward,
                              std::function<glm::vec3()> getRight) {
    BuildApi();
    m_setCameraMode = std::move(setMode);
    m_getCameraMode = std::move(getMode);
    m_getCameraForward = std::move(getForward);
    m_getCameraRight = std::move(getRight);
}

void ScriptEngine::BindPlayer(std::function<bool()> getStrafing,
                              std::function<void(bool)> setStrafing) {
    BuildApi();
    m_getPlayerStrafing = std::move(getStrafing);
    m_setPlayerStrafing = std::move(setStrafing);
}

void ScriptEngine::BindShader(
    std::function<bool(const std::string&, std::string&)> compileShader) {
    BuildApi();
    m_compileShader = std::move(compileShader);
}

bool ScriptEngine::CheckCppSyntax(const std::string& source,
                                  const std::string& sourcePath,
                                  std::string& diagnostics) {
    if (source.empty()) {
        diagnostics.clear();
        return true;
    }
#if !defined(__unix__) && !defined(__APPLE__)
    diagnostics = "C++ syntax checking is only supported on Unix.";
    return false;
#else
    namespace fs = std::filesystem;
    std::error_code pathEc;
    const fs::path sourceFile =
        fs::absolute(sourcePath, pathEc).lexically_normal();
    const fs::path parent = sourceFile.parent_path();
    const fs::path syntaxRoot =
        fs::temp_directory_path() / "galeshi_cpp_syntax";
    std::error_code ec;
    fs::create_directories(syntaxRoot, ec);
    if (ec) {
        diagnostics = "Could not create syntax-check directory: " +
                      ec.message();
        return false;
    }

    const std::string hash = std::to_string(
        std::hash<std::string>{}(sourcePath + ":" + source));
    const fs::path temporarySource =
        syntaxRoot / (sourceFile.stem().string() + "_" + hash + ".cpp");
    const fs::path temporaryOutput =
        syntaxRoot / (sourceFile.stem().string() + "_" + hash + ".so");
    std::ofstream out(temporarySource, std::ios::binary);
    if (!out.is_open()) {
        diagnostics = "Could not write syntax-check source.";
        return false;
    }
    out << source;
    out.close();

    std::string projectRoot = ProjectManager::GetProjectPath();
    if (projectRoot.empty()) projectRoot = fs::current_path().string();
    const std::string includeRoot =
#ifdef GALESHI_SCRIPT_INCLUDE_DIR
        GALESHI_SCRIPT_INCLUDE_DIR;
#else
        "engine/include";
#endif
    const bool success = CompileCppScript(
        temporarySource, temporaryOutput, includeRoot, projectRoot,
        diagnostics, parent.string(), true);
    std::error_code removeEc;
    fs::remove(temporarySource, removeEc);
    fs::remove(temporaryOutput, removeEc);
    if (success) diagnostics.clear();
    return success;
#endif
}

bool ScriptEngine::CheckLuaSyntax(const std::string& source,
                                  const std::string& sourcePath,
                                  std::string& error) {
    // Use a private Lua state: the live state is not safe to compile from a
    // background syntax-check thread.
    lua_State* state = luaL_newstate();
    if (!state) {
        error = "Could not create Lua syntax-check state";
        return false;
    }
    const int status = luaL_loadbuffer(
        state, source.data(), source.size(), sourcePath.c_str());
    if (status == LUA_OK) {
        lua_pop(state, 1);
        lua_close(state);
        error.clear();
        return true;
    }
    const char* message = lua_tostring(state, -1);
    error = message ? message : "Lua syntax error";
    lua_pop(state, 1);
    lua_close(state);
    return false;
}

bool ScriptEngine::ReloadCppScripts(bool force) {
    namespace fs = std::filesystem;
    std::map<std::string, fs::path> sources;
    std::string projectRoot = ProjectManager::GetProjectPath();
    if (projectRoot.empty()) {
        projectRoot = fs::current_path().string();
    }

    std::set<std::string> roots;
    const fs::path current = fs::current_path();
    const fs::path project(projectRoot);
    for (const fs::path& root : {
             project / "scripts",
             project / "assets/scripts",
             current / "scripts",
             current / "assets/scripts"}) {
        std::error_code ec;
        const fs::path absolute = fs::absolute(root, ec).lexically_normal();
        if (ec || !fs::is_directory(absolute, ec)) continue;
        if (!roots.insert(absolute.string()).second) continue;
        for (fs::directory_iterator it(absolute, ec), end; !ec && it != end;
             it.increment(ec)) {
            if (!it->is_regular_file(ec)) continue;
            const fs::path source = it->path();
            if (!IsCppScriptPath(source)) continue;
            sources[fs::absolute(source).lexically_normal().string()] = source;
        }
    }

    bool modulesChanged = false;
    for (auto it = m_cppModules.begin(); it != m_cppModules.end();) {
        if (sources.find(it->first) == sources.end()) {
            AddLog("Unloaded C++ script: " + it->first);
            it = m_cppModules.erase(it);
            modulesChanged = true;
        } else {
            ++it;
        }
    }

#if !defined(__unix__) && !defined(__APPLE__)
    if (!sources.empty()) {
        AddLog("C++ scripts are not supported on this platform");
    }
    return false;
#else
    const fs::path cacheRoot =
        fs::temp_directory_path() / "galeshi_cpp_scripts";
    std::error_code cacheEc;
    fs::create_directories(cacheRoot, cacheEc);

    const std::string includeRoot =
#ifdef GALESHI_SCRIPT_INCLUDE_DIR
        GALESHI_SCRIPT_INCLUDE_DIR;
#else
        "engine/include";
#endif

    if (force) {
        m_cppBuildOutput.clear();
        m_cppBuildErrors.clear();
    }
    if (force && sources.empty()) {
        m_cppBuildOutput = "No C++ scripts found in scripts/ or assets/scripts/.";
    }

    bool allSucceeded = true;
    bool buildStarted = false;
    auto beginBuild = [&] {
        if (buildStarted) return;
        buildStarted = true;
        if (!force) {
            m_cppBuildOutput.clear();
            m_cppBuildErrors.clear();
        }
    };
    for (const auto& [pathKey, source] : sources) {
        std::error_code timeEc;
        const auto writeTime = fs::last_write_time(source, timeEc);
        if (timeEc) continue;

        auto moduleIt = m_cppModules.find(pathKey);
        if (!force && moduleIt != m_cppModules.end() &&
            moduleIt->second.writeTime == writeTime) {
            continue;
        }
        auto failed = m_cppFailures.find(pathKey);
        if (!force && failed != m_cppFailures.end() &&
            failed->second == writeTime) {
            continue;
        }

        const auto stamp = writeTime.time_since_epoch().count();
        const std::string hash = std::to_string(
            std::hash<std::string>{}(pathKey + ":" + std::to_string(stamp)));
        const fs::path output = cacheRoot /
            (source.stem().string() + "_" + hash + ".so");
        std::error_code removeEc;
        fs::remove(output, removeEc);

        beginBuild();
        m_cppBuildOutput += "Compiling " + pathKey + "\n";

        std::string compilerOutput;
        if (!CompileCppScript(source, output, includeRoot, projectRoot,
                              compilerOutput)) {
            const std::string message = compilerOutput.empty()
                ? "Compiler exited without diagnostics."
                : compilerOutput;
            m_cppBuildErrors += "=== " + pathKey + " ===\n" + message + "\n";
            m_cppBuildOutput += "FAILED " + pathKey + "\n";
            if (!compilerOutput.empty()) {
                m_cppBuildOutput += compilerOutput;
                AddLog("C++ compile error in " + pathKey + ":\n" +
                       compilerOutput);
            } else {
                AddLog("C++ compile error in " + pathKey);
            }
            m_cppFailures[pathKey] = writeTime;
            if (moduleIt != m_cppModules.end()) {
                moduleIt->second.writeTime = writeTime;
            }
            allSucceeded = false;
            continue;
        }
        if (!compilerOutput.empty()) {
            m_cppBuildOutput += compilerOutput;
        }

        CppScriptModule module;
        module.path = pathKey;
        module.writeTime = writeTime;
        module.libraryPath = output.string();
        m_cppModules[pathKey] = std::move(module);
        m_cppFailures.erase(pathKey);
        modulesChanged = true;
        m_cppBuildOutput += "Compiled " + pathKey + "\n";
    }

    if (modulesChanged || force) {
        if (m_cppModules.empty()) {
            StopCppWorker();
        } else if (!StartCppWorker()) {
            allSucceeded = false;
        }
    }
    return allSucceeded;
#endif
}

void ScriptEngine::UpdateCppScripts(float dt) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= m_nextCppScan) {
        m_nextCppScan = now + std::chrono::seconds(1);
        ReloadCppScripts(false);
    }
    if (!m_cppWorkerReady || m_cppWorkerWrite < 0 ||
        m_cppWorkerRead < 0) {
        return;
    }

    CppWorkerPacket frame;
    frame.type = static_cast<uint32_t>(CppWorkerMessage::Frame);
    frame.x = dt;
    if (!WriteWorkerPacket(m_cppWorkerWrite, frame)) {
        StopCppWorker("C++ worker exited before update.");
        return;
    }

    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (true) {
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) {
            StopCppWorker("C++ worker timed out during update.");
            return;
        }
        pollfd descriptor{m_cppWorkerRead, POLLIN, 0};
        const int ready = poll(&descriptor, 1,
                               static_cast<int>(remaining.count()));
        if (ready == 0) {
            StopCppWorker("C++ worker timed out during update.");
            return;
        }
        if (ready < 0) {
            StopCppWorker("C++ worker poll failed during update.");
            return;
        }
        CppWorkerPacket packet;
        if (!ReadWorkerPacket(m_cppWorkerRead, packet)) {
            StopCppWorker("C++ worker exited during update.");
            return;
        }
        const auto type = static_cast<CppWorkerMessage>(packet.type);
        if (type == CppWorkerMessage::FrameDone) return;
        if (type == CppWorkerMessage::LoadResult) {
            if (!packet.valid) {
                m_cppBuildErrors += "C++ worker load error: " +
                    std::string(packet.text) + "\n";
                AddLog("C++ worker load error: " + std::string(packet.text));
            }
            continue;
        }
        if (!HandleWorkerPacket(packet)) {
            StopCppWorker("C++ worker communication failed during update.");
            return;
        }
    }
}

void ScriptEngine::RunAIScripts(Scene& scene, float dt) {
    auto& reg = scene.Registry();
    auto view = reg.view<AIControllerComponent>();
    for (auto e : view) {
        auto& aic = view.get<AIControllerComponent>(e);
        if (aic.scriptPath.empty()) continue;
        uint32_t eid = (uint32_t)e;
        auto& instance = m_aiControllers[eid];
        if (instance.loadedPath != aic.scriptPath) {
            try {
                instance.env = sol::environment(m_lua, sol::create, m_lua.globals());
                m_lua.script_file(aic.scriptPath, instance.env);
                instance.updateFunc = instance.env["update_ai"];
                instance.loadedPath = aic.scriptPath;
                spdlog::info("[AI] Loaded script for entity {}: {}", eid, aic.scriptPath);
            } catch (const sol::error& err) {
                AddLog(std::string("[AI ERROR] ") + err.what());
                instance.loadedPath = "ERROR_" + aic.scriptPath;
            }
        }
        if (instance.updateFunc.valid()) {
            sol::protected_function_result result = instance.updateFunc(eid, dt);
            if (!result.valid()) {
                sol::error err = result;
                AddLog(std::string("[AI ERROR] ") + err.what());
            }
        }
    }
}

void ScriptEngine::ClearLuaEntities() {
    if (!m_scene) return;
    auto view = m_scene->Registry().view<LuaEntityComponent>();
    std::vector<entt::entity> to_destroy(view.begin(), view.end());
    for (auto entity : to_destroy) DestroyEntity(entity);
    if (!to_destroy.empty()) AddLog("Cleared " + std::to_string(to_destroy.size()) +
                                   " script-spawned entities");
}

bool ScriptEngine::CallUIHandler(const std::string& handler,
                                 const std::string& widgetId,
                                 float value,
                                 bool checked) {
    if (handler.empty()) return false;

    sol::protected_function function;
    const auto it = m_uiHandlers.find(handler);
    if (it != m_uiHandlers.end()) {
        function = it->second;
    } else {
        sol::object object = m_lua[handler];
        if (!object.valid() || object.get_type() != sol::type::function) {
            spdlog::warn("[UI] Lua handler '{}' is not defined", handler);
            return false;
        }
        function = object;
    }

    sol::protected_function_result result = function(widgetId, value, checked);
    if (!result.valid()) {
        sol::error error = result;
        AddLog(std::string("[UI ERROR] ") + handler + ": " + error.what());
        return false;
    }
    return true;
}

bool ScriptEngine::HotReload(const std::string& path) {
    ClearLuaEntities();
    m_aiControllers.clear();
    m_uiHandlers.clear();
    return RunScript(path);
}

void ScriptEngine::Update(float dt) {
    m_deltaTime = dt;
    m_time += static_cast<double>(dt);
    ++m_frame;

    UpdateCppScripts(dt);

    if (!m_runOnceInvoked) {
        // Mark as invoked before the call so a failing RunOnce is not retried
        // every frame. It is reset by the next successful script load/reload.
        m_runOnceInvoked = true;

        auto runOnceFunc = m_lua["RunOnce"];
        if (runOnceFunc.valid() && runOnceFunc.get_type() != sol::type::nil) {
            if (runOnceFunc.get_type() == sol::type::function) {
                try {
                    runOnceFunc();
                } catch (const sol::error& e) {
                    AddLog(std::string("[RUNONCE ERROR] ") + e.what());
                }
            } else {
                AddLog("[RUNONCE ERROR] RunOnce is not a function");
            }
        }
    }

    // RunAlways is the new per-frame entry point. Keep update(dt) working for
    // scripts written before the lifecycle callbacks were introduced.
    auto runAlwaysFunc = m_lua["RunAlways"];
    if (runAlwaysFunc.valid() && runAlwaysFunc.get_type() == sol::type::function) {
        try {
            runAlwaysFunc(dt);
        } catch (const sol::error& e) {
            AddLog(std::string("[RUNALWAYS ERROR] ") + e.what());
        }
    } else {
        auto updateFunc = m_lua["update"];
        if (updateFunc.valid() &&
            updateFunc.get_type() == sol::type::function) {
            try {
                updateFunc(dt);
            } catch (const sol::error& e) {
                AddLog(std::string("[UPDATE ERROR] ") + e.what());
            }
        }
    }

    // Apply script velocities for character roots that have no live Jolt body.
    // Scripts must refresh the velocity during this Update; stale entries are
    // kept for get_velocity() but no longer move the entity.
    if (!m_scene || dt == 0.0f) return;
    auto& registry = m_scene->Registry();
    for (auto it = m_fallbackVelocities.begin();
         it != m_fallbackVelocities.end();) {
        const entt::entity entity = static_cast<entt::entity>(it->first);
        if (!registry.valid(entity) || HasPhysicsBody(it->first)) {
            it = m_fallbackVelocities.erase(it);
            continue;
        }
        if (it->second.frame == m_frame &&
            registry.all_of<TransformComponent>(entity)) {
            auto& tf = registry.get<TransformComponent>(entity);
            auto* rb = registry.try_get<RigidBodyComponent>(entity);
            if (rb && rb->useGravity) {
                if (!it->second.hasGroundY) {
                    it->second.groundY = tf.position.y;
                    it->second.hasGroundY = true;
                }
                // Body-less character roots have no Jolt gravity. Keep the
                // fallback controller grounded at its initial height so a
                // jump rises, slows down and returns instead of flying away.
                if (tf.position.y > it->second.groundY + 1e-4f ||
                    it->second.value.y > 0.0f) {
                    it->second.value.y -= 9.81f * dt;
                }
                tf.position += it->second.value * dt;
                if (tf.position.y <= it->second.groundY) {
                    tf.position.y = it->second.groundY;
                    if (it->second.value.y < 0.0f) {
                        it->second.value.y = 0.0f;
                    }
                }
            } else {
                tf.position += it->second.value * dt;
            }
        }
        ++it;
    }
}

} // namespace Galeshi
