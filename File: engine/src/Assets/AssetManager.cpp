namespace Galeshi {
    AssetManager::AssetManager() {
        m_white      = std::shared_ptr<Texture>(Texture::CreateWhite());
        m_black      = std::shared_ptr<Texture>(Texture::CreateBlack());
        // Load other PBR-specific assets
    }

    std::shared_ptr<Texture> AssetManager::LoadTexture(const std::string& path) {
        auto it = m_textures.find(path);
        if (it != m_textures.end()) return it->second;
        Texture* raw = Texture::LoadFromFile(path);
        if (!raw) return m_white;
        std::shared_ptr<Texture> tex(raw);
        m_textures[path] = tex;
        return tex;
    }

    std::shared_ptr<Material> AssetManager::LoadMaterial(const std::string& path) {
        // Load PBR material from file
        return nullptr;
    }
}
