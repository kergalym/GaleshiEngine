#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Galeshi {

class Texture;

// PBR (metallic-roughness) материал.
class Material {
public:
    Material() = default;
    ~Material() = default;

    glm::vec4 albedoFactor     {1.0f, 1.0f, 1.0f, 1.0f};
    float     metallicFactor   = 0.0f;
    float     roughnessFactor  = 0.5f;
    float     normalStrength   = 1.0f;
    glm::vec3 emissiveFactor   {0.0f};

    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> metallicRoughnessMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> emissiveMap;

    std::string name = "Material";
    std::string sourcePath;
};

} // namespace Galeshi
