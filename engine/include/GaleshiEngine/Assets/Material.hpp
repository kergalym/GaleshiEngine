#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Galeshi {

class Texture;

// PBR (metallic-roughness) материал.
class Material {
public:
    bool isGlass = false;
    // Render both faces and flip normals on back-facing fragments.
    bool twoSided = false;
public:
    Material() = default;
    ~Material() = default;

    glm::vec4 albedoFactor     {1.0f, 1.0f, 1.0f, 1.0f};
    float     metallicFactor   = 0.0f;
    float     roughnessFactor  = 0.5f;
    float     normalStrength   = 1.0f;
    float     occlusionStrength = 1.0f;
    float     alphaCutoff      = 0.5f;
    float     glassOpacity     = 0.35f;
    glm::vec3 emissiveFactor   {0.0f};

    // ---------------------------------------------------------------------
    // Subsurface scattering (deferred opaque materials)
    // ---------------------------------------------------------------------
    // The scatter color is multiplied by strength before it is written to
    // the lighting G-buffer. Radius is expressed in world units and controls
    // how far the wrapped diffuse lobe bleeds past the terminator.
    bool      subsurfaceEnabled  = false;
    glm::vec3 subsurfaceColor    {0.82f, 0.28f, 0.18f};
    float     subsurfaceStrength = 0.55f;
    float     subsurfaceRadius   = 0.85f;

    // ---------------------------------------------------------------------
    // Lit translucency (forward, alpha-blended)
    // ---------------------------------------------------------------------
    // Translucent materials are excluded from the deferred G-buffer and are
    // rendered after the opaque/water/hair passes. Thickness and
    // translucencyShadowDepth modulate how much direct light is transmitted
    // from behind the surface through the shadow depth map.
    bool      translucencyEnabled = false;
    glm::vec3 translucencyColor   {0.95f, 0.72f, 0.42f};
    float     translucencyOpacity = 0.55f;
    float     translucencyStrength = 1.0f;
    float     translucencyThickness = 0.35f;
    float     translucencyDistortion = 0.25f;
    float     translucencyShadowDepth = 1.0f;

    // ---------------------------------------------------------------------
    // Shadow depth controls
    // ---------------------------------------------------------------------
    bool  castsShadow = true;
    // World-space offset along the surface normal applied when this material
    // is rendered into the directional/point shadow depth maps. Useful for
    // thin cards and for pushing a caster slightly toward the light.
    float shadowDepthOffset = 0.0f;

    std::shared_ptr<Texture> albedoMap;
    std::shared_ptr<Texture> metallicRoughnessMap;
    std::shared_ptr<Texture> metallicMap;
    std::shared_ptr<Texture> roughnessMap;
    std::shared_ptr<Texture> normalMap;
    std::shared_ptr<Texture> emissiveMap;
    std::shared_ptr<Texture> occlusionMap;
    std::shared_ptr<Texture> opacityMap;
    std::shared_ptr<Texture> specularMap;

    std::string name = "Material";
    std::string sourcePath;

    // Optional parent material.  A material with a base is an instance: it
    // owns an independent copy of the parameters and texture slots, while the
    // base link keeps the asset lineage for Reset to Base and for preserving
    // the relationship in scene files.
    std::shared_ptr<Material> base;

    bool IsInstance() const { return base != nullptr; }
};

} // namespace Galeshi
