#pragma once

#include <string>

namespace Galeshi {

class Scene;
class AssetManager;

class SceneSerializer {
public:
    SceneSerializer(Scene& scene, AssetManager& assets)
        : m_scene(scene), m_assets(assets) {}

    bool Save(const std::string& path) const;
    bool Load(const std::string& path);

private:
    Scene&         m_scene;
    AssetManager&  m_assets;
};

} // namespace Galeshi
