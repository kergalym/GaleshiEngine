$input v_texcoord0
#include <bgfx_shader.sh>

// Prefilter one cubemap face / mip into a 2D scratch slice.
SAMPLERCUBE(s_envCube, 0);

uniform vec4 u_faceParams;      // x = face size, y = face index
uniform vec4 u_prefilterParams; // x = roughness

#define PREFILTER_PI 3.14159265359
#define PREFILTER_TAPS 512u

float vanDerCorput(uint value)
{
    float result = 0.0;
    float fraction = 0.5;
    for (uint bit = 0u; bit < 32u; ++bit) {
        result += fraction * float(value & 1u);
        value >>= 1u;
        fraction *= 0.5;
    }
    return result;
}

vec2 lowDiscrepancyPair(uint index, uint total)
{
    return vec2((float(index) + 0.5) / float(total), vanDerCorput(index));
}

vec3 ggxMicroNormal(vec2 uvPair, vec3 normal, float roughness)
{
    float alpha = roughness * roughness;
    float alpha4 = alpha * alpha;
    float cosTheta = sqrt(max(0.0, (1.0 - uvPair.y) /
                                  (1.0 + (alpha4 - 1.0) * uvPair.y)));
    float sinTheta = sqrt(max(0.0, 1.0 - cosTheta * cosTheta));
    float phi = 2.0 * PREFILTER_PI * uvPair.x;

    vec3 local = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

vec3 cubeFaceDirection(vec2 uv, float face)
{
    vec2 st = uv * 2.0 - 1.0;
    if (face < 0.5)      return normalize(vec3( 1.0, -st.y, -st.x));
    else if (face < 1.5) return normalize(vec3(-1.0, -st.y,  st.x));
    else if (face < 2.5) return normalize(vec3( st.x,  1.0,  st.y));
    else if (face < 3.5) return normalize(vec3( st.x, -1.0, -st.y));
    else if (face < 4.5) return normalize(vec3( st.x, -st.y,  1.0));
    else                 return normalize(vec3(-st.x, -st.y, -1.0));
}

void main()
{
    vec3 normal = cubeFaceDirection(v_texcoord0, u_faceParams.y);
    vec3 viewDir = normal;
    float roughness = clamp(u_prefilterParams.x, 0.0, 1.0);

    vec3 accumulated = vec3(0.0);
    float weightSum = 0.0;

    for (uint index = 0u; index < PREFILTER_TAPS; ++index) {
        vec3 microNormal = ggxMicroNormal(
            lowDiscrepancyPair(index, PREFILTER_TAPS), normal, roughness);
        vec3 lightDir = normalize(2.0 * dot(viewDir, microNormal) * microNormal - viewDir);
        float ndl = max(dot(normal, lightDir), 0.0);
        if (ndl <= 0.0) continue;

        // The procedural sky is single-mip; sample LOD 0.
        accumulated += textureCubeLod(s_envCube, lightDir, 0.0).rgb * ndl;
        weightSum += ndl;
    }

    accumulated /= max(weightSum, 0.0001);
    gl_FragColor = vec4(accumulated, 1.0);
}
