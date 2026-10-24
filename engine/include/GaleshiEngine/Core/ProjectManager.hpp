#pragma once
#include <string>

namespace Galeshi {
class ProjectManager {
public:
    static bool CreateProject(const std::string& path, const std::string& name);
    static bool OpenProject(const std::string& filepath);
    static bool IsProjectOpen();
    static std::string GetProjectPath();
    static std::string GetProjectName();
};
}
