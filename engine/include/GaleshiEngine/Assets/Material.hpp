#pragma once

#include <glm/glm.hpp>
#include <memory>
#include <string>

namespace Galeshi {

class Texture;

// Metallic-roughness PBR material.
class Material {
public:
    bool isGlass = false;
    // Render both faces and flip normals on back-facing fragments.
    bool twoSided = false;
    // Independent render state: cull faces pointing away from the camera.
    bool backfaceCulling = true;
public:
    Material() = default;
    ~Material() = default;

    glm::vec4 albedoFactor     {1.0f, 1.0f, 1.0f, 1.0f};
    // Per-map color space. Albedo defaults to sRGB; the remaining slots
    // default to linear data. Each flag is user-editable.
    bool      albedoSrgb       = true;
    bool      metallicRoughnessSrgb = false;
    bool      metallicSrgb     = false;
    bool      roughnessSrgb    = false;
    bool      normalSrgb       = false;
    bool      emissiveSrgb     = true;
    bool      occlusionSrgb    = false;
    bool      opacitySrgb      = false;
    bool      specularSrgb     = false;
    // Landscape paint layer maps. The blend weight comes from the landscape
    // vertex weight channel.
    bool      landscapeAlbedoSrgb = true;
    bool      landscapeOrmSrgb = false;
    bool      landscapeNormalSrgb = false;
    bool      landscapeBlend = false;
    // Tiling and micro-detail for the landscape paint layer maps. The detail
    // octave is a second, higher-frequency sample of the same maps used to
    // break up the macro pattern on large shores.
    float     landscapeUvScale = 1.0f;
    float     landscapeDetailScale = 4.0f;
    float     landscapeDetailStrength = 0.0f;
    float     landscapeNormalStrength = 1.0f;
    float     metallicFactor   = 0.0f;
    // Scales the dielectric specular response. The optional specular map is
    // multiplied by this value. Default dielectric is 0.25 (F0 = 0.01).
    float     specularFactor   = 0.25f;
    // Standard opaque-material default.
    float     roughnessFactor  = 1.0f;
    float     normalStrength   = 1.0f;
    float     occlusionStrength = 1.0f;
    float     alphaCutoff      = 0.5f;
    // Use the albedo texture's alpha channel as an alpha-test mask. glTF
    // MASK materials and near-opaque BLEND cutouts (eyes) store the cutoff
    // there rather than in a separate opacity map.
    bool      alphaTest        = false;
    // Forward alpha-blended material for glTF BLEND surfaces such as hair,
    // eyebrows, eyelashes and similar cards.
    bool      alphaBlend       = false;
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
    std::shared_ptr<Texture> landscapeAlbedoMap;
    std::shared_ptr<Texture> landscapeOrmMap;
    std::shared_ptr<Texture> landscapeNormalMap;

    // Set when one or more imported maps came from a UDIM tile set. The UVs
    // of meshes using this material are remapped into the generated atlas at
    // import time; these values are kept for diagnostics and future tools.
    bool usesUdim = false;
    glm::vec2 udimUvScale{1.0f, 1.0f};
    glm::vec2 udimUvOffset{0.0f, 0.0f};

    std::string name = "Material";
    std::string sourcePath;
    // Optional .mat master recipe. The material is an instance whose base is
    // the source material and whose parameters were produced by this script.
    std::string masterScriptPath;

    // True when this material has been edited in the scene and its values must
    // survive scene reloads instead of being refreshed from the glTF source.
    bool sceneOverride = false;

    // Optional parent material.  A material with a base is an instance: it
    // owns an independent copy of the parameters and texture slots, while the
    // base link keeps the asset lineage for Reset to Base and for preserving
    // the relationship in scene files.
    std::shared_ptr<Material> base;

    bool IsInstance() const { return base != nullptr; }
};

} // namespace Galeshi
