#include "GaleshiEditorApp.hpp"



    // -------------------------------------------------------------------
    // Content Browser & Assets
    // -------------------------------------------------------------------
    void GaleshiEditorApp::SetCodeEditorContent(const std::string& path,
                              const std::string& text,
                              bool cppMode) {
        m_currentCodeFile = path;
        m_codeEditorCppMode = cppMode;
        m_codeEditorTabRequest = 0;
        const bool shader = IsShaderAssetPath(path);
        m_codeEditor.SetLanguageDefinition(
            cppMode || shader ? TextEditor::LanguageDefinition::CPlusPlus()
                              : TextEditor::LanguageDefinition::Lua());
        m_codeEditor.SetPalette(TextEditor::GetDarkPalette());
        m_codeEditor.SetText(text);
        m_codeEditorInitialized = true;
        m_codeEditorSyntaxErrors.clear();
        m_codeEditorLastSyntaxSource.clear();
        m_codeEditorSyntaxDirty = !path.empty() && !shader &&
                                  !IsMaterialScriptAssetPath(path);
        m_codeEditorLastEditTime = ImGui::GetTime();
        // The text input lives in a child window. Focusing only the parent
        // leaves the editor non-editable until it is clicked manually.
        m_focusCodeEditor = !path.empty();
    }



    void GaleshiEditorApp::OpenCodeFile(const std::string& path) {
        std::string text;
        std::ifstream in(path);
        if (in.is_open()) {
            std::stringstream ss;
            ss << in.rdbuf();
            text = ss.str();
        }
        SetCodeEditorContent(path, text, IsCppCodeAssetPath(path));
        if (IsShaderAssetPath(path)) {
            CompileShaderPreview(path);
        } else {
            m_shaderPreviewError.clear();
        }
        ImGui::SetWindowFocus("Code Editor");
    }



    void GaleshiEditorApp::SyncCodeEditorStateToSerializer() {
        if (!m_serializer) return;
        if (m_currentCodeFile.empty()) {
            m_serializer->SetCodeEditorState({}, {}, false);
        } else {
            m_serializer->SetCodeEditorState(
                m_currentCodeFile, m_codeEditor.GetText(),
                m_codeEditorCppMode);
        }
    }



    void GaleshiEditorApp::RestoreCodeEditorStateFromSerializer() {
        if (!m_serializer) return;
        if (m_serializer->CodeEditorPath().empty()) {
            SetCodeEditorContent({}, "-- Select a code file.", false);
            return;
        }
        SetCodeEditorContent(
            m_serializer->CodeEditorPath(),
            m_serializer->CodeEditorText(),
            m_serializer->CodeEditorCppMode());
        if (IsShaderAssetPath(m_serializer->CodeEditorPath())) {
            CompileShaderPreview(m_serializer->CodeEditorPath());
        }
    }



    int GaleshiEditorApp::DiagnosticLineNumber(const std::string& line) {
        for (size_t i = 0; i + 1 < line.size(); ++i) {
            if (line[i] != ':') continue;
            size_t cursor = i + 1;
            int value = 0;
            bool hasDigits = false;
            while (cursor < line.size() &&
                   std::isdigit(static_cast<unsigned char>(line[cursor]))) {
                value = value * 10 + (line[cursor] - '0');
                cursor++;
                hasDigits = true;
            }
            if (hasDigits && cursor < line.size() && line[cursor] == ':') {
                return value;
            }
        }
        return -1;
    }



    std::string GaleshiEditorApp::FormatSyntaxDiagnostics(const std::string& diagnostics,
                                        bool cpp) {
        const std::vector<std::string> lines = m_codeEditor.GetTextLines();
        std::istringstream input(diagnostics);
        std::string line;
        std::string formatted;
        while (std::getline(input, line)) {
            const bool diagnosticLine =
                cpp ? (line.find("error:") != std::string::npos ||
                       line.find("fatal error:") != std::string::npos ||
                       line.find("warning:") != std::string::npos)
                    : true;
            if (diagnosticLine) {
                const int lineNumber = DiagnosticLineNumber(line);
                if (lineNumber > 0 &&
                    lineNumber <= static_cast<int>(lines.size())) {
                    formatted += "Line " + std::to_string(lineNumber) + ": " +
                                 lines[static_cast<size_t>(lineNumber - 1)] +
                                 "\n";
                }
            }
            formatted += line + "\n";
        }
        return formatted;
    }



    void GaleshiEditorApp::StartCodeSyntaxCheck() {
        if (m_codeEditorSyntaxRunning || m_currentCodeFile.empty() ||
            IsShaderAssetPath(m_currentCodeFile) ||
            IsMaterialScriptAssetPath(m_currentCodeFile)) {
            m_codeEditorSyntaxDirty = false;
            return;
        }
        const std::string source = m_codeEditor.GetText();
        if (source == m_codeEditorLastSyntaxSource &&
            m_currentCodeFile == m_codeEditorSyntaxPendingPath) {
            m_codeEditorSyntaxDirty = false;
            return;
        }
        m_codeEditorSyntaxPendingSource = source;
        m_codeEditorSyntaxPendingPath = m_currentCodeFile;
        const bool cpp = IsCppCodeAssetPath(m_currentCodeFile);
        m_codeEditorSyntaxRunning = true;
        m_codeEditorSyntaxDirty = false;
        Galeshi::ScriptEngine* scripts = &m_scripts;
        try {
            m_codeEditorSyntaxFuture = std::async(
                std::launch::async,
                [scripts, source, path = m_currentCodeFile, cpp]() {
                    CodeSyntaxResult result;
                    result.cpp = cpp;
                    result.source = source;
                    result.path = path;
                    result.ok = cpp
                        ? scripts->CheckCppSyntax(
                              source, path, result.diagnostics)
                        : scripts->CheckLuaSyntax(
                              source, path, result.diagnostics);
                    return result;
                });
        } catch (const std::exception& error) {
            m_codeEditorSyntaxRunning = false;
            m_codeEditorSyntaxErrors =
                std::string("Could not start syntax watcher: ") +
                error.what() + "\n";
        }
    }



    void GaleshiEditorApp::PollCodeSyntaxCheck() {
        if (!m_codeEditorSyntaxRunning ||
            !m_codeEditorSyntaxFuture.valid()) {
            return;
        }
        if (m_codeEditorSyntaxFuture.wait_for(
                std::chrono::seconds(0)) != std::future_status::ready) {
            return;
        }
        CodeSyntaxResult result;
        try {
            result = m_codeEditorSyntaxFuture.get();
        } catch (const std::exception& error) {
            m_codeEditorSyntaxRunning = false;
            m_codeEditorSyntaxErrors =
                std::string("Syntax watcher failed: ") + error.what() + "\n";
            return;
        } catch (...) {
            m_codeEditorSyntaxRunning = false;
            m_codeEditorSyntaxErrors =
                "Syntax watcher failed with an unknown exception.\n";
            return;
        }
        m_codeEditorSyntaxRunning = false;
        if (result.path != m_currentCodeFile ||
            result.source != m_codeEditor.GetText()) {
            m_codeEditorSyntaxDirty = true;
            return;
        }
        m_codeEditorLastSyntaxSource = result.source;
        if (result.ok) {
            m_codeEditorSyntaxErrors.clear();
        } else if (result.diagnostics.empty()) {
            m_codeEditorSyntaxErrors =
                "Syntax check failed without diagnostics.\n";
        } else {
            m_codeEditorSyntaxErrors = FormatSyntaxDiagnostics(
                result.diagnostics, result.cpp);
        }
    }



    std::string GaleshiEditorApp::CodeEditorErrorsText() const {
        if (IsShaderAssetPath(m_currentCodeFile) &&
            !m_shaderPreviewError.empty()) {
            return m_shaderPreviewError;
        }
        std::string errors = m_codeEditorSyntaxErrors;
        const std::string& buildErrors = m_scripts.GetCppBuildErrors();
        if (!buildErrors.empty() &&
            (m_currentCodeFile.empty() ||
             IsCppCodeAssetPath(m_currentCodeFile))) {
            if (!errors.empty()) {
                errors += "\n--- C++ build ---\n";
            }
            errors += buildErrors;
        }
        return errors;
    }



    std::string GaleshiEditorApp::ShellQuote(const std::string& value) {
        std::string quoted = "'";
        for (char c : value) {
            if (c == '\'') {
                quoted += "'\\''";
            } else {
                quoted += c;
            }
        }
        quoted += "'";
        return quoted;
    }



    std::string GaleshiEditorApp::FindShadercExecutable() const {
        std::error_code ec;
        const auto check = [&ec](const std::filesystem::path& path) {
            return !path.empty() && std::filesystem::is_regular_file(path, ec)
                       ? path.string()
                       : std::string{};
        };
        std::string result = check(
            std::filesystem::current_path() / "shaderc");
        if (!result.empty()) return result;
#if defined(__linux__)
        const auto executable =
            std::filesystem::read_symlink("/proc/self/exe", ec);
        if (!ec) {
            result = check(executable.parent_path() / "shaderc");
            if (!result.empty()) return result;
        }
#endif
        result = check("/usr/bin/shaderc");
        if (!result.empty()) return result;
        return check("/usr/local/bin/shaderc");
    }



    std::filesystem::path GaleshiEditorApp::ShaderIncludeDirectory() const {
        std::error_code ec;
        const auto valid = [&ec](const std::filesystem::path& path) {
            return std::filesystem::is_regular_file(
                       path / "bgfx_shader.sh", ec) &&
                   std::filesystem::is_regular_file(
                       path / "varying_fullscreen.def.sc", ec);
        };
        std::filesystem::path include =
            std::filesystem::current_path() / "assets/shaders/include";
        if (valid(include)) return include;
        for (const char* relative : {
                 "../_deps/bgfx-src/bgfx/src",
                 "../../_deps/bgfx-src/bgfx/src",
                 "../../../_deps/bgfx-src/bgfx/src",
             }) {
            include = std::filesystem::current_path() / relative;
            if (valid(include)) return include;
        }
        return {};
    }



    void GaleshiEditorApp::DestroyShaderPreviewProgram() {
        if (bgfx::isValid(m_shaderPreviewProgram)) {
            bgfx::destroy(m_shaderPreviewProgram);
            m_shaderPreviewProgram = BGFX_INVALID_HANDLE;
        }
    }



    void GaleshiEditorApp::ShutdownShaderPreview() {
        DestroyShaderPreviewProgram();
        if (bgfx::isValid(m_shaderPreviewFbo)) {
            bgfx::destroy(m_shaderPreviewFbo);
            m_shaderPreviewFbo = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewTex)) {
            bgfx::destroy(m_shaderPreviewTex);
            m_shaderPreviewTex = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewVB)) {
            bgfx::destroy(m_shaderPreviewVB);
            m_shaderPreviewVB = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewIB)) {
            bgfx::destroy(m_shaderPreviewIB);
            m_shaderPreviewIB = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewSampler)) {
            bgfx::destroy(m_shaderPreviewSampler);
            m_shaderPreviewSampler = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewTime)) {
            bgfx::destroy(m_shaderPreviewTime);
            m_shaderPreviewTime = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewResolution)) {
            bgfx::destroy(m_shaderPreviewResolution);
            m_shaderPreviewResolution = BGFX_INVALID_HANDLE;
        }
        if (bgfx::isValid(m_shaderPreviewFallbackTex)) {
            bgfx::destroy(m_shaderPreviewFallbackTex);
            m_shaderPreviewFallbackTex = BGFX_INVALID_HANDLE;
        }
    }



    void GaleshiEditorApp::EnsureShaderPreviewResources() {
        if (!bgfx::isValid(m_shaderPreviewSampler)) {
            m_shaderPreviewSampler = bgfx::createUniform(
                "s_texColor", bgfx::UniformType::Sampler);
            m_shaderPreviewTime = bgfx::createUniform(
                "u_shaderTime", bgfx::UniformType::Vec4);
            m_shaderPreviewResolution = bgfx::createUniform(
                "u_shaderResolution", bgfx::UniformType::Vec4);
        }
        if (!bgfx::isValid(m_shaderPreviewFallbackTex)) {
            const uint32_t white = 0xffffffffu;
            m_shaderPreviewFallbackTex = bgfx::createTexture2D(
                1, 1, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_SAMPLER_U_CLAMP | BGFX_SAMPLER_V_CLAMP,
                bgfx::copy(&white, sizeof(white)));
        }
        if (!bgfx::isValid(m_shaderPreviewTex)) {
            constexpr uint16_t size = 256;
            m_shaderPreviewTex = bgfx::createTexture2D(
                size, size, false, 1, bgfx::TextureFormat::RGBA8,
                BGFX_TEXTURE_RT | BGFX_SAMPLER_U_CLAMP |
                    BGFX_SAMPLER_V_CLAMP);
        }
        if (bgfx::isValid(m_shaderPreviewTex) &&
            !bgfx::isValid(m_shaderPreviewFbo)) {
            m_shaderPreviewFbo = bgfx::createFrameBuffer(
                1, &m_shaderPreviewTex, false);
        }
        if (!bgfx::isValid(m_shaderPreviewVB)) {
            struct PreviewVertex {
                float x, y, z;
                float u, v;
            };
            static const PreviewVertex quad[4] = {
                {-1.0f, -1.0f, 0.0f, 0.0f, 1.0f},
                { 1.0f, -1.0f, 0.0f, 1.0f, 1.0f},
                {-1.0f,  1.0f, 0.0f, 0.0f, 0.0f},
                { 1.0f,  1.0f, 0.0f, 1.0f, 0.0f},
            };
            static const uint16_t indices[6] = {0, 1, 2, 2, 1, 3};
            bgfx::VertexLayout layout;
            layout.begin()
                .add(bgfx::Attrib::Position, 3, bgfx::AttribType::Float)
                .add(bgfx::Attrib::TexCoord0, 2, bgfx::AttribType::Float)
                .end();
            m_shaderPreviewVB = bgfx::createVertexBuffer(
                bgfx::makeRef(quad, sizeof(quad)), layout);
            m_shaderPreviewIB = bgfx::createIndexBuffer(
                bgfx::makeRef(indices, sizeof(indices)));
        }
    }



    std::filesystem::path GaleshiEditorApp::ResolveShaderSourcePath(
        const std::string& path) const {
        std::filesystem::path source(path);
        std::error_code ec;
        if (source.is_absolute() ||
            std::filesystem::is_regular_file(source, ec)) {
            return source;
        }
        const std::string project = Galeshi::ProjectManager::GetProjectPath();
        if (!project.empty()) {
            const std::filesystem::path projectPath(project);
            const std::filesystem::path candidates[] = {
                projectPath / source,
                projectPath / "assets" / source,
                projectPath / "assets/scripts" / source.filename(),
            };
            for (const auto& candidate : candidates) {
                ec.clear();
                if (std::filesystem::is_regular_file(candidate, ec)) {
                    return candidate;
                }
            }
        }
        return source;
    }



    void GaleshiEditorApp::CompileShaderPreview(const std::string& path,
                              const std::string& sourceOverride) {
        DestroyShaderPreviewProgram();
        m_shaderPreviewPath = path;
        m_shaderPreviewError.clear();
        if (!IsShaderAssetPath(path)) {
            m_shaderPreviewError = "Not a shader file: " + path;
            return;
        }

        std::string source = sourceOverride;
        if (source.empty()) {
            const std::filesystem::path resolved =
                ResolveShaderSourcePath(path);
            std::ifstream in(resolved, std::ios::binary);
            if (!in.is_open()) {
                m_shaderPreviewError =
                    "Cannot open shader file: " + path;
                return;
            }
            std::stringstream buffer;
            buffer << in.rdbuf();
            source = buffer.str();
        }
        if (source.empty()) {
            m_shaderPreviewError = "Shader file is empty.";
            return;
        }

        const bool vertexShader = IsVertexShaderSource(source);
        const std::string shaderc = FindShadercExecutable();
        if (shaderc.empty()) {
            m_shaderPreviewError =
                "shaderc executable was not found. Build or install shaderc.";
            return;
        }
        const std::filesystem::path include = ShaderIncludeDirectory();
        if (include.empty()) {
            m_shaderPreviewError =
                "Shader include directory with bgfx_shader.sh was not found.";
            return;
        }

        std::error_code ec;
        const std::filesystem::path temp =
            std::filesystem::temp_directory_path(ec);
        const std::filesystem::path root =
            ec ? std::filesystem::current_path() : temp;
        const std::filesystem::path sourcePath =
            root / ("galeshi_shader_preview" +
                    std::filesystem::path(path).extension().string());
        const std::filesystem::path binaryPath =
            root / "galeshi_shader_preview.bin";
        const std::filesystem::path logPath =
            root / "galeshi_shader_preview.log";
        {
            std::ofstream out(sourcePath, std::ios::binary);
            if (!out.is_open()) {
                m_shaderPreviewError =
                    "Cannot write temporary shader source.";
                return;
            }
            out << source;
        }
        std::filesystem::remove(binaryPath, ec);
        ec.clear();
        std::filesystem::remove(logPath, ec);

#if defined(__linux__)
        const char* platform = "linux";
#elif defined(__APPLE__)
        const char* platform = "osx";
#else
        const char* platform = "windows";
#endif
        const std::string command =
            ShellQuote(shaderc) + " -f " + ShellQuote(sourcePath.string()) +
            " -o " + ShellQuote(binaryPath.string()) +
            " --type " + (vertexShader ? "vertex" : "fragment") +
            " --platform " + platform +
            " --profile spirv -i " + ShellQuote(include.string()) +
            " --varyingdef " +
            ShellQuote((include / "varying_fullscreen.def.sc").string()) +
            " > " + ShellQuote(logPath.string()) + " 2>&1";
        const int status = std::system(command.c_str());

        std::string diagnostics;
        {
            std::ifstream log(logPath);
            if (log.is_open()) {
                std::stringstream buffer;
                buffer << log.rdbuf();
                diagnostics = buffer.str();
            }
        }
        if (status != 0 ||
            !std::filesystem::is_regular_file(binaryPath, ec)) {
            m_shaderPreviewError = "Shader compilation failed.";
            if (!diagnostics.empty()) {
                m_shaderPreviewError += "\n" + diagnostics;
            }
            return;
        }

        m_shaderPreviewProgram = vertexShader
            ? Galeshi::ShaderManager::LoadProgram(
                  binaryPath.string(), "shaders/fs_passthrough.bin")
            : Galeshi::ShaderManager::LoadProgram(
                  "shaders/vs_fullscreen.bin", binaryPath.string());
        if (!bgfx::isValid(m_shaderPreviewProgram)) {
            m_shaderPreviewError =
                "Compiled shader could not be loaded by bgfx.";
            return;
        }

        EnsureShaderPreviewResources();
        if (!bgfx::isValid(m_shaderPreviewFbo)) {
            m_shaderPreviewError =
                "Could not create the shader preview framebuffer.";
            DestroyShaderPreviewProgram();
            return;
        }
        spdlog::info("[Shader] Compiled preview: {}", path);
    }



    void GaleshiEditorApp::RenderShaderPreview() {
        if (!bgfx::isValid(m_shaderPreviewProgram) ||
            !bgfx::isValid(m_shaderPreviewFbo) ||
            !bgfx::isValid(m_shaderPreviewSampler)) {
            return;
        }
        constexpr uint16_t size = 256;
        bgfx::setViewFrameBuffer(m_shaderPreviewView, m_shaderPreviewFbo);
        bgfx::setViewRect(m_shaderPreviewView, 0, 0, size, size);
        bgfx::setViewClear(m_shaderPreviewView, BGFX_CLEAR_COLOR,
                           0x0b1220ff);
        const bgfx::TextureHandle input =
            m_logoTexture && m_logoTexture->Valid()
                ? m_logoTexture->Handle()
                : m_shaderPreviewFallbackTex;
        bgfx::setTexture(0, m_shaderPreviewSampler, input);
        const float time[4] = {
            static_cast<float>(ImGui::GetTime()), 0.0f, 0.0f, 0.0f};
        const float resolution[4] = {
            static_cast<float>(size), static_cast<float>(size),
            1.0f / static_cast<float>(size),
            1.0f / static_cast<float>(size)};
        bgfx::setUniform(m_shaderPreviewTime, time);
        bgfx::setUniform(m_shaderPreviewResolution, resolution);
        bgfx::setVertexBuffer(0, m_shaderPreviewVB);
        bgfx::setIndexBuffer(m_shaderPreviewIB);
        bgfx::setState(BGFX_STATE_WRITE_RGB | BGFX_STATE_WRITE_A);
        bgfx::submit(m_shaderPreviewView, m_shaderPreviewProgram);
    }



    void GaleshiEditorApp::DrawShaderPreviewPane() {
        if (!m_shaderPreviewPath.empty()) {
            ImGui::TextDisabled("%s",
                                std::filesystem::path(m_shaderPreviewPath)
                                    .filename()
                                    .string()
                                    .c_str());
        }
        if (!m_shaderPreviewError.empty()) {
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImVec4(1.0f, 0.45f, 0.42f, 1.0f));
            ImGui::TextWrapped("%s", m_shaderPreviewError.c_str());
            ImGui::PopStyleColor();
            ImGui::Separator();
        }
        if (!bgfx::isValid(m_shaderPreviewProgram) ||
            !bgfx::isValid(m_shaderPreviewTex)) {
            ImGui::TextDisabled(
                "Compile a GLSL/HLSL fragment shader to see its effect.");
            return;
        }
        RenderShaderPreview();
        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float size =
            std::max(32.0f, std::min(available.x, available.y));
        ImGui::Image(static_cast<ImTextureID>(m_shaderPreviewTex.idx),
                     ImVec2(size, size), ImVec2(0.0f, 0.0f),
                     ImVec2(1.0f, 1.0f));
        ImGui::TextDisabled("256 x 256 live preview");
    }



    void GaleshiEditorApp::DrawCodeEditor() {
        ImGui::Begin("Code Editor");
        const std::string chosenCodeFile = m_codeFilePicker.Draw();
        if (!chosenCodeFile.empty()) OpenCodeFile(chosenCodeFile);
        if (!m_codeEditorInitialized) {
            m_codeEditor.SetLanguageDefinition(
                m_codeEditorCppMode
                    ? TextEditor::LanguageDefinition::CPlusPlus()
                    : TextEditor::LanguageDefinition::Lua());
            m_codeEditor.SetPalette(TextEditor::GetDarkPalette());
            m_codeEditor.SetText(
                "-- Select a .lua, .cpp, .glsl, .hlsl or .mat file from Content Browser");
            m_codeEditorInitialized = true;
        }

        auto drawDiagnostics = [](const char* childId,
                                  const std::string& text,
                                  bool errorText) {
            if (text.empty()) {
                ImGui::TextDisabled(errorText ? "No C++ errors."
                                              : "No C++ build output.");
                return;
            }
            if (errorText) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.52f, 0.48f, 1.0f));
            }
            ImGui::BeginChild(childId, ImVec2(0, 0), false,
                              ImGuiWindowFlags_HorizontalScrollbar);
            ImGui::TextUnformatted(text.c_str());
            ImGui::EndChild();
            if (errorText) ImGui::PopStyleColor();
        };

        if (ImGui::BeginTabBar("CodeEditorTabs",
                               ImGuiTabBarFlags_Reorderable)) {
            const auto selectFlag = [this](int tab) {
                return m_codeEditorTabRequest == tab
                    ? ImGuiTabItemFlags_SetSelected
                    : ImGuiTabItemFlags_None;
            };
            if (ImGui::BeginTabItem("Code", nullptr, selectFlag(0))) {
                m_codeEditorTabRequest = -1;
                if (ImGui::IsItemClicked()) m_focusCodeEditor = true;
                if (ImGui::Button("Open...")) {
                    std::filesystem::path start =
                        Galeshi::ProjectManager::GetProjectPath();
                    if (start.empty()) start = std::filesystem::current_path();
                    start /= "scripts";
                    if (!std::filesystem::is_directory(start)) {
                        start = std::filesystem::path(
                            Galeshi::ProjectManager::GetProjectPath()) /
                            "assets/scripts";
                    }
                    if (!std::filesystem::is_directory(start)) {
                        start = std::filesystem::current_path() /
                                "assets/scripts";
                    }
                    m_codeFilePicker.Open(
                        start.string(), "Open Code File",
                        {".lua", ".cpp", ".cc", ".cxx", ".hpp", ".h",
                         ".glsl", ".hlsl", ".mat"});
                }
                ImGui::SameLine();
                if (m_currentCodeFile.empty()) {
                    ImGui::TextDisabled(
                        "No file opened. Double-click a .lua, .cpp, .glsl, "
                        ".hlsl or .mat file in Content Browser, or use Open...");
                } else {
                    const bool cppFile =
                        IsCppCodeAssetPath(m_currentCodeFile);
                    const bool shaderFile =
                        IsShaderAssetPath(m_currentCodeFile);
                    if (shaderFile) {
                        ImGui::TextDisabled("Shader");
                    } else if (ImGui::Checkbox("C++ mode",
                                               &m_codeEditorCppMode)) {
                        m_codeEditor.SetLanguageDefinition(
                            m_codeEditorCppMode
                                ? TextEditor::LanguageDefinition::CPlusPlus()
                                : TextEditor::LanguageDefinition::Lua());
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Save")) {
                        std::ofstream out(m_currentCodeFile);
                        if (out.is_open()) {
                            out << m_codeEditor.GetText();
                            out.close();
                            spdlog::info(
                                "[Editor] Saved code file: {}",
                                std::filesystem::absolute(
                                    m_currentCodeFile).string());
                            bool saved = true;
                            if (cppFile && !m_scripts.ReloadCppScripts(true)) {
                                m_codeEditorTabRequest = 2;
                                saved = false;
                            }
                            if (shaderFile) {
                                CompileShaderPreview(
                                    m_currentCodeFile,
                                    m_codeEditor.GetText());
                                if (!m_shaderPreviewError.empty()) {
                                    m_codeEditorTabRequest = 2;
                                }
                            } else if (IsMaterialScriptAssetPath(
                                           m_currentCodeFile)) {
                                const int updated =
                                    ReapplyMaterialScripts(m_currentCodeFile);
                                spdlog::info(
                                    "[Material] Reapplied '{}' to {} slot(s)",
                                    m_currentCodeFile, updated);
                            }
                            if (saved) m_focusCodeEditor = true;
                        } else {
                            spdlog::error(
                                "[Editor] FAILED to open file for saving: {}",
                                m_currentCodeFile);
                        }
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Reload")) {
                        if (shaderFile) {
                            OpenCodeFile(m_currentCodeFile);
                        } else if (cppFile) {
                            if (!m_scripts.ReloadCppScripts(true)) {
                                m_codeEditorTabRequest = 2;
                            } else {
                                m_focusCodeEditor = true;
                            }
                        } else if (IsMaterialScriptAssetPath(
                                       m_currentCodeFile)) {
                            if (m_codeEditor.IsTextChanged()) {
                                std::ofstream out(m_currentCodeFile);
                                if (out.is_open()) {
                                    out << m_codeEditor.GetText();
                                    out.close();
                                }
                            }
                            const int updated =
                                ReapplyMaterialScripts(m_currentCodeFile);
                            spdlog::info(
                                "[Material] Reapplied '{}' to {} slot(s)",
                                m_currentCodeFile, updated);
                            m_focusCodeEditor = true;
                        } else if (m_scripts.HotReload(m_currentCodeFile)) {
                            spdlog::info("[Editor] Reloaded {}",
                                         m_currentCodeFile);
                            m_focusCodeEditor = true;
                        }
                    }
                    if (cppFile) {
                        ImGui::SameLine();
                        if (ImGui::Button("Compile C++")) {
                            if (!m_scripts.ReloadCppScripts(true)) {
                                m_codeEditorTabRequest = 2;
                            } else {
                                m_focusCodeEditor = true;
                            }
                        }
                    } else if (shaderFile) {
                        ImGui::SameLine();
                        if (ImGui::Button("Compile Shader")) {
                            CompileShaderPreview(
                                m_currentCodeFile, m_codeEditor.GetText());
                            if (!m_shaderPreviewError.empty()) {
                                m_codeEditorTabRequest = 2;
                            } else {
                                m_focusCodeEditor = true;
                            }
                        }
                    }
                    ImGui::SameLine();
                    ImGui::TextDisabled("%s", m_currentCodeFile.c_str());
                    ImGui::Separator();

                    const auto drawEditorSurface = [&]() {
                        if (m_focusCodeEditor) {
                            m_codeEditor.RequestEditorFocus();
                            m_focusCodeEditor = false;
                        }
                        m_codeEditor.Render("CodeEditor");

                        if (ImGui::IsWindowHovered(
                                ImGuiHoveredFlags_ChildWindows) &&
                            ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                            ImGui::OpenPopup("Code Editor Context");
                        }
                        if (ImGui::BeginPopup("Code Editor Context")) {
                            const bool readOnly = m_codeEditor.IsReadOnly();
                            if (ImGui::MenuItem("Copy", "Ctrl+C")) {
                                m_codeEditor.Copy();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem(
                                    "Cut", "Ctrl+X", false,
                                    !readOnly && m_codeEditor.HasSelection())) {
                                m_codeEditor.Cut();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem("Paste", "Ctrl+V", false,
                                                !readOnly)) {
                                m_codeEditor.Paste();
                                m_focusCodeEditor = true;
                            }
                            ImGui::Separator();
                            if (ImGui::MenuItem("Select All", "Ctrl+A")) {
                                m_codeEditor.SelectAll();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem("Undo", "Ctrl+Z", false,
                                                m_codeEditor.CanUndo())) {
                                m_codeEditor.Undo();
                                m_focusCodeEditor = true;
                            }
                            if (ImGui::MenuItem("Redo", "Ctrl+Y", false,
                                                m_codeEditor.CanRedo())) {
                                m_codeEditor.Redo();
                                m_focusCodeEditor = true;
                            }
                            ImGui::EndPopup();
                        }

                        if (m_codeEditor.IsTextChanged()) {
                            m_codeEditorSyntaxDirty = true;
                            m_codeEditorLastEditTime = ImGui::GetTime();
                        }
                        PollCodeSyntaxCheck();
                        if (m_codeEditorSyntaxDirty &&
                            !m_codeEditorSyntaxRunning &&
                            ImGui::GetTime() - m_codeEditorLastEditTime >=
                                0.6) {
                            StartCodeSyntaxCheck();
                        }
                    };

                    if (shaderFile) {
                        const ImVec2 available =
                            ImGui::GetContentRegionAvail();
                        const float previewWidth = std::clamp(
                            available.x * 0.34f, 220.0f, 460.0f);
                        ImGui::BeginChild(
                            "CodeEditorSurface",
                            ImVec2(std::max(
                                       120.0f,
                                       available.x - previewWidth - 8.0f),
                                   0.0f),
                            false);
                        drawEditorSurface();
                        ImGui::EndChild();
                        ImGui::SameLine();
                        ImGui::BeginChild("ShaderPreview", ImVec2(0, 0), true);
                        DrawShaderPreviewPane();
                        ImGui::EndChild();
                    } else {
                        drawEditorSurface();
                    }
                }
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Output", nullptr, selectFlag(1))) {
                m_codeEditorTabRequest = -1;
                drawDiagnostics("CodeEditorOutput",
                                m_scripts.GetCppBuildOutput(), false);
                ImGui::EndTabItem();
            }
            const std::string errors = CodeEditorErrorsText();
            const bool hasErrors = !errors.empty();
            if (hasErrors) {
                ImGui::PushStyleColor(ImGuiCol_Text,
                                      ImVec4(1.0f, 0.45f, 0.42f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Tab,
                                      ImVec4(0.35f, 0.06f, 0.06f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabHovered,
                                      ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabSelected,
                                      ImVec4(0.60f, 0.12f, 0.12f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabDimmed,
                                      ImVec4(0.28f, 0.05f, 0.05f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_TabDimmedSelected,
                                      ImVec4(0.45f, 0.08f, 0.08f, 1.0f));
            }
            const bool errorsTabOpen =
                ImGui::BeginTabItem("Errors", nullptr, selectFlag(2));
            if (hasErrors) ImGui::PopStyleColor(6);
            if (errorsTabOpen) {
                m_codeEditorTabRequest = -1;
                drawDiagnostics("CodeEditorErrors", errors, true);
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
        ImGui::End();
    }

