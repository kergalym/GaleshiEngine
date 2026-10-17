#include "GaleshiEngine/Renderer/RayPick.hpp"
#include "GaleshiEngine/Renderer/Camera.hpp"
#include "GaleshiEngine/Assets/Mesh.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <limits>
#include <cmath>

namespace Galeshi {

Ray ScreenPointToRay(const Camera& camera,
                     float screenX, float screenY,
                     float viewportX, float viewportY,
                     float viewportW, float viewportH)
{
    const float ndcX = ((screenX - viewportX) / viewportW) * 2.0f - 1.0f;
    const float ndcY = 1.0f - ((screenY - viewportY) / viewportH) * 2.0f;

    const glm::mat4 invVP = glm::inverse(camera.ViewProj());

    glm::vec4 nearP = invVP * glm::vec4(ndcX, ndcY, -1.0f, 1.0f);
    glm::vec4 farP  = invVP * glm::vec4(ndcX, ndcY,  1.0f, 1.0f);

    nearP /= nearP.w;
    farP  /= farP.w;

    Ray r;
    r.origin    = glm::vec3(nearP);
    r.direction = glm::normalize(glm::vec3(farP) - glm::vec3(nearP));
    return r;
}

bool IntersectPlaneY(const Ray& ray, float planeY, glm::vec3& outHit) {
    const float denom = ray.direction.y;
    if (std::abs(denom) < 1e-6f) return false;
    const float t = (planeY - ray.origin.y) / denom;
    if (t < 0.0f) return false;
    outHit = ray.origin + ray.direction * t;
    return true;
}

bool IntersectAABB(const Ray& ray,
                   const glm::vec3& boxMin,
                   const glm::vec3& boxMax,
                   float& outTNear)
{
    constexpr float INF = std::numeric_limits<float>::max();
    float tmin = -INF;
    float tmax =  INF;

    for (int i = 0; i < 3; ++i) {
        const float invD = 1.0f / (ray.direction[i] != 0.0f ? ray.direction[i] : 1e-8f);
        float t1 = (boxMin[i] - ray.origin[i]) * invD;
        float t2 = (boxMax[i] - ray.origin[i]) * invD;
        if (t1 > t2) std::swap(t1, t2);
        tmin = std::max(tmin, t1);
        tmax = std::min(tmax, t2);
        if (tmin > tmax) return false;
    }
    if (tmax < 0.0f) return false;
    outTNear = (tmin >= 0.0f) ? tmin : tmax;
    return true;
}

bool IntersectTriangle(const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir,
                       const glm::vec3& v0,
                       const glm::vec3& v1,
                       const glm::vec3& v2,
                       float& outT)
{
    constexpr float EPS = 1e-7f;
    const glm::vec3 e1 = v1 - v0;
    const glm::vec3 e2 = v2 - v0;
    const glm::vec3 p  = glm::cross(rayDir, e2);
    const float det = glm::dot(e1, p);
    if (std::abs(det) < EPS) return false;
    const float invDet = 1.0f / det;

    const glm::vec3 tvec = rayOrigin - v0;
    const float u = glm::dot(tvec, p) * invDet;
    if (u < 0.0f || u > 1.0f) return false;

    const glm::vec3 q = glm::cross(tvec, e1);
    const float v = glm::dot(rayDir, q) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;

    const float t = glm::dot(e2, q) * invDet;
    if (t < EPS) return false;
    outT = t;
    return true;
}

bool IntersectMesh(const glm::vec3& rayOrigin,
                   const glm::vec3& rayDir,
                   const Mesh& mesh,
                   float& outT)
{
    const auto& verts = mesh.Vertices();
    const auto& idxs  = mesh.Indices();
    if (verts.empty() || idxs.size() < 3) return false;

    glm::vec3 bmin, bmax;
    mesh.LocalAABB(bmin, bmax);
    constexpr float pad = 1e-4f;
    bmin -= glm::vec3(pad);
    bmax += glm::vec3(pad);

    Ray localRay{ rayOrigin, rayDir };
    float tAabb = 0.0f;
    if (!IntersectAABB(localRay, bmin, bmax, tAabb)) return false;

    float bestT = std::numeric_limits<float>::max();
    bool  hit   = false;

    for (size_t i = 0; i + 2 < idxs.size(); i += 3) {
        const glm::vec3& v0 = verts[idxs[i    ]].position;
        const glm::vec3& v1 = verts[idxs[i + 1]].position;
        const glm::vec3& v2 = verts[idxs[i + 2]].position;

        float t;
        if (IntersectTriangle(rayOrigin, rayDir, v0, v1, v2, t)) {
            if (t < bestT) { bestT = t; hit = true; }
        }
    }

    if (hit) outT = bestT;
    return hit;
}

} // namespace Galeshi
