$input v_texcoord0
#include <bgfx_shader.sh>

uniform vec4 u_viewportSize;

#define BRDF_PI 3.14159265359

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
    float phi = 2.0 * BRDF_PI * uvPair.x;

    vec3 local = vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
    vec3 up = abs(normal.y) < 0.999 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 tangent = normalize(cross(up, normal));
    vec3 bitangent = cross(normal, tangent);
    return normalize(tangent * local.x + bitangent * local.y + normal * local.z);
}

// UE4 BRDF.ush: height-correlated joint Smith approximation. The
// split-sum LUT must use the same visibility term as direct lighting.
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / max(Vis_SmithV + Vis_SmithL, 0.000001);
}

vec2 integrateBrdf(float ndv, float roughness)
{
    const uint pairCount = 1024u;
    vec3 viewDir = vec3(sqrt(max(0.0, 1.0 - ndv * ndv)), 0.0, ndv);
    vec3 normal = vec3(0.0, 0.0, 1.0);
    float sumA = 0.0;
    float sumB = 0.0;

    for (uint index = 0u; index < pairCount; ++index) {
        vec3 microNormal = ggxMicroNormal(
            lowDiscrepancyPair(index, pairCount), normal, roughness);
        vec3 lightDir = normalize(2.0 * dot(viewDir, microNormal) * microNormal - viewDir);
        float ndl = max(lightDir.z, 0.0);
        if (ndl <= 0.0) continue;

        float ndh = max(microNormal.z, 0.000001);
        float vdh = max(dot(viewDir, microNormal), 0.0);
        float a2 = roughness * roughness;
        a2 *= a2;
        float vis = Vis_SmithJointApprox(a2, ndv, ndl);
        // G_Vis = G * VoH / (NoH * NoV), with G = 4 * NoV * NoL * Vis.
        float visibility = 4.0 * ndl * vdh * vis / ndh;
        float fresnel = pow(1.0 - vdh, 5.0);
        sumA += (1.0 - fresnel) * visibility;
        sumB += fresnel * visibility;
    }

    return vec2(sumA, sumB) / float(pairCount);
}

void main()
{
    vec2 uv = gl_FragCoord.xy / u_viewportSize.xy;
    float ndv = clamp(uv.x, 0.001, 0.999);
    float roughness = clamp(uv.y, 0.001, 0.999);
    gl_FragColor = vec4(integrateBrdf(ndv, roughness), 0.0, 1.0);
}
