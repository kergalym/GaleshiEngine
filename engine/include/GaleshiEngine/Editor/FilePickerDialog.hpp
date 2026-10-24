#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Galeshi {

class FilePickerDialog {
public:
    FilePickerDialog() = default;

    // Режим открытия файла
    void Open(const std::string& startDir,
              const std::string& title,
              std::vector<std::string> extensions);

    // Режим сохранения файла (с полем ввода имени)
    // Режим выбора папки
    void PickFolder(const std::string& startDir, const std::string& title);

    void SaveAs(const std::string& startDir,
                const std::string& defaultName,
                const std::string& title,
                std::vector<std::string> extensions);

    bool IsOpen()  const { return m_open; }
    bool IsSave()  const { return m_saveMode; }
    void Close()   { m_open = false; }

    // Рисуется каждый кадр. Возвращает выбранный/введённый путь, если
    // пользователь нажал Open/Save (пустая строка иначе).
    std::string Draw();

private:
    void Refresh();

    bool         m_open = false;
    bool         m_pickFolderMode = false;
    bool         m_saveMode = false;
    std::string  m_title = "Open File";
    std::string  m_root;
    std::filesystem::path m_current;
    std::string  m_selectedFile;
    char         m_saveNameBuf[256] = {};
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
