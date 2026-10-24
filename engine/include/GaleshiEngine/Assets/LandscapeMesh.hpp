#pragma once
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

namespace Galeshi {

enum class LandscapeBrush {
    Raise = 0,
    Lower,
    Smooth,
    Flatten,
    Noise
};

struct LandscapeBrushSettings {
    LandscapeBrush tool = LandscapeBrush::Raise;
    float radius = 4.0f;
    float strength = 3.0f;
    float falloff = 0.65f;
    float flattenHeight = 0.0f;
    float noiseFrequency = 0.15f;
};

struct LandscapeRaycastHit {
    bool hit = false;
    glm::vec3 localPosition{0.0f};
    glm::vec3 worldPosition{0.0f};
    float height = 0.0f;
    int gridX = 0;
    int gridZ = 0;
};

class LandscapeMesh {
public:
    static void Rebuild(LandscapeComponent& landscape, Mesh& mesh);

    // Updates positions and normals in-place.  Used while sculpting so the
    // vertex/index buffers are not reallocated on every mouse move.
    static void UpdateMesh(const LandscapeComponent& landscape, Mesh& mesh);

    // Bilinear height sample in landscape-local XZ coordinates.
    static float SampleHeight(const LandscapeComponent& landscape, float localX, float localZ);
    static glm::vec3 SampleNormal(const LandscapeComponent& landscape, float localX, float localZ);

    // Casts a world-space ray against the height field.  worldMatrix is the
    // landscape entity's world transform.
    static bool Raycast(const LandscapeComponent& landscape,
                        const glm::mat4& worldMatrix,
                        const glm::vec3& rayOrigin,
                        const glm::vec3& rayDirection,
                        LandscapeRaycastHit& outHit,
                        float maxDistance = 1000.0f);

    // Applies one brush stamp at a landscape-local position.  Returns the
    // number of height samples that changed.
    static int ApplyBrush(LandscapeComponent& landscape,
                          const LandscapeBrushSettings& settings,
                          float localX, float localZ,
                          float dt);

    static void ResetHeights(LandscapeComponent& landscape);
};

} // namespace Galeshi
