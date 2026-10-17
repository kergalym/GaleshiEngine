#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Galeshi {

// Модальный файловый диалог на ImGui.
// Использование:
//     picker.Open("assets", "3D Models", {".fbx", ".gltf", ".glb", ".obj"});
//     ...
//     std::string chosen = picker.Draw();
//     if (!chosen.empty()) { /* загрузить модель */ }
class FilePickerDialog {
public:
    FilePickerDialog() = default;

    void Open(const std::string& startDir,
              const std::string& title,
              std::vector<std::string> extensions);

    bool IsOpen() const { return m_open; }
    void Close() { m_open = false; }

    // Рисуется каждый кадр. Возвращает выбранный путь, если пользователь
    // нажал "Open" (пустая строка иначе).
    std::string Draw();

private:
    void Refresh();

    bool         m_open = false;
    std::string  m_title = "Open File";
    std::string  m_root;
    std::filesystem::path m_current;
    std::string  m_selectedFile;
    std::vector<std::string> m_allowedExts;

    struct Entry {
        std::filesystem::path path;
        std::string           name;
        bool                  isDirectory;
    };
    std::vector<Entry> m_entries;
    bool m_dirty = true;
};

} // namespace Galeshi
