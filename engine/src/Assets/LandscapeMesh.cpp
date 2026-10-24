#include "GaleshiEngine/Assets/LandscapeMesh.hpp"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Galeshi {
namespace {

float Clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }

uint32_t Hash2D(int x, int z, uint32_t seed) {
    uint32_t h = seed;
    h ^= static_cast<uint32_t>(x) * 0x8da6b343u;
    h ^= static_cast<uint32_t>(z) * 0xd8163841u;
    h ^= h >> 13;
    h *= 0x85ebca6bu;
    h ^= h >> 16;
    return h;
}

float HashToFloat(uint32_t h) {
    return static_cast<float>(h & 0x00FFFFFFu) / static_cast<float>(0x00FFFFFFu);
}

float ValueNoise(float x, float z, uint32_t seed) {
    const int ix = static_cast<int>(std::floor(x));
    const int iz = static_cast<int>(std::floor(z));
    const float fx = x - static_cast<float>(ix);
    const float fz = z - static_cast<float>(iz);
    const float sx = fx * fx * (3.0f - 2.0f * fx);
    const float sz = fz * fz * (3.0f - 2.0f * fz);

    const float a = HashToFloat(Hash2D(ix,     iz,     seed));
    const float b = HashToFloat(Hash2D(ix + 1, iz,     seed));
    const float c = HashToFloat(Hash2D(ix,     iz + 1, seed));
    const float d = HashToFloat(Hash2D(ix + 1, iz + 1, seed));
    return glm::mix(glm::mix(a, b, sx), glm::mix(c, d, sx), sz) * 2.0f - 1.0f;
}

glm::vec3 SafeNormalize(const glm::vec3& v, const glm::vec3& fallback) {
    const float len = glm::length(v);
    return len > 1e-6f ? v / len : fallback;
}

bool RayAabb(const glm::vec3& origin, const glm::vec3& dir,
             const glm::vec3& bmin, const glm::vec3& bmax,
             float& tEnter, float& tExit) {
    tEnter = 0.0f;
    tExit = std::numeric_limits<float>::max();
    for (int axis = 0; axis < 3; ++axis) {
        const float o = origin[axis];
        const float d = dir[axis];
        if (std::abs(d) < 1e-8f) {
            if (o < bmin[axis] || o > bmax[axis]) return false;
            continue;
        }
        float t0 = (bmin[axis] - o) / d;
        float t1 = (bmax[axis] - o) / d;
        if (t0 > t1) std::swap(t0, t1);
        tEnter = std::max(tEnter, t0);
        tExit  = std::min(tExit,  t1);
        if (tEnter > tExit) return false;
    }
    return tExit >= 0.0f;
}

} // namespace

void LandscapeMesh::Rebuild(LandscapeComponent& lc, Mesh& mesh) {
    lc.sizeX = std::max(2, lc.sizeX);
    lc.sizeZ = std::max(2, lc.sizeZ);
    lc.scale = std::max(0.001f, lc.scale);

    const size_t expected = static_cast<size_t>(lc.sizeX) * static_cast<size_t>(lc.sizeZ);
    if (lc.heights.size() != expected) lc.heights.resize(expected, 0.0f);

    std::vector<Vertex> verts;
    verts.reserve(expected);

    for (int z = 0; z < lc.sizeZ; ++z) {
        for (int x = 0; x < lc.sizeX; ++x) {
            Vertex v{};
            v.position = glm::vec3(
                (x - lc.sizeX / 2.0f) * lc.scale,
                lc.heights[static_cast<size_t>(z) * lc.sizeX + x],
                (z - lc.sizeZ / 2.0f) * lc.scale
            );
            v.normal = glm::vec3(0.0f, 1.0f, 0.0f);
            v.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            v.uv = glm::vec2(static_cast<float>(x) / lc.sizeX,
                             static_cast<float>(z) / lc.sizeZ);
            v.boneIDs = glm::vec4(0.0f);
            v.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
            verts.push_back(v);
        }
    }

    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(lc.sizeX - 1) * (lc.sizeZ - 1) * 6);
    for (int z = 0; z < lc.sizeZ - 1; ++z) {
        for (int x = 0; x < lc.sizeX - 1; ++x) {
            const uint32_t tl = static_cast<uint32_t>(z * lc.sizeX + x);
            const uint32_t tr = tl + 1;
            const uint32_t bl = static_cast<uint32_t>((z + 1) * lc.sizeX + x);
            const uint32_t br = bl + 1;
            indices.push_back(tl); indices.push_back(bl); indices.push_back(tr);
            indices.push_back(tr); indices.push_back(bl); indices.push_back(br);
        }
    }

    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        const glm::vec3& p0 = verts[indices[i]].position;
        const glm::vec3& p1 = verts[indices[i + 1]].position;
        const glm::vec3& p2 = verts[indices[i + 2]].position;
        const glm::vec3 n = glm::cross(p1 - p0, p2 - p0);
        if (glm::dot(n, n) > 1e-12f) {
            const glm::vec3 nn = glm::normalize(n);
            verts[indices[i]].normal += nn;
            verts[indices[i + 1]].normal += nn;
            verts[indices[i + 2]].normal += nn;
        }
    }
    for (auto& v : verts) {
        v.normal = SafeNormalize(v.normal, glm::vec3(0.0f, 1.0f, 0.0f));
    }

    mesh.Upload(verts, indices);
}

void LandscapeMesh::UpdateMesh(const LandscapeComponent& lc, Mesh& mesh) {
    const size_t expected = static_cast<size_t>(lc.sizeX) * static_cast<size_t>(lc.sizeZ);
    if (!mesh.Valid() || lc.heights.size() != expected ||
        mesh.Vertices().size() != expected) {
        auto copy = lc;
        Rebuild(copy, mesh);
        return;
    }

    std::vector<glm::vec3> positions;
    positions.reserve(expected);
    for (int z = 0; z < lc.sizeZ; ++z) {
        for (int x = 0; x < lc.sizeX; ++x) {
            positions.emplace_back(
                (x - lc.sizeX / 2.0f) * lc.scale,
                lc.heights[static_cast<size_t>(z) * lc.sizeX + x],
                (z - lc.sizeZ / 2.0f) * lc.scale);
        }
    }
    mesh.UpdateVertices(positions);
}

float LandscapeMesh::SampleHeight(const LandscapeComponent& lc, float localX, float localZ) {
    if (lc.sizeX < 2 || lc.sizeZ < 2 || lc.heights.empty()) return 0.0f;
    const float gx = localX / std::max(0.001f, lc.scale) + lc.sizeX * 0.5f;
    const float gz = localZ / std::max(0.001f, lc.scale) + lc.sizeZ * 0.5f;
    const float cx = std::clamp(gx, 0.0f, static_cast<float>(lc.sizeX - 1));
    const float cz = std::clamp(gz, 0.0f, static_cast<float>(lc.sizeZ - 1));
    const int x0 = std::min(static_cast<int>(std::floor(cx)), lc.sizeX - 1);
    const int z0 = std::min(static_cast<int>(std::floor(cz)), lc.sizeZ - 1);
    const int x1 = std::min(x0 + 1, lc.sizeX - 1);
    const int z1 = std::min(z0 + 1, lc.sizeZ - 1);
    const float fx = cx - static_cast<float>(x0);
    const float fz = cz - static_cast<float>(z0);

    const auto& h = lc.heights;
    const float h00 = h[static_cast<size_t>(z0) * lc.sizeX + x0];
    const float h10 = h[static_cast<size_t>(z0) * lc.sizeX + x1];
    const float h01 = h[static_cast<size_t>(z1) * lc.sizeX + x0];
    const float h11 = h[static_cast<size_t>(z1) * lc.sizeX + x1];
    return glm::mix(glm::mix(h00, h10, fx), glm::mix(h01, h11, fx), fz);
}

glm::vec3 LandscapeMesh::SampleNormal(const LandscapeComponent& lc, float localX, float localZ) {
    const float step = std::max(0.01f, lc.scale);
    const float hL = SampleHeight(lc, localX - step, localZ);
    const float hR = SampleHeight(lc, localX + step, localZ);
    const float hD = SampleHeight(lc, localX, localZ - step);
    const float hU = SampleHeight(lc, localX, localZ + step);
    return SafeNormalize(glm::vec3(hL - hR, 2.0f * step, hD - hU),
                         glm::vec3(0.0f, 1.0f, 0.0f));
}

bool LandscapeMesh::Raycast(const LandscapeComponent& lc,
                            const glm::mat4& worldMatrix,
                            const glm::vec3& rayOrigin,
                            const glm::vec3& rayDirection,
                            LandscapeRaycastHit& outHit,
                            float maxDistance) {
    outHit = {};
    if (lc.sizeX < 2 || lc.sizeZ < 2 || lc.heights.empty()) return false;

    glm::mat4 invWorld = glm::inverse(worldMatrix);
    glm::vec4 localOrigin4 = invWorld * glm::vec4(rayOrigin, 1.0f);
    glm::vec4 localDir4 = invWorld * glm::vec4(rayDirection, 0.0f);
    glm::vec3 localOrigin = glm::vec3(localOrigin4) / (std::abs(localOrigin4.w) > 1e-8f ? localOrigin4.w : 1.0f);
    // Leave the inverse-scaled direction unnormalized: the ray parameter t
    // then matches world distance, which keeps maxDistance and the AABB entry
    // distance meaningful for scaled landscape entities.
    const glm::vec3 localDir = glm::vec3(localDir4);
    const float localDirLength = glm::length(localDir);
    if (localDirLength < 1e-6f) return false;

    const float halfX = lc.sizeX * 0.5f * lc.scale;
    const float halfZ = lc.sizeZ * 0.5f * lc.scale;
    float minH = std::numeric_limits<float>::max();
    float maxH = std::numeric_limits<float>::lowest();
    for (float h : lc.heights) {
        minH = std::min(minH, h);
        maxH = std::max(maxH, h);
    }
    const glm::vec3 bmin(-halfX, minH - 0.5f, -halfZ);
    const glm::vec3 bmax( halfX, maxH + 0.5f,  halfZ);

    float tEnter = 0.0f, tExit = 0.0f;
    if (!RayAabb(localOrigin, localDir, bmin, bmax, tEnter, tExit)) return false;
    tEnter = std::max(tEnter, 0.0f);
    tExit = std::min(tExit, maxDistance);

    const float localStep = std::max(0.08f, std::min(lc.scale * 0.5f, 0.5f));
    const float step = localStep / localDirLength;
    float previousT = tEnter;
    glm::vec3 previousPoint = localOrigin + localDir * previousT;
    bool previousValid = std::abs(previousPoint.x) <= halfX && std::abs(previousPoint.z) <= halfZ;

    for (float t = tEnter + step; t <= tExit; t += step) {
        const glm::vec3 p = localOrigin + localDir * t;
        if (std::abs(p.x) > halfX || std::abs(p.z) > halfZ) {
            previousT = t;
            previousPoint = p;
            previousValid = false;
            continue;
        }
        const float h = SampleHeight(lc, p.x, p.z);
        if (p.y <= h) {
            float lo = previousValid ? previousT : std::max(tEnter, t - step);
            float hi = t;
            glm::vec3 hit = p;
            for (int i = 0; i < 16; ++i) {
                const float mid = (lo + hi) * 0.5f;
                const glm::vec3 q = localOrigin + localDir * mid;
                if (q.y <= SampleHeight(lc, q.x, q.z)) {
                    hi = mid;
                    hit = q;
                } else {
                    lo = mid;
                }
            }
            hit.y = SampleHeight(lc, hit.x, hit.z);
            outHit.hit = true;
            outHit.localPosition = hit;
            outHit.worldPosition = glm::vec3(worldMatrix * glm::vec4(hit, 1.0f));
            outHit.height = hit.y;
            outHit.gridX = std::clamp(static_cast<int>(std::floor(hit.x / lc.scale + lc.sizeX * 0.5f)), 0, lc.sizeX - 1);
            outHit.gridZ = std::clamp(static_cast<int>(std::floor(hit.z / lc.scale + lc.sizeZ * 0.5f)), 0, lc.sizeZ - 1);
            return true;
        }
        previousT = t;
        previousPoint = p;
        previousValid = true;
    }
    return false;
}

int LandscapeMesh::ApplyBrush(LandscapeComponent& lc,
                              const LandscapeBrushSettings& settings,
                              float localX, float localZ,
                              float dt) {
    if (lc.sizeX < 2 || lc.sizeZ < 2) return 0;
    const size_t expected = static_cast<size_t>(lc.sizeX) * static_cast<size_t>(lc.sizeZ);
    if (lc.heights.size() != expected) lc.heights.resize(expected, 0.0f);
    if (settings.radius <= 0.0f || dt <= 0.0f) return 0;

    const std::vector<float> original = lc.heights;
    const float centerX = localX / lc.scale + lc.sizeX * 0.5f;
    const float centerZ = localZ / lc.scale + lc.sizeZ * 0.5f;
    const float radiusCells = settings.radius / lc.scale;
    const int minX = std::max(0, static_cast<int>(std::floor(centerX - radiusCells)));
    const int maxX = std::min(lc.sizeX - 1, static_cast<int>(std::ceil(centerX + radiusCells)));
    const int minZ = std::max(0, static_cast<int>(std::floor(centerZ - radiusCells)));
    const int maxZ = std::min(lc.sizeZ - 1, static_cast<int>(std::ceil(centerZ + radiusCells)));
    const float strength = std::max(0.0f, settings.strength);
    const float exponent = glm::mix(0.25f, 2.0f, Clamp01(settings.falloff));
    int changed = 0;

    for (int z = minZ; z <= maxZ; ++z) {
        for (int x = minX; x <= maxX; ++x) {
            const float dx = (static_cast<float>(x) - centerX) * lc.scale;
            const float dz = (static_cast<float>(z) - centerZ) * lc.scale;
            const float dist = std::sqrt(dx * dx + dz * dz);
            if (dist > settings.radius) continue;

            const float t = 1.0f - dist / settings.radius;
            const float weight = std::pow(Clamp01(t), exponent);
            if (weight <= 0.0f) continue;

            const size_t index = static_cast<size_t>(z) * lc.sizeX + x;
            float& height = lc.heights[index];
            const float before = height;

            switch (settings.tool) {
                case LandscapeBrush::Raise:
                    height += strength * dt * weight;
                    break;
                case LandscapeBrush::Lower:
                    height -= strength * dt * weight;
                    break;
                case LandscapeBrush::Smooth: {
                    const int x0 = std::max(0, x - 1);
                    const int x1 = std::min(lc.sizeX - 1, x + 1);
                    const int z0 = std::max(0, z - 1);
                    const int z1 = std::min(lc.sizeZ - 1, z + 1);
                    float sum = 0.0f;
                    int count = 0;
                    for (int sz = z0; sz <= z1; ++sz) {
                        for (int sx = x0; sx <= x1; ++sx) {
                            sum += original[static_cast<size_t>(sz) * lc.sizeX + sx];
                            ++count;
                        }
                    }
                    const float average = count > 0 ? sum / static_cast<float>(count) : before;
                    const float blend = Clamp01(strength * dt * 2.0f / std::max(0.25f, settings.radius * 0.3f));
                    height = glm::mix(before, average, blend * weight);
                    break;
                }
                case LandscapeBrush::Flatten: {
                    const float blend = Clamp01(strength * dt * 2.0f / std::max(0.25f, settings.radius * 0.3f));
                    height = glm::mix(before, settings.flattenHeight, blend * weight);
                    break;
                }
                case LandscapeBrush::Noise: {
                    const float n = ValueNoise(
                        static_cast<float>(x) * settings.noiseFrequency,
                        static_cast<float>(z) * settings.noiseFrequency,
                        lc.sizeX * 73856093u ^ lc.sizeZ * 19349663u);
                    height += n * strength * dt * weight;
                    break;
                }
            }

            if (std::abs(height - before) > 1e-7f) ++changed;
        }
    }
    return changed;
}

void LandscapeMesh::ResetHeights(LandscapeComponent& lc) {
    const size_t expected = static_cast<size_t>(std::max(2, lc.sizeX)) * static_cast<size_t>(std::max(2, lc.sizeZ));
    lc.heights.assign(expected, 0.0f);
}

} // namespace Galeshi
