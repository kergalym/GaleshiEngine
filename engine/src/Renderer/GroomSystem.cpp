#include "GaleshiEngine/Renderer/GroomSystem.hpp"

#include "GaleshiEngine/Assets/AssetManager.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"
#include "GaleshiEngine/Scene/Scene.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <random>

namespace Galeshi {
namespace {

glm::mat4 EntityWorldMatrix(Scene& scene, entt::entity handle) {
    glm::mat4 model(1.0f);
    entt::entity current = handle;
    std::vector<entt::entity> chain;
    int depth = 0;
    while (current != entt::null && depth++ < 128) {
        chain.push_back(current);
        if (scene.Registry().all_of<ParentComponent>(current)) {
            current = scene.Registry().get<ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) {
        if (scene.Registry().all_of<TransformComponent>(*it)) {
            model = model * scene.Registry().get<TransformComponent>(*it).LocalMatrix();
        }
    }
    return model;
}

bool IsDescendantOf(Scene& scene, entt::entity entity, entt::entity ancestor) {
    entt::entity current = entity;
    int depth = 0;
    while (current != entt::null && depth++ < 128) {
        if (current == ancestor) return true;
        if (scene.Registry().all_of<ParentComponent>(current)) {
            current = scene.Registry().get<ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    return false;
}

entt::entity FindTargetMesh(Scene& scene, entt::entity groomEntity,
                            const GroomComponent& groom) {
    auto& reg = scene.Registry();

    if (!groom.targetMeshName.empty()) {
        if (Entity named = scene.FindEntityByName(groom.targetMeshName)) {
            if (reg.all_of<MeshComponent>(named.Handle())) return named.Handle();
        }
    }

    if (reg.all_of<MeshComponent>(groomEntity)) return groomEntity;

    entt::entity best = entt::null;
    float bestTop = -std::numeric_limits<float>::max();
    float bestRadius = std::numeric_limits<float>::max();

    auto meshes = reg.view<MeshComponent>();
    for (auto h : meshes) {
        if (!IsDescendantOf(scene, h, groomEntity)) continue;
        const auto& mc = meshes.get<MeshComponent>(h);
        if (!mc.mesh || !mc.mesh->Valid()) continue;

        glm::vec3 localMin, localMax;
        mc.mesh->LocalAABB(localMin, localMax);
        const glm::mat4 world = EntityWorldMatrix(scene, h);
        const glm::vec3 center = glm::vec3(world * glm::vec4(
            (localMin + localMax) * 0.5f, 1.0f));
        const float maxScale = std::max(
            std::max(glm::length(glm::vec3(world[0])),
                     glm::length(glm::vec3(world[1]))),
            glm::length(glm::vec3(world[2])));
        const float radius = glm::length(localMax - localMin) * 0.5f * maxScale;

        // The highest mesh is the head cap. If two meshes share the same top
        // (for example a body mesh and a separate head mesh), prefer the
        // smaller one.
        float top = -std::numeric_limits<float>::max();
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local{
                (corner & 1) ? localMax.x : localMin.x,
                (corner & 2) ? localMax.y : localMin.y,
                (corner & 4) ? localMax.z : localMin.z,
            };
            top = std::max(top, glm::vec3(world * glm::vec4(local, 1.0f)).y);
        }

        if (top > bestTop + 0.01f ||
            (std::abs(top - bestTop) <= 0.01f && radius < bestRadius)) {
            bestTop = top;
            bestRadius = radius;
            best = h;
        }
        (void)center;
    }
    return best;
}

entt::entity FindSkeletonRoot(Scene& scene, entt::entity entity) {
    entt::entity current = entity;
    int depth = 0;
    while (current != entt::null && depth++ < 128) {
        if (scene.Registry().all_of<SkeletalComponent>(current)) return current;
        if (scene.Registry().all_of<ParentComponent>(current)) {
            current = scene.Registry().get<ParentComponent>(current).parent;
        } else {
            break;
        }
    }
    return entt::null;
}

glm::mat4 BuildSkinMatrix(const std::vector<glm::mat4>& bones,
                          const glm::vec4& ids, const glm::vec4& weights) {
    if (bones.size() < 256) return glm::mat4(1.0f);
    glm::mat4 result(0.0f);
    float total = 0.0f;
    for (int j = 0; j < 4; ++j) {
        const int id = std::clamp(static_cast<int>(ids[j] + 0.5f), 0, 255);
        const float weight = weights[j];
        if (weight <= 0.0f) continue;
        result += bones[static_cast<size_t>(id)] * weight;
        total += weight;
    }
    if (total <= 1e-6f) return glm::mat4(1.0f);
    return result / total;
}

struct RootSample {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec3 flow{0.0f, -1.0f, 0.0f};
    glm::vec4 boneIDs{0.0f};
    glm::vec4 boneWeights{1.0f, 0.0f, 0.0f, 0.0f};
};

struct TriangleCandidate {
    uint32_t i0 = 0;
    uint32_t i1 = 0;
    uint32_t i2 = 0;
    float area = 0.0f;
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
};

void MixKey(uint64_t& key, uint64_t value) {
    key ^= value;
    key *= 1099511628211ull;
}

uint64_t MakeGenerationKey(const GroomComponent& groom, entt::entity target,
                           const Mesh* mesh) {
    uint64_t key = 1469598103934665603ull;
    MixKey(key, static_cast<uint64_t>(target));
    MixKey(key, reinterpret_cast<uintptr_t>(mesh));
    MixKey(key, static_cast<uint64_t>(groom.seed));
    MixKey(key, static_cast<uint64_t>(std::max(groom.guideCount, 1)));
    MixKey(key, static_cast<uint64_t>(std::max(groom.strandsPerGuide, 1)));
    MixKey(key, static_cast<uint64_t>(std::clamp(groom.segments, 2, 32)));

    auto mixFloat = [&key](float value) {
        uint32_t bits = 0;
        std::memcpy(&bits, &value, sizeof(bits));
        MixKey(key, bits);
    };
    mixFloat(groom.length);
    mixFloat(groom.clumpRadius);
    mixFloat(groom.clumping);
    mixFloat(groom.frizz);
    mixFloat(groom.curl);
    mixFloat(groom.volume);
    mixFloat(groom.scalpStart);
    mixFloat(groom.scalpEnd);
    mixFloat(groom.normalThreshold);
    mixFloat(groom.groomDirection.x);
    mixFloat(groom.groomDirection.y);
    mixFloat(groom.groomDirection.z);
    for (char c : groom.targetMeshName) MixKey(key, static_cast<uint64_t>(c));
    return key;
}

std::shared_ptr<GroomData> GenerateGroom(Scene& scene, entt::entity target,
                                         const GroomComponent& groom) {
    auto data = std::make_shared<GroomData>();
    data->targetEntity = target;
    data->segments = std::clamp(groom.segments, 2, 32);
    data->width = std::max(groom.width, 0.0001f);
    data->rootWidth = std::max(groom.rootWidth, 0.0f);
    data->tipWidth = std::max(groom.tipWidth, 0.0f);

    const auto& mc = scene.Registry().get<MeshComponent>(target);
    if (!mc.mesh || !mc.mesh->Valid()) return data;
    const auto& vertices = mc.mesh->Vertices();
    const auto& indices = mc.mesh->Indices();
    if (vertices.size() < 3 || indices.size() < 3) return data;

    float minY = std::numeric_limits<float>::max();
    float maxY = -std::numeric_limits<float>::max();
    for (const auto& vertex : vertices) {
        minY = std::min(minY, vertex.position.y);
        maxY = std::max(maxY, vertex.position.y);
    }
    const float height = maxY - minY;
    if (height <= 1e-6f) return data;

    const float start = std::clamp(std::min(groom.scalpStart, groom.scalpEnd),
                                   0.0f, 1.0f);
    const float end = std::clamp(std::max(groom.scalpStart, groom.scalpEnd),
                                 0.0f, 1.0f);
    const float bandMin = minY + height * start;
    const float bandMax = minY + height * end;

    auto collectCandidates = [&](bool requireNormal) {
        std::vector<TriangleCandidate> out;
        out.reserve(indices.size() / 3);
        for (size_t i = 0; i + 2 < indices.size(); i += 3) {
            const uint32_t i0 = indices[i + 0];
            const uint32_t i1 = indices[i + 1];
            const uint32_t i2 = indices[i + 2];
            if (i0 >= vertices.size() || i1 >= vertices.size() ||
                i2 >= vertices.size()) {
                continue;
            }
            const glm::vec3 p0 = vertices[i0].position;
            const glm::vec3 p1 = vertices[i1].position;
            const glm::vec3 p2 = vertices[i2].position;
            const glm::vec3 centroid = (p0 + p1 + p2) / 3.0f;
            if (centroid.y < bandMin || centroid.y > bandMax) continue;

            glm::vec3 normal = glm::cross(p1 - p0, p2 - p0);
            const float twiceArea = glm::length(normal);
            if (twiceArea < 1e-9f) continue;
            normal /= twiceArea;
            // Procedural primitives and some imported meshes have a face
            // winding that disagrees with the authored vertex normals. Align
            // the face normal with the interpolated vertex normal so the
            // upper scalp cap is not accidentally rejected.
            const glm::vec3 vertexNormal =
                vertices[i0].normal + vertices[i1].normal + vertices[i2].normal;
            if (glm::length(vertexNormal) > 1e-6f &&
                glm::dot(normal, vertexNormal) < 0.0f) {
                normal = -normal;
            }
            if (requireNormal && normal.y < groom.normalThreshold) continue;

            out.push_back({i0, i1, i2, twiceArea * 0.5f, normal});
        }
        return out;
    };

    std::vector<TriangleCandidate> candidates = collectCandidates(true);
    if (candidates.empty()) candidates = collectCandidates(false);
    if (candidates.empty()) return data;

    std::vector<float> cumulative;
    cumulative.reserve(candidates.size());
    float totalArea = 0.0f;
    for (const auto& candidate : candidates) {
        totalArea += std::max(candidate.area, 1e-8f);
        cumulative.push_back(totalArea);
    }

    std::mt19937 rng(groom.seed == 0 ? 1u : groom.seed);
    std::uniform_real_distribution<float> dist(0.0f, 1.0f);
    auto random = [&]() { return dist(rng); };

    auto sampleRoot = [&]() -> RootSample {
        RootSample sample;
        const float pick = random() * totalArea;
        const auto it = std::upper_bound(cumulative.begin(), cumulative.end(), pick);
        const size_t index = static_cast<size_t>(
            it == cumulative.end() ? cumulative.size() - 1
                                   : std::distance(cumulative.begin(), it));
        const TriangleCandidate& triangle = candidates[index];
        const glm::vec3 p0 = vertices[triangle.i0].position;
        const glm::vec3 p1 = vertices[triangle.i1].position;
        const glm::vec3 p2 = vertices[triangle.i2].position;

        float u = random();
        float v = random();
        if (u + v > 1.0f) {
            u = 1.0f - u;
            v = 1.0f - v;
        }
        const float w = 1.0f - u - v;

        sample.position = p0 * u + p1 * v + p2 * w;
        glm::vec3 normal = vertices[triangle.i0].normal * u +
                           vertices[triangle.i1].normal * v +
                           vertices[triangle.i2].normal * w;
        if (glm::length(normal) < 1e-5f) normal = triangle.normal;
        sample.normal = glm::normalize(normal);
        if (sample.normal.y < groom.normalThreshold) {
            sample.normal = triangle.normal;
        }

        // Pick the dominant source vertex so the root inherits a coherent
        // set of bone influences without expanding to more than four bones.
        uint32_t source = triangle.i0;
        if (v > u && v > w) source = triangle.i1;
        else if (w > u && w > v) source = triangle.i2;
        sample.boneIDs = vertices[source].boneIDs;
        sample.boneWeights = vertices[source].boneWeights;
        const float weightSum = sample.boneWeights.x + sample.boneWeights.y +
                                sample.boneWeights.z + sample.boneWeights.w;
        if (weightSum < 1e-5f) {
            sample.boneWeights = glm::vec4(1.0f, 0.0f, 0.0f, 0.0f);
        }

        glm::vec3 direction = groom.groomDirection;
        if (glm::length(direction) < 1e-5f) direction = glm::vec3(0.0f, -1.0f, 0.0f);
        direction = glm::normalize(direction);
        sample.flow = direction - sample.normal * glm::dot(direction, sample.normal);
        if (glm::length(sample.flow) < 1e-4f) {
            sample.flow = glm::cross(sample.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (glm::length(sample.flow) < 1e-4f) {
            sample.flow = glm::cross(sample.normal, glm::vec3(1.0f, 0.0f, 0.0f));
        }
        sample.flow = glm::normalize(sample.flow);
        sample.position += sample.normal * 0.006f;
        return sample;
    };

    const int guides = std::clamp(groom.guideCount, 1, 4096);
    const int strandsPerGuide = std::clamp(groom.strandsPerGuide, 1, 64);
    const int segments = data->segments;
    const float length = std::max(groom.length, 0.001f);
    const float step = length / static_cast<float>(segments);

    data->guides.reserve(static_cast<size_t>(guides));
    data->strands.reserve(static_cast<size_t>(guides) *
                          static_cast<size_t>(strandsPerGuide));

    for (int guideIndex = 0; guideIndex < guides; ++guideIndex) {
        const RootSample root = sampleRoot();
        std::vector<glm::vec3> points(static_cast<size_t>(segments + 1));
        points[0] = root.position;

        const glm::vec3 sideSeed = glm::cross(root.normal, root.flow);
        const glm::vec3 side = glm::length(sideSeed) > 1e-5f
                                   ? glm::normalize(sideSeed)
                                   : glm::vec3(1.0f, 0.0f, 0.0f);
        // Start with a clear normal lift so the crown is visible instead of
        // immediately diving into the scalp, then bend toward gravity.
        glm::vec3 direction = glm::normalize(
            root.normal * (0.12f + groom.volume * 0.16f) +
            root.flow * 0.95f +
            side * (random() - 0.5f) * 0.14f);
        const glm::vec3 falling = glm::normalize(
            root.flow * 0.38f + root.normal * 0.05f +
            glm::vec3(0.0f, -0.85f, 0.0f) * 0.72f);

        for (int s = 1; s <= segments; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(segments);
            const float phase = random() * 6.2831853f;
            const glm::vec3 jitter =
                side * std::cos(phase) * groom.curl * 0.6f +
                root.normal * std::sin(phase) * groom.curl * 0.25f;
            glm::vec3 target = glm::normalize(
                glm::mix(direction, falling, 0.14f + t * 0.42f) +
                root.normal * (groom.volume * 0.08f * (1.0f - t)) + jitter);
            direction = glm::normalize(glm::mix(direction, target, 0.22f));
            points[static_cast<size_t>(s)] =
                points[static_cast<size_t>(s - 1)] + direction * step;
            points[static_cast<size_t>(s)] +=
                root.normal * (groom.volume * step * 0.6f *
                               std::sin(3.14159265f * t));
        }

        GroomGuide guide;
        guide.points = points;
        guide.normal = root.normal;
        data->guides.push_back(guide);

        const glm::vec3 guideTangent = glm::length(points[1] - points[0]) > 1e-6f
            ? glm::normalize(points[1] - points[0])
            : root.flow;
        glm::vec3 strandSide = glm::cross(root.normal, guideTangent);
        if (glm::length(strandSide) < 1e-5f) {
            strandSide = glm::cross(root.normal, glm::vec3(0.0f, 1.0f, 0.0f));
        }
        if (glm::length(strandSide) < 1e-5f) {
            strandSide = glm::vec3(1.0f, 0.0f, 0.0f);
        }
        strandSide = glm::normalize(strandSide);

        for (int strandIndex = 0; strandIndex < strandsPerGuide; ++strandIndex) {
            GroomStrand strand;
            strand.restLocal.resize(static_cast<size_t>(segments + 1));
            strand.boneIDs = root.boneIDs;
            strand.boneWeights = root.boneWeights;
            strand.rootNormal = root.normal;
            strand.random = random();
            strand.lengthScale = 0.82f + random() * 0.34f;
            strand.widthScale = 0.75f + random() * 0.50f;

            const float angle = random() * 6.2831853f;
            const float radius = std::sqrt(random()) * std::max(groom.clumpRadius, 0.0f);
            const glm::vec3 rootOffset =
                (strandSide * std::cos(angle) + guideTangent * std::sin(angle)) * radius;
            const float frizzPhase = random() * 6.2831853f;

            for (int s = 0; s <= segments; ++s) {
                const float t = static_cast<float>(s) / static_cast<float>(segments);
                const glm::vec3 base = points[static_cast<size_t>(s)] - points[0];
                glm::vec3 point = points[0] +
                                  rootOffset * (1.0f - groom.clumping * t) +
                                  base * strand.lengthScale;
                const float wave = std::sin(frizzPhase + t * 8.0f) *
                                   groom.frizz * length * t;
                const float crossWave = std::cos(frizzPhase * 1.31f + t * 5.0f) *
                                        groom.frizz * length * 0.45f * t;
                point += strandSide * wave + root.normal * crossWave;
                point += strandSide *
                         (std::sin(t * 6.2831853f + frizzPhase) *
                          groom.curl * length * t);
                point += root.normal * 0.0015f;
                strand.restLocal[static_cast<size_t>(s)] = point;
            }
            data->strands.push_back(std::move(strand));
        }
    }

    return data;
}

void UpdateSimulation(Scene& scene, entt::entity target,
                      const GroomComponent& groom, GroomData& data, float dt) {
    const auto& mc = scene.Registry().get<MeshComponent>(target);
    if (!mc.mesh || !mc.mesh->Valid() || data.strands.empty()) {
        data.boundsCenter = glm::vec3(0.0f);
        data.boundsRadius = 0.0f;
        return;
    }

    const glm::mat4 targetWorld = EntityWorldMatrix(scene, target);
    entt::entity skeleton = FindSkeletonRoot(scene, target);
    const std::vector<glm::mat4>* bones = nullptr;
    if (skeleton != entt::null) {
        const auto& skel = scene.Registry().get<SkeletalComponent>(skeleton);
        if (skel.boneMatrices.size() >= 256) bones = &skel.boneMatrices;
    }

    const float delta = std::clamp(dt, 0.0f, 0.033f);
    data.time += std::max(dt, 0.0f);
    glm::vec3 windDirection = groom.windDirection;
    if (glm::length(windDirection) > 1e-5f) {
        windDirection = glm::normalize(windDirection);
    } else {
        windDirection = glm::vec3(0.0f, 0.0f, 0.0f);
    }

    const bool simulate = groom.simulate;
    for (GroomStrand& strand : data.strands) {
        const glm::mat4 skin = bones
            ? BuildSkinMatrix(*bones, strand.boneIDs, strand.boneWeights)
            : glm::mat4(1.0f);
        const glm::mat4 bindToWorld = targetWorld * skin;

        strand.restWorld.resize(strand.restLocal.size());
        for (size_t i = 0; i < strand.restLocal.size(); ++i) {
            strand.restWorld[i] = glm::vec3(
                bindToWorld * glm::vec4(strand.restLocal[i], 1.0f));
        }

        if (strand.positions.size() != strand.restWorld.size() ||
            strand.velocities.size() != strand.restWorld.size()) {
            strand.positions = strand.restWorld;
            strand.velocities.assign(strand.restWorld.size(), glm::vec3(0.0f));
        }

        const glm::vec3 root = strand.restWorld.front();
        if (glm::distance(strand.positions.front(), root) > 0.75f) {
            strand.positions = strand.restWorld;
            strand.velocities.assign(strand.restWorld.size(), glm::vec3(0.0f));
        }

        strand.positions.front() = root;
        strand.velocities.front() = glm::vec3(0.0f);

        if (!simulate || delta <= 0.0f) {
            strand.positions = strand.restWorld;
            strand.velocities.assign(strand.restWorld.size(), glm::vec3(0.0f));
        } else {
            const float gust = 0.55f + 0.45f *
                std::sin(data.time * groom.windFrequency * 6.2831853f +
                         strand.random * 6.2831853f);
            const glm::vec3 wind = windDirection *
                (std::max(groom.windStrength, 0.0f) * gust);
            const glm::vec3 gravity(0.0f, -9.81f * groom.gravity, 0.0f);

            for (size_t i = 1; i < strand.positions.size(); ++i) {
                const glm::vec3 toRest = strand.restWorld[i] - strand.positions[i];
                const glm::vec3 acceleration =
                    gravity + wind + toRest * (groom.stiffness * 22.0f);
                glm::vec3& velocity = strand.velocities[i];
                velocity += acceleration * delta;
                velocity *= std::exp(-std::max(groom.damping, 0.0f) * delta * 8.0f);
                const float speed = glm::length(velocity);
                if (speed > 18.0f) velocity *= 18.0f / speed;
                strand.positions[i] += velocity * delta;
            }

            // Preserve segment lengths. The root is kinematic, so this also
            // communicates head motion down the chain.
            for (int iteration = 0; iteration < 2; ++iteration) {
                strand.positions.front() = root;
                for (size_t i = 1; i < strand.positions.size(); ++i) {
                    const glm::vec3 deltaPos = strand.positions[i] -
                                               strand.positions[i - 1];
                    const float currentLength = glm::length(deltaPos);
                    const float targetLength = glm::distance(
                        strand.restWorld[i - 1], strand.restWorld[i]);
                    if (currentLength > 1e-6f) {
                        strand.positions[i] = strand.positions[i - 1] +
                            deltaPos * (targetLength / currentLength);
                    }
                }
            }
        }
    }

    glm::vec3 boundsMin(std::numeric_limits<float>::max());
    glm::vec3 boundsMax(-std::numeric_limits<float>::max());
    bool hasPoints = false;
    for (const GroomStrand& strand : data.strands) {
        for (const glm::vec3& point : strand.positions) {
            boundsMin = glm::min(boundsMin, point);
            boundsMax = glm::max(boundsMax, point);
            hasPoints = true;
        }
    }
    if (hasPoints) {
        data.boundsCenter = (boundsMin + boundsMax) * 0.5f;
        data.boundsRadius = glm::length(boundsMax - boundsMin) * 0.5f;
    } else {
        data.boundsCenter = glm::vec3(targetWorld[3]);
        data.boundsRadius = 0.0f;
    }
}

} // namespace

void GroomSystem::Update(Scene& scene, AssetManager& assets, float dt) {
    (void)assets;
    auto& reg = scene.Registry();
    auto view = reg.view<GroomComponent>();
    for (auto handle : view) {
        GroomComponent& groom = view.get<GroomComponent>(handle);
        if (!groom.enabled) {
            groom.runtime.reset();
            continue;
        }

        const entt::entity target = FindTargetMesh(scene, handle, groom);
        if (target == entt::null) {
            groom.runtime.reset();
            continue;
        }

        const auto& targetMesh = reg.get<MeshComponent>(target);
        if (!targetMesh.mesh || !targetMesh.mesh->Valid()) {
            groom.runtime.reset();
            continue;
        }

        const uint64_t key =
            MakeGenerationKey(groom, target, targetMesh.mesh.get());
        if (!groom.runtime || groom.runtime->generationKey != key ||
            groom.runtime->targetEntity != target) {
            auto data = GenerateGroom(scene, target, groom);
            data->generationKey = key;
            data->targetEntity = target;
            groom.runtime = std::move(data);
        }

        if (!groom.runtime) continue;
        groom.runtime->width = std::max(groom.width, 0.0001f);
        groom.runtime->rootWidth = std::max(groom.rootWidth, 0.0f);
        groom.runtime->tipWidth = std::max(groom.tipWidth, 0.0f);
        UpdateSimulation(scene, target, groom, *groom.runtime, dt);
    }
}

void GroomSystem::BuildRenderMesh(GroomData& data,
                                  const glm::vec3& cameraPos) {
    if (data.strands.empty() || data.segments < 2) return;

    const size_t pointsPerStrand = static_cast<size_t>(data.segments + 1);
    const size_t vertexCount = data.strands.size() * pointsPerStrand * 2;
    const size_t indexCount = data.strands.size() *
                              static_cast<size_t>(data.segments) * 6;
    data.vertices.resize(vertexCount);

    if (data.indices.size() != indexCount ||
        data.meshSegments != data.segments) {
        data.indices.clear();
        data.indices.reserve(indexCount);
        for (size_t s = 0; s < data.strands.size(); ++s) {
            const uint32_t base = static_cast<uint32_t>(
                s * pointsPerStrand * 2);
            for (int j = 0; j < data.segments; ++j) {
                const uint32_t i0 = base + static_cast<uint32_t>(j) * 2;
                const uint32_t i1 = i0 + 1;
                const uint32_t i2 = i0 + 2;
                const uint32_t i3 = i0 + 3;
                data.indices.push_back(i0);
                data.indices.push_back(i2);
                data.indices.push_back(i1);
                data.indices.push_back(i1);
                data.indices.push_back(i2);
                data.indices.push_back(i3);
            }
        }
        data.meshSegments = data.segments;
    }

    const glm::vec3 up(0.0f, 1.0f, 0.0f);
    size_t vertexOffset = 0;
    for (const GroomStrand& strand : data.strands) {
        if (strand.positions.size() != pointsPerStrand) {
            data.vertices.resize(vertexOffset);
            break;
        }
        for (int j = 0; j <= data.segments; ++j) {
            const size_t index = static_cast<size_t>(j);
            glm::vec3 tangent(0.0f, 1.0f, 0.0f);
            if (j == 0) {
                tangent = strand.positions[1] - strand.positions[0];
            } else if (j == data.segments) {
                tangent = strand.positions[index] - strand.positions[index - 1];
            } else {
                tangent = strand.positions[index + 1] -
                          strand.positions[index - 1];
            }
            if (glm::length(tangent) < 1e-6f) tangent = strand.rootNormal;
            if (glm::length(tangent) < 1e-6f) tangent = up;
            tangent = glm::normalize(tangent);

            const glm::vec3 toCamera = cameraPos - strand.positions[index];
            glm::vec3 side = glm::cross(tangent, toCamera);
            if (glm::length(side) < 1e-6f) side = glm::cross(tangent, up);
            if (glm::length(side) < 1e-6f) side = glm::vec3(1.0f, 0.0f, 0.0f);
            side = glm::normalize(side);

            glm::vec3 normal = glm::cross(side, tangent);
            if (glm::length(normal) < 1e-6f) normal = strand.rootNormal;
            normal = glm::normalize(normal);

            const float t = static_cast<float>(j) /
                            static_cast<float>(data.segments);
            const float taper = glm::mix(data.rootWidth, data.tipWidth, t);
            const float width = std::max(
                data.width * strand.widthScale * taper, 0.0001f);

            Vertex v0{};
            Vertex v1{};
            v0.position = strand.positions[index] - side * (width * 0.5f);
            v1.position = strand.positions[index] + side * (width * 0.5f);
            v0.normal = normal;
            v1.normal = normal;
            v0.tangent = tangent;
            v1.tangent = tangent;
            v0.uv = {0.0f, t};
            v1.uv = {1.0f, t};
            v0.boneIDs = glm::vec4(strand.random, 0.0f, 0.0f, 0.0f);
            v1.boneIDs = v0.boneIDs;
            data.vertices[vertexOffset++] = v0;
            data.vertices[vertexOffset++] = v1;
        }
    }
    if (data.vertices.size() != vertexCount) return;

    if (!data.renderMesh) data.renderMesh = std::make_shared<Mesh>();
    if (!data.renderMesh->Valid() ||
        data.renderMesh->IndexCount() != data.indices.size()) {
        data.renderMesh->Upload(data.vertices, data.indices);
    } else {
        data.renderMesh->UpdateVertexData(data.vertices);
    }
}

} // namespace Galeshi
