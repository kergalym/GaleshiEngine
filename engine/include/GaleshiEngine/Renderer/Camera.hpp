#pragma once

#include <glm/glm.hpp>

namespace Galeshi {

class Camera {
public:
    Camera() = default;

    void SetPerspective(float fovYRadians, float aspect, float nearZ, float farZ);
    void SetProjection(const glm::mat4& projection) { m_proj = projection; }
    void SetView(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up);
    void Orbit(const glm::vec3& target, float yawRad, float pitchRad, float radius);

    const glm::mat4& View()       const { return m_view; }
    const glm::mat4& Projection() const { return m_proj; }
    const glm::mat4  ViewProj()   const { return m_proj * m_view; }
    const glm::vec3& Position()   const { return m_eye; }
    float NearPlane() const { return m_nearZ; }
    float FarPlane()  const { return m_farZ; }

private:
    glm::mat4 m_view{1.0f};
    glm::mat4 m_proj{1.0f};
    glm::vec3 m_eye{0.0f};
    float m_nearZ = 0.1f;
    float m_farZ = 1000.0f;
};

} // namespace Galeshi
