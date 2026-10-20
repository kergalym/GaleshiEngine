#pragma once
#include <glm/glm.hpp>

namespace Galeshi {

struct Plane {
    glm::vec3 normal;
    float distance;
    Plane() : normal(0.0f), distance(0.0f) {}
    Plane(const glm::vec3& n, float d) : normal(n), distance(d) {}
    void Normalize() {
        float len = glm::length(normal);
        normal /= len;
        distance /= len;
    }
};

class Frustum {
public:
    Plane planes[6];

    Frustum(const glm::mat4& vp) {
        // Gribb-Hartmann plane extraction (Vulkan NDC Z is [0, 1])
        glm::vec4 row0 = glm::vec4(vp[0][0], vp[1][0], vp[2][0], vp[3][0]);
        glm::vec4 row1 = glm::vec4(vp[0][1], vp[1][1], vp[2][1], vp[3][1]);
        glm::vec4 row2 = glm::vec4(vp[0][2], vp[1][2], vp[2][2], vp[3][2]);
        glm::vec4 row3 = glm::vec4(vp[0][3], vp[1][3], vp[2][3], vp[3][3]);

        // Left, Right, Bottom, Top
        planes[0] = Plane(glm::vec3(row3 + row0), (row3 + row0).w);
        planes[1] = Plane(glm::vec3(row3 - row0), (row3 - row0).w);
        planes[2] = Plane(glm::vec3(row3 + row1), (row3 + row1).w);
        planes[3] = Plane(glm::vec3(row3 - row1), (row3 - row1).w);
        
        // Near, Far
        planes[4] = Plane(glm::vec3(row2), row2.w);
        planes[5] = Plane(glm::vec3(row3 - row2), (row3 - row2).w);

        for (int i = 0; i < 6; ++i) planes[i].Normalize();
    }

    bool TestSphere(const glm::vec3& center, float radius) const {
        for (int i = 0; i < 6; ++i) {
            if (glm::dot(planes[i].normal, center) + planes[i].distance < -radius) {
                return false; // Сфера полностью снаружи пирамиды
            }
        }
        return true;
    }
};

} // namespace Galeshi
