#include "GaleshiEngine/Core/ProjectManager.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <spdlog/spdlog.h>
#include <filesystem>

namespace Galeshi {
static std::string s_projectPath;
static std::string s_projectName;

bool ProjectManager::IsProjectOpen() { return !s_projectPath.empty(); }
std::string ProjectManager::GetProjectPath() { return s_projectPath; }
std::string ProjectManager::GetProjectName() { return s_projectName; }

bool ProjectManager::CreateProject(const std::string& path, const std::string& name) {
    if (path.empty() || name.empty()) return false;
    std::filesystem::path root = std::filesystem::path(path) / name;
    std::error_code ec;
    std::filesystem::create_directories(root / "assets", ec);
    std::filesystem::create_directories(root / "scenes", ec);

    nlohmann::json j;
    j["name"] = name;
    j["version"] = "1.0.0";
    j["engine"] = "GaleshiEngine";

    std::ofstream out(root / (name + ".gproj"));
    if (!out.is_open()) return false;
    out << j.dump(4);
    out.close();

    return OpenProject((root / (name + ".gproj")).string());
}

bool ProjectManager::OpenProject(const std::string& filepath) {
    std::filesystem::path p(filepath);
    if (!std::filesystem::exists(p)) return false;
    s_projectPath = p.parent_path().string();
    s_projectName = p.stem().string();
    spdlog::info("[ProjectManager] Opened project: {} at {}", s_projectName, s_projectPath);
    return true;
}
}
