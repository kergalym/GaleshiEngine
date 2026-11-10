#pragma once

#include <atomic>
#include <memory>
#include <nlohmann/json_fwd.hpp>
#include <string>

namespace Galeshi {

class Scene;
class AssetManager;
class Material;
struct PostProcessSettings;

// Material JSON round-trip, shared by scene save/load and the editor's
// per-character material presets (.matp).
nlohmann::json MaterialToJson(const Material& material,
                              const AssetManager& assets,
                              bool includeTextures = true);
std::shared_ptr<Material> MaterialFromJson(const nlohmann::json& jm,
                                           AssetManager& assets,
                                           const std::string& textureRoot);

// Post-process settings round-trip, shared by scene volumes and the
// editor's PP.settings export/import.
nlohmann::json PostProcessSettingsToJson(const PostProcessSettings& s);
void PostProcessSettingsFromJson(const nlohmann::json& js,
                                 PostProcessSettings& s);

class SceneSerializer {
public:
    SceneSerializer(Scene& scene, AssetManager& assets)
        : m_scene(scene), m_assets(assets) {}

    bool Save(const std::string& path) const;

    // Optional cancel flag lets the async loading dialog abort between
    // entities instead of waiting for the whole scene to finish.
    bool Load(const std::string& path,
              const std::atomic<bool>* cancel = nullptr);

    // Chunked binary scene. Save() writes this as a sidecar next to JSON.
    bool SaveBinary(const std::string& path) const;
    bool LoadBinary(const std::string& path,
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

    void SetContentBrowserPath(std::string path);
    const std::string& ContentBrowserPath() const {
        return m_contentBrowserPath;
    }

private:
    nlohmann::json SaveToJson(bool includeEditorState) const;
    bool LoadFromJson(const nlohmann::json& root,
                      const std::atomic<bool>* cancel);

    Scene&        m_scene;
    AssetManager& m_assets;
    std::string   m_codeEditorPath;
    std::string   m_codeEditorText;
    bool          m_codeEditorCppMode = false;
    std::string   m_contentBrowserPath;
};

} // namespace Galeshi
