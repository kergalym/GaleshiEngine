#pragma once

#include <glm/glm.hpp>

namespace Galeshi {

class Camera;
class Mesh;

struct Ray {
    glm::vec3 origin{0.0f};
    glm::vec3 direction{0.0f, 0.0f, -1.0f};
};

// Screen pixel to world ray.
Ray ScreenPointToRay(const Camera& camera,
                     float screenX, float screenY,
                     float viewportX, float viewportY,
                     float viewportW, float viewportH);

// Intersection with the horizontal plane y = planeY.
bool IntersectPlaneY(const Ray& ray, float planeY, glm::vec3& outHit);

// AABB intersection (slab method).
bool IntersectAABB(const Ray& ray,
                   const glm::vec3& boxMin,
                   const glm::vec3& boxMax,
                   float& outTNear);

// Möller-Trumbore triangle intersection.
bool IntersectTriangle(const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir,
                       const glm::vec3& v0,
                       const glm::vec3& v1,
                       const glm::vec3& v2,
                       float& outT);

// Full mesh intersection (tests every triangle).
bool IntersectMesh(const glm::vec3& rayOrigin,
                   const glm::vec3& rayDir,
                   const Mesh& mesh,
                   float& outT);

} // namespace Galeshi
