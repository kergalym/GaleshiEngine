#pragma once

#include <atomic>
#include <string>

namespace Galeshi {

class Scene;
class AssetManager;

class SceneSerializer {
public:
    SceneSerializer(Scene& scene, AssetManager& assets)
        : m_scene(scene), m_assets(assets) {}

    bool Save(const std::string& path) const;

    // Optional cancel flag lets the async loading dialog abort between
    // entities instead of waiting for the whole scene to finish.
    bool Load(const std::string& path,
              const std::atomic<bool>* cancel = nullptr);

    // Undo/Redo snapshots. Editor UI state is serialized only when requested
    // by a real scene save, not by history snapshots.
    std::string SaveToString(bool includeEditorState = false) const;
    bool LoadFromString(const std::string& jsonStr,
                        const std::atomic<bool>* cancel = nullptr);

    void SetCodeEditorState(std::string path, std::string text,
                            bool cppMode);
    const std::string& CodeEditorPath() const { return m_codeEditorPath; }
    const std::string& CodeEditorText() const { return m_codeEditorText; }
    bool CodeEditorCppMode() const { return m_codeEditorCppMode; }

private:
    Scene&        m_scene;
    AssetManager& m_assets;
    std::string   m_codeEditorPath;
    std::string   m_codeEditorText;
    bool          m_codeEditorCppMode = false;
};

} // namespace Galeshi
