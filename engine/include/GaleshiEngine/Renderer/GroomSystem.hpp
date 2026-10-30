#pragma once

#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Scene/Components.hpp"

#include <glm/glm.hpp>
#include <cstdint>
#include <memory>
#include <vector>

namespace Galeshi {

class Scene;
class AssetManager;

struct GroomGuide {
    std::vector<glm::vec3> points; // target-mesh local space, bind pose
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

struct GroomStrand {
    std::vector<glm::vec3> restLocal;   // target-mesh local bind points
    std::vector<glm::vec3> restWorld;   // skinned target space for this frame
    std::vector<glm::vec3> positions;   // simulated world-space points
    std::vector<glm::vec3> velocities;

    glm::vec4 boneIDs{0.0f};
    glm::vec4 boneWeights{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 rootNormal{0.0f, 1.0f, 0.0f};
    float random = 0.0f;
    float widthScale = 1.0f;
    float lengthScale = 1.0f;
};

struct GroomData {
    std::vector<GroomGuide> guides;
    std::vector<GroomStrand> strands;

    std::shared_ptr<Mesh> renderMesh;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;

    entt::entity targetEntity = entt::null;
    uint64_t generationKey = 0;
    int segments = 8;
    int meshSegments = -1;

    float width = 0.006f;
    float rootWidth = 1.0f;
    float tipWidth = 0.08f;

    glm::vec3 boundsCenter{0.0f};
    float boundsRadius = 0.0f;
    float time = 0.0f;
};

class GroomSystem {
public:
    // Generates/refreshes grooms and advances their spring simulation.
    static void Update(Scene& scene, AssetManager& assets, float dt);

    // Rebuilds camera-facing ribbon geometry from the simulated strands.
    static void BuildRenderMesh(GroomData& data, const glm::vec3& cameraPos);
};

} // namespace Galeshi
