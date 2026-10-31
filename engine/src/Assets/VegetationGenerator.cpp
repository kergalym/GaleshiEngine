#include "GaleshiEngine/Assets/VegetationGenerator.hpp"
#include "GaleshiEngine/Assets/LandscapeMesh.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <random>

namespace Galeshi {
namespace {

float SlopeAt(const LandscapeComponent& landscape, float x, float z) {
    const glm::vec3 n = LandscapeMesh::SampleNormal(landscape, x, z);
    const float ny = std::max(0.001f, n.y);
    return std::sqrt(std::max(0.0f, 1.0f - ny * ny)) / ny;
}

bool ValidPlacement(const LandscapeComponent& landscape,
                    const VegetationComponent& vegetation,
                    float x, float z, float slopeMultiplier = 1.0f) {
    if (std::abs(x) > landscape.sizeX * 0.5f * landscape.scale) return false;
    if (std::abs(z) > landscape.sizeZ * 0.5f * landscape.scale) return false;
    return SlopeAt(landscape, x, z) <= vegetation.slopeLimit * slopeMultiplier;
}

float RandomRange(std::mt19937& rng, float lo, float hi) {
    std::uniform_real_distribution<float> dist(lo, hi);
    return dist(rng);
}

} // namespace

void VegetationGenerator::Clear(VegetationComponent& vegetation) {
    vegetation.treeInstances.clear();
    vegetation.grassInstances.clear();
    vegetation.paintedTreeInstances.clear();
    vegetation.paintedGrassInstances.clear();
    for (auto& type : vegetation.treeTypes) {
        type.generatedInstances.clear();
        type.paintedInstances.clear();
    }
    vegetation.dirty = false;
}

void VegetationGenerator::Generate(const LandscapeComponent& landscape,
                                   VegetationComponent& vegetation) {
    // Rebuild generated instances only. Painted instances are authored data
    // and must survive density/seed/type changes and regeneration.
    vegetation.treeInstances.clear();
    vegetation.grassInstances.clear();
    for (auto& type : vegetation.treeTypes) {
        type.generatedInstances.clear();
    }
    vegetation.dirty = false;
    if (!vegetation.enabled) return;
    if (vegetation.hideDefaultInstances) return;
    if (landscape.sizeX < 2 || landscape.sizeZ < 2 || landscape.heights.empty()) return;

    const int treeCount = std::clamp(vegetation.treeCount, 0, 20000);
    const int grassCount = std::clamp(vegetation.grassCount, 0, 50000);
    if (treeCount == 0 && grassCount == 0) return;

    std::mt19937 rng(vegetation.seed);
    const float halfX = landscape.sizeX * 0.5f * landscape.scale;
    const float halfZ = landscape.sizeZ * 0.5f * landscape.scale;
    const float maxMargin = std::max(0.0f, std::min(halfX, halfZ) * 0.9f);
    const float margin = std::min(std::max(0.5f, landscape.scale * 1.25f), maxMargin);

    // A single jittered position grid is shared by all species so different
    // tree models do not stack on top of each other.
    std::vector<double> speciesWeights;
    double totalDensity = 0.0;
    if (!vegetation.treeTypes.empty()) {
        speciesWeights.reserve(vegetation.treeTypes.size());
        for (const auto& type : vegetation.treeTypes) {
            const double weight = std::max(0.0, static_cast<double>(type.density));
            speciesWeights.push_back(weight);
            totalDensity += weight;
        }
    }
    std::discrete_distribution<int> chooseSpecies;
    if (totalDensity > 0.0) {
        chooseSpecies = std::discrete_distribution<int>(
            speciesWeights.begin(), speciesWeights.end());
    }

    if (treeCount > 0) {
        const int columns = std::max(1, static_cast<int>(
            std::ceil(std::sqrt(static_cast<float>(treeCount)))));
        const int rows = std::max(1, (treeCount + columns - 1) / columns);
        const float cellW = (halfX * 2.0f - margin * 2.0f) /
                            static_cast<float>(columns);
        const float cellH = (halfZ * 2.0f - margin * 2.0f) /
                            static_cast<float>(rows);

        for (int i = 0; i < treeCount; ++i) {
            const int row = i / columns;
            const int col = i % columns;
            const float x = -halfX + margin +
                (col + 0.5f + RandomRange(rng, -0.38f, 0.38f)) * cellW;
            const float z = -halfZ + margin +
                (row + 0.5f + RandomRange(rng, -0.38f, 0.38f)) * cellH;
            if (!ValidPlacement(landscape, vegetation, x, z)) continue;

            const float y = LandscapeMesh::SampleHeight(landscape, x, z);
            const float baseScale = RandomRange(
                rng, vegetation.treeMinScale, vegetation.treeMaxScale);
            const float yaw = RandomRange(rng, 0.0f, 6.2831853f);

            if (vegetation.treeTypes.empty()) {
                const float widthScale = baseScale *
                    RandomRange(rng, 0.85f, 1.18f);
                glm::mat4 transform = glm::translate(
                    glm::mat4(1.0f), glm::vec3(x, y, z));
                transform = glm::rotate(transform, yaw,
                                        glm::vec3(0.0f, 1.0f, 0.0f));
                transform = glm::scale(
                    transform, glm::vec3(widthScale, baseScale, widthScale));
                vegetation.treeInstances.push_back(transform);
                continue;
            }

            if (totalDensity <= 0.0) continue;
            const int typeIndex = chooseSpecies(rng);
            if (typeIndex < 0 ||
                typeIndex >= static_cast<int>(vegetation.treeTypes.size())) {
                continue;
            }
            auto& type = vegetation.treeTypes[static_cast<size_t>(typeIndex)];
            if (type.density <= 0.0f) continue;
            // Custom species use their own Size (native model scale) rather
            // than the procedural tree scale range.
            const float scale = std::max(type.scale, 0.01f) *
                                RandomRange(rng, 0.92f, 1.08f);
            const float widthScale = scale * RandomRange(rng, 0.90f, 1.10f);
            glm::mat4 transform = glm::translate(
                glm::mat4(1.0f), glm::vec3(x, y, z));
            transform = glm::rotate(transform, yaw,
                                    glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(
                transform, glm::vec3(widthScale, scale, widthScale));
            type.generatedInstances.push_back(transform);
        }
    }

    if (grassCount > 0) {
        vegetation.grassInstances.reserve(grassCount);
        const int maxAttempts = grassCount * 10;
        int attempts = 0;
        while (static_cast<int>(vegetation.grassInstances.size()) < grassCount &&
               attempts++ < maxAttempts) {
            const float x = RandomRange(rng, -halfX + margin, halfX - margin);
            const float z = RandomRange(rng, -halfZ + margin, halfZ - margin);
            if (!ValidPlacement(landscape, vegetation, x, z, 1.4f)) continue;

            const float y = LandscapeMesh::SampleHeight(landscape, x, z);
            const float scale = RandomRange(
                rng, vegetation.grassMinScale, vegetation.grassMaxScale);
            const float yaw = RandomRange(rng, 0.0f, 6.2831853f);

            glm::mat4 transform = glm::translate(
                glm::mat4(1.0f), glm::vec3(x, y, z));
            transform = glm::rotate(transform, yaw,
                                    glm::vec3(0.0f, 1.0f, 0.0f));
            transform = glm::scale(transform,
                                   glm::vec3(scale, scale, scale));
            vegetation.grassInstances.push_back(transform);
        }
    }
}

} // namespace Galeshi
