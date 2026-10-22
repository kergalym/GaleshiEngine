$input v_texcoord0
#include <bgfx_shader.sh>

// Specular prefilter: GGX importance sampling of the environment cubemap.
// One face / mip is rendered into a 2D scratch slice and copied into the
// mip chain of m_prefilteredCube with bgfx::blit.

SAMPLERCUBE(s_envCube, 0);

uniform vec4 u_faceParams;      // x = face size, y = face index
uniform vec4 u_prefilterParams; // x = roughness

#define PI 3.14159265359
#define SAMPLE_COUNT 512u

float radicalInverseVdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10;
}

vec2 hammersley(uint i, uint N)
{
    return vec2(float(i) / float(N), radicalInverseVdC(i));
}

vec3 importanceSampleGGX(vec2 Xi, vec3 N, float roughness)
{
    float a = roughness * roughness;
    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a * a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta * cosTheta);

    vec3 H = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);

    vec3 up = abs(N.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, N));
    vec3 bitangent = cross(N, tangent);

    return normalize(tangent * H.x + bitangent * H.y + N * H.z);
}

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
    vec3 V = N;

    float roughness = clamp(u_prefilterParams.x, 0.0, 1.0);
    vec3 prefiltered = vec3(0.0);
    float totalWeight = 0.0;

    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        vec2 Xi = hammersley(i, SAMPLE_COUNT);
        vec3 H = importanceSampleGGX(Xi, N, roughness);
        vec3 L = normalize(2.0 * dot(V, H) * H - V);

        float NdotL = max(dot(N, L), 0.0);
        if (NdotL > 0.0)
        {
            // The environment is a smooth procedural sky and the source cube
            // has a single mip, so sampling LOD 0 avoids undefined mip data.
            prefiltered += textureCubeLod(s_envCube, L, 0.0).rgb * NdotL;
            totalWeight += NdotL;
        }
    }

    prefiltered /= max(totalWeight, 0.0001);
    gl_FragColor = vec4(prefiltered, 1.0);
}
