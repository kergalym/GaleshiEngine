namespace Galeshi {
    class AssetManager {
    public:
        AssetManager();
        ~AssetManager();

        std::shared_ptr<Texture> LoadTexture(const std::string& path);
        std::shared_ptr<Material> LoadMaterial(const std::string& path);

        // Add other PBR-specific asset loading methods here
    };
}
