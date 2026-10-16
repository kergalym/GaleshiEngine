#include "GaleshiEngine/Renderer/Camera.hpp"

#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Galeshi {

void Camera::SetPerspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    m_proj = glm::perspective(fovYRadians, aspect, nearZ, farZ);
}

void Camera::SetView(const glm::vec3& eye, const glm::vec3& target, const glm::vec3& up) {
    m_eye  = eye;
    m_view = glm::lookAt(eye, target, up);
}

void Camera::Orbit(const glm::vec3& target, float yawRad, float pitchRad, float radius) {
    const float cp = std::cos(pitchRad);
    const float sp = std::sin(pitchRad);
    const float cy = std::cos(yawRad);
    const float sy = std::sin(yawRad);
    const glm::vec3 offset{
        radius * cp * sy,
        radius * sp,
        radius * cp * cy,
    };
    SetView(target + offset, target, {0.0f, 1.0f, 0.0f});
}

} // namespace Galeshi
