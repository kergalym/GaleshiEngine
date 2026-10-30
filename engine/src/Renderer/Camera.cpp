#include "GaleshiEngine/Renderer/Camera.hpp"

#include <bgfx/bgfx.h>
#include <algorithm>
#include <cmath>
#include <glm/gtc/matrix_transform.hpp>

namespace Galeshi {

void Camera::SetPerspective(float fovYRadians, float aspect, float nearZ, float farZ) {
    m_nearZ = std::max(nearZ, 1e-4f);
    m_farZ = std::max(farZ, m_nearZ + 1e-3f);

    // bgfx renders through Vulkan with a [0,1] clip-space depth range. Using
    // glm::perspective (OpenGL's [-1,1] range) pushes the near half of the
    // frustum outside the clip volume and loses depth precision, which shows
    // up as clipped geometry and z-fighting/flicker at larger camera
    // distances. Match the clip convention reported by bgfx.
    const bgfx::Caps* caps = bgfx::getCaps();
    if (caps && caps->homogeneousDepth) {
        m_proj = glm::perspectiveRH_NO(fovYRadians, aspect, m_nearZ, m_farZ);
    } else {
        m_proj = glm::perspectiveRH_ZO(fovYRadians, aspect, m_nearZ, m_farZ);
    }
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
