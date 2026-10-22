$input v_texcoord0
#include <bgfx_shader.sh>

// Diffuse irradiance convolution.
// Input : m_envCubeMap (procedural sky, mip 0)
// Output: one face of m_irradianceCube, rendered into a 2D scratch slice
//         and then copied with bgfx::blit.

SAMPLERCUBE(s_envCube, 0);

uniform vec4 u_faceParams; // x = face size, y = face index

#define PI 3.14159265359

// BGFX/Vulkan cubemap face convention (+X, -X, +Y, -Y, +Z, -Z).
// v_texcoord0.y grows downwards, so the vertical face axis is negated.
vec3 faceDirection(vec2 uv, float face)
{
    vec2 st = uv * 2.0 - 1.0;
    if (face < 0.5)      return normalize(vec3( 1.0, -st.y, -st.x)); // +X
    else if (face < 1.5) return normalize(vec3(-1.0, -st.y,  st.x)); // -X
    else if (face < 2.5) return normalize(vec3( st.x,  1.0,  st.y)); // +Y
    else if (face < 3.5) return normalize(vec3( st.x, -1.0, -st.y)); // -Y
    else if (face < 4.5) return normalize(vec3( st.x, -st.y,  1.0)); // +Z
    else                 return normalize(vec3(-st.x, -st.y, -1.0)); // -Z
}

void main()
{
    vec3 N = faceDirection(v_texcoord0, u_faceParams.y);

    // Build a stable tangent frame around N.
    vec3 up    = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 right = normalize(cross(up, N));
    up         = cross(N, right);

    vec3 irradiance = vec3(0.0);
    float sampleCount = 0.0;

    const int PHI_STEPS   = 12;
    const int THETA_STEPS = 6;

    for (int phi = 0; phi < PHI_STEPS; ++phi)
    {
        float phiAngle = (float(phi) + 0.5) * (2.0 * PI / float(PHI_STEPS));
        for (int theta = 0; theta < THETA_STEPS; ++theta)
        {
            // Cosine-weighted hemisphere sampling.
            float cosTheta = sqrt(1.0 - (float(theta) + 0.5) / float(THETA_STEPS));
            float sinTheta = sqrt(1.0 - cosTheta * cosTheta);
            vec3 tangentDir = vec3(cos(phiAngle) * sinTheta, sin(phiAngle) * sinTheta, cosTheta);
            vec3 dir = normalize(tangentDir.x * right + tangentDir.y * up + tangentDir.z * N);

            irradiance += textureCube(s_envCube, dir).rgb;
            sampleCount += 1.0;
        }
    }

    irradiance = (irradiance / sampleCount) * PI;
    gl_FragColor = vec4(irradiance, 1.0);
}
