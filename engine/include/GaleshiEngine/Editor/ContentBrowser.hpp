#pragma once

#include <chrono>
#include <filesystem>
#include "GaleshiEngine/Editor/FilePickerDialog.hpp"
#include <string>
#include <vector>

namespace Galeshi {

class AssetManager;

class ContentBrowser {
public:
    explicit ContentBrowser(std::string rootDir, AssetManager* assets = nullptr);

    // Draws the panel and returns the selected/created file path.
    std::string Draw();

    // Creates a Lua script from one of the built-in templates inside
    // <content root>/scripts. Returns the created file path, or an empty
    // string on failure.
    std::string CreateScript(const std::string& templateId);

    void Refresh();

    // Folder currently shown in the browser. File-manager drops that land on
    // the Content Browser are imported here.
    const std::filesystem::path& CurrentDirectory() const { return m_current; }

    // Copies external files into destDir and refreshes the browser. Returns
    // true when at least one file was imported.
    bool ImportFiles(const std::vector<std::string>& files,
                     const std::filesystem::path& destDir);

    // Drop coordinates are window-relative (single-viewport ImGui space).
    bool ContainsPoint(float x, float y) const;

private:
    struct Entry {
        std::filesystem::path path;
        bool                  isDirectory;
        std::string           name;
    };

    void DrawDirectory(const std::filesystem::path& dir);

    // Polls the current directory for changes (files added/removed/renamed)
    // and refreshes the listing when needed.
    void UpdateAutoRefresh();

    enum class Dialog {
        None,
        Rename,
        Delete,
        NewFolder,
        NewLua
    };

    void OpenDialog(Dialog dialog, const std::filesystem::path& path,
                    const std::string& defaultName = {});
    void DrawDialogs();
    bool RenameEntry(const std::filesystem::path& source,
                     const std::string& newName);
    bool DeleteEntry(const std::filesystem::path& path);
    bool CreateFolder(const std::string& name);
    bool CreateLuaFile(const std::string& name);
    bool MoveEntry(const std::filesystem::path& source,
                   const std::filesystem::path& destinationDir);
    bool ValidateName(const std::string& name, std::string& error) const;
    bool IsInsideRoot(const std::filesystem::path& path) const;
    static std::string UniqueName(const std::filesystem::path& dir,
                                  const std::string& base,
                                  const std::string& extension,
                                  bool directory);

    std::string           m_root;
    AssetManager*         m_assets = nullptr;
    std::filesystem::path m_current;
    std::vector<Entry>    m_entries;
    FilePickerDialog m_importPicker;
    FilePickerDialog m_movePicker;
    std::filesystem::path m_moveSource;
    Dialog m_dialog = Dialog::None;
    bool m_dialogPopupPending = false;
    std::filesystem::path m_dialogPath;
    char m_dialogName[256] = {};
    std::string m_dialogError;

    bool                                  m_autoRefresh = true;
    std::chrono::steady_clock::time_point  m_lastCheck{};
    std::filesystem::file_time_type        m_lastWriteTime{};
    float                                  m_windowMinX = 0.0f;
    float                                  m_windowMinY = 0.0f;
    float                                  m_windowMaxX = 0.0f;
    float                                  m_windowMaxY = 0.0f;
};

} // namespace Galeshi
