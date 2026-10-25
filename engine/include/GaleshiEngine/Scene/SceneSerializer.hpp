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

    // Для Undo/Redo
    std::string SaveToString() const;
    bool LoadFromString(const std::string& jsonStr,
                        const std::atomic<bool>* cancel = nullptr);

private:
    Scene&        m_scene;
    AssetManager& m_assets;
};

} // namespace Galeshi
