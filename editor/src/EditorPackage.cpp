#include "EditorCommon.hpp"
#include "EditorPackage.hpp"

namespace fs = std::filesystem;

int PackageProject(const std::string& projectPath) {
    spdlog::info("[Package] Starting packaging for: {}", projectPath);

    const char* homeEnv = std::getenv("HOME");
    fs::path homeDir = homeEnv ? homeEnv : "";

    fs::path projPath = fs::absolute(projectPath);
    if (!fs::exists(projPath)) {
        projPath = fs::current_path() / fs::path(projectPath).filename();
    }
    if (!fs::exists(projPath) && !homeDir.empty()) {
        projPath = homeDir / fs::path(projectPath).filename();
    }

    if (!fs::exists(projPath)) {
        spdlog::error("[Package] Project file not found: {}", projPath.string());
        return 1;
    }

    std::ifstream f(projPath);
    if (!f.is_open()) return 1;
    nlohmann::json proj;
    try {
        f >> proj;
    } catch (const std::exception& e) {
        spdlog::error("[Package] Invalid JSON: {}", e.what());
        return 1;
    }

    std::string gameName = proj.value("name", "GaleshiGame");
    std::string startScene = proj.value("startScene", "scene.json");

    fs::path outDir = fs::current_path() / "build" / "package" / gameName;
    fs::create_directories(outDir);

    // 1. Copy the executable and make it runnable.
    fs::path exePath = fs::current_path() / "GaleshiEditor";
    if (!fs::exists(exePath)) exePath = fs::current_path() / "build" / "debug" / "bin" / "GaleshiEditor";

    if (fs::exists(exePath)) {
        fs::path destExe = outDir / (gameName + ".bin");
        fs::copy_file(exePath, destExe, fs::copy_options::overwrite_existing);
        fs::permissions(destExe,
            fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
            fs::perm_options::replace);
        spdlog::info("[Package] Copied executable to {} (chmod +x)", destExe.filename().string());

        // C++ scripts run in this helper process. Keep it beside the game
        // binary so runtime isolation also works from packaged builds.
        const fs::path workerPath =
            exePath.parent_path() / "GaleshiScriptWorker";
        if (fs::exists(workerPath)) {
            const fs::path destWorker = outDir / "GaleshiScriptWorker";
            fs::copy_file(workerPath, destWorker,
                          fs::copy_options::overwrite_existing);
            fs::permissions(destWorker,
                fs::perms::owner_all | fs::perms::group_all |
                    fs::perms::others_all,
                fs::perm_options::replace);
        } else {
            spdlog::warn("[Package] C++ script worker not found at {}",
                         workerPath.string());
        }
    } else {
        spdlog::error("[Package] Executable not found at {}", exePath.string());
        return 1;
    }

    // 2. Copy assets and shaders.
    fs::path projectRoot = projPath.parent_path();
    auto copyDir = [&](const std::string& dirName) {
        fs::path srcDir = projectRoot / dirName;
        if (!fs::exists(srcDir)) srcDir = fs::current_path() / dirName;
        if (fs::exists(srcDir)) {
            fs::copy(srcDir, outDir / dirName, fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            spdlog::info("[Package] Copied directory: {}", dirName);
        } else {
            spdlog::warn("[Package] Directory not found: {}", srcDir.string());
        }
    };
    copyDir("assets");

    // Copy compiled shaders.
    fs::path compiledShaders = fs::current_path() / "build" / "debug" / "bin" / "shaders";
    if (!fs::exists(compiledShaders)) {
        // When running from build/debug/bin, shaders are in the current directory.
        compiledShaders = fs::current_path() / "shaders";
    }
    if (fs::exists(compiledShaders)) {
        fs::copy(compiledShaders, outDir / "shaders", fs::copy_options::recursive | fs::copy_options::overwrite_existing);
        spdlog::info("[Package] Copied compiled shaders from {}", compiledShaders.string());
    } else {
        spdlog::error("[Package] Compiled shaders not found at {}", compiledShaders.string());
    }

    // 3. Copy the start scene.
    fs::path scenePath = "";
    if (!homeDir.empty() && fs::exists(homeDir / startScene)) {
        scenePath = homeDir / startScene;
    } else if (fs::exists(projectRoot / startScene)) {
        scenePath = projectRoot / startScene;
    }

    if (!scenePath.empty()) {
        fs::copy_file(scenePath, outDir / startScene, fs::copy_options::overwrite_existing);
        spdlog::info("[Package] Copied start scene from {}", scenePath.string());
    } else {
        spdlog::error("[Package] Could not find start scene {} anywhere!", startScene);
    }

    // 4. Save .gproj.
    proj["startScene"] = startScene;
    std::ofstream outProj(outDir / (gameName + ".gproj"));
    outProj << proj.dump(4);
    outProj.close();

    // 5. Create run.sh.
    std::ofstream runScript(outDir / "run.sh");
    runScript << "#!/bin/bash\n";
    runScript << "SCRIPT_DIR=\"$( cd \"$( dirname \"${BASH_SOURCE[0]}\" )\" &> /dev/null && pwd )\"\n";
    runScript << "cd \"$SCRIPT_DIR\"\n";
    runScript << "./" << gameName << ".bin --game " << gameName << ".gproj\n";
    runScript.close();
    fs::permissions(outDir / "run.sh",
        fs::perms::owner_all | fs::perms::group_all | fs::perms::others_all,
        fs::perm_options::replace);

    spdlog::info("[Package] Packaging complete! Output: {}", outDir.string());
    return 0;
}

