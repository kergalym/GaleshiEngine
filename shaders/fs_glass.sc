$input v_worldPos, v_normal, v_tangent, v_uv, v_motion, v_landscapeBlend
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMap,  0);
SAMPLER2D(s_normalMap,  2);
SAMPLER2D(s_opacityMap, 9);

uniform vec4 u_cameraPosition;
uniform vec4 u_albedoFactor;
// x = metallic factor, y = specular strength
uniform vec4 u_metallicFactor;
// x = glass opacity, y = normal strength, z = roughness,
// w = two-sided material flag
uniform vec4 u_glassParams;

void main()
{
    // Two-sided shading for transparent surfaces viewed from either side.
    bool backFace = u_glassParams.w > 0.5 && !gl_FrontFacing;
    vec3 N = normalize(v_normal);
    if (backFace) N = -N;

    // Screen-space tangent frame: handles mirrored UV islands and meshes
    // without authored tangents. The old vertex-tangent path assumed
    // B = cross(N, T), which flips normal-map X/Y on mirrored UVs.
    vec3 dp1 = dFdx(v_worldPos);
    vec3 dp2 = dFdy(v_worldPos);
    vec2 duv1 = dFdx(v_uv);
    vec2 duv2 = dFdy(v_uv);
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;
    float maxLen = max(dot(T, T), dot(B, B));
    if (maxLen > 1e-12) {
        float invmax = inversesqrt(maxLen);
        T *= invmax;
        B *= invmax;
        vec3 tangentNormal = texture2D(s_normalMap, v_uv).xyz * 2.0 - 1.0;
        tangentNormal.xy *= u_glassParams.y;
        N = normalize(mul(mat3(T, B, N), tangentNormal));
    }

    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos.xyz);
    float NdotV = max(dot(N, V), 0.0);
    float fresnel = pow(1.0 - NdotV, 5.0) *
                    clamp(u_metallicFactor.y, 0.0, 1.0);

    vec4 base = texture2D(s_albedoMap, v_uv);
    float opacityTex = texture2D(s_opacityMap, v_uv).r;
    vec3 tint = base.rgb * u_albedoFactor.rgb;

    // Cheap environment reflection: tint the reflected ray with the sky
    // gradient. This keeps glass readable without an extra cubemap binding.
    vec3 R = reflect(-V, N);
    vec3 sky = mix(vec3(0.035, 0.032, 0.028), vec3(0.55, 0.70, 0.95),
                   clamp(R.y * 0.5 + 0.5, 0.0, 1.0));

    vec3 color = mix(tint, sky, fresnel);
    float alpha = clamp(u_glassParams.x * u_albedoFactor.a * base.a * opacityTex
                        + fresnel * 0.35, 0.0, 1.0);

    gl_FragColor = vec4(color, alpha);
}
