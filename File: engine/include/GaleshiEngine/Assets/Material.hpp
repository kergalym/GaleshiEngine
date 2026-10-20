namespace Galeshi {
    class Material {
    public:
        bool isGlass = false;
        glm::vec4 albedoFactor {1.0f, 1.0f, 1.0f, 1.0f};
        float metallicFactor = 0.0f;
        float roughnessFactor = 0.5f;
        // Add other PBR properties here
    };
}
