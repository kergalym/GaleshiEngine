#include "ScriptEngineInternal.hpp"
#include "GaleshiEngine/Core/ProjectManager.hpp"
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

using namespace ScriptEngineInternal;

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

    while (!m_cppTriggerEvents.empty()) {
        const CppTriggerEvent event = m_cppTriggerEvents.front();
        CppWorkerPacket packet;
        packet.type = static_cast<uint32_t>(
            event.begin ? CppWorkerMessage::TriggerBeginOverlap
                        : CppWorkerMessage::TriggerEndOverlap);
        packet.entity = event.trigger;
        packet.other = event.other;
        if (!WriteWorkerPacket(m_cppWorkerWrite, packet)) {
            StopCppWorker("C++ worker exited while sending trigger event.");
            return;
        }
        m_cppTriggerEvents.pop_front();
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



} // namespace Galeshi
