$input v_worldPos, v_normal, v_tangent, v_uv, v_motion, v_landscapeBlend

#include <bgfx_shader.sh>

uniform vec4 u_albedoFactor;
// x = metallic factor, y = specular strength
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
uniform vec4 u_emissiveFactor;
uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
// x = normal strength, y = occlusion strength, z = alpha cutoff,
// w = two-sided material flag
uniform vec4 u_materialParams;
// x = separate metallic map, y = separate roughness map,
// z = specular map, w = opacity map
uniform vec4 u_materialFlags;
// xyz = plane normal, w = plane constant. The regular forward pass uses
// (0,0,0,1); the planar-reflection pass clips geometry below the waterline.
uniform vec4 u_reflectionClip;

SAMPLER2D(s_albedoMap,            0);
SAMPLER2D(s_metallicRoughnessMap, 1);
SAMPLER2D(s_normalMap,            2);
SAMPLER2D(s_irradianceMap,        4);
SAMPLER2D(s_envMap,               5);
SAMPLER2D(s_brdfLUT,              6);
SAMPLER2D(s_emissiveMap,          7);
SAMPLER2D(s_occlusionMap,         8);
SAMPLER2D(s_opacityMap,           9);
SAMPLER2D(s_metallicMap,         10);
SAMPLER2D(s_roughnessMap,        11);
SAMPLER2D(s_specularMap,         12);
SAMPLER2D(s_landscapeAlbedoMap,  13);
SAMPLER2D(s_landscapeOrmMap,     14);
SAMPLER2D(s_landscapeNormalMap,  15);

uniform vec4 u_landscapeParams;

#define PI 3.14159265359

// UE4 BRDF.ush: GGX / Trowbridge-Reitz with a2 = Pow4(Roughness).
float D_GGX(float a2, float NoH)
{
    float d = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / max(PI * d * d, 0.000001);
}

// UE4 BRDF.ush: height-correlated joint Smith approximation.
float Vis_SmithJointApprox(float a2, float NoV, float NoL)
{
    float a = sqrt(a2);
    float Vis_SmithV = NoL * (NoV * (1.0 - a) + a);
    float Vis_SmithL = NoV * (NoL * (1.0 - a) + a);
    return 0.5 / max(Vis_SmithV + Vis_SmithL, 0.000001);
}

vec3 F_Schlick(vec3 F0, float VoH)
{
    float Fc = pow(1.0 - VoH, 5.0);
    return saturate(50.0 * F0.g) * Fc + (1.0 - Fc) * F0;
}

void main()
{
    if (dot(v_worldPos, u_reflectionClip.xyz) + u_reflectionClip.w < 0.0) {
        discard;
    }

    float landscapeBlend = u_landscapeParams.x > 0.5
        ? clamp(v_landscapeBlend * u_landscapeParams.y, 0.0, 1.0)
        : 0.0;
    vec4 albedoTex = texture2D(s_albedoMap, v_uv);
    if (landscapeBlend > 0.0) {
        vec4 layerAlbedo = texture2D(s_landscapeAlbedoMap, v_uv);
        albedoTex = mix(albedoTex, layerAlbedo, landscapeBlend);
    }
    float opacity = texture2D(s_opacityMap, v_uv).r;
    // Texture alpha only: legacy material factors must not turn a fully
    // opaque opacity map into a discarded surface.
    float alpha = albedoTex.a * opacity;
    float cutoff = u_materialFlags.w > 0.5 ? u_materialParams.z : 0.0;
    if (alpha < cutoff) discard;

    vec3 albedo = u_albedoFactor.rgb * albedoTex.rgb;

    vec4 mr = texture2D(s_metallicRoughnessMap, v_uv);
    float metallic = mr.b * u_metallicFactor.x;
    float rough    = mr.g * u_roughnessFactor.x;
    if (u_materialFlags.x > 0.5) {
        metallic = texture2D(s_metallicMap, v_uv).r * u_metallicFactor.x;
    }
    if (u_materialFlags.y > 0.5) {
        rough = texture2D(s_roughnessMap, v_uv).r * u_roughnessFactor.x;
    }
    if (landscapeBlend > 0.0) {
        vec4 layerOrm = texture2D(s_landscapeOrmMap, v_uv);
        metallic = mix(
            metallic, layerOrm.b * u_metallicFactor.x, landscapeBlend);
        rough = mix(
            rough, layerOrm.g * u_roughnessFactor.x, landscapeBlend);
    }
    metallic = clamp(metallic, 0.0, 1.0);
    rough = clamp(rough, 0.04, 1.0);

    // Two-sided shading: flip the geometric normal and tangent handedness on
    // back-facing fragments so both sides of open meshes are lit correctly.
    // u_materialParams.w is the per-material Two Sided switch.
    bool backFace = u_materialParams.w > 0.5 && !gl_FrontFacing;
    vec3 N = normalize(v_normal);
    if (backFace) N = -N;

    // Screen-space tangent frame: handles mirrored UV islands and meshes
    // without authored tangents (e.g. the armour and sword). The old vertex
    // tangent path assumed B = cross(N, T), which flips normal-map X/Y on
    // mirrored UVs.
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
        if (landscapeBlend > 0.0) {
            vec3 layerNormal =
                texture2D(s_landscapeNormalMap, v_uv).xyz * 2.0 - 1.0;
            tangentNormal = normalize(
                mix(tangentNormal, layerNormal, landscapeBlend));
        }
        tangentNormal.xy *= u_materialParams.x;
        N = normalize(mul(mat3(T, B, N), tangentNormal));
    }

    float occlusionValue = texture2D(s_occlusionMap, v_uv).r;
    if (landscapeBlend > 0.0) {
        float layerOcclusion = texture2D(s_landscapeOrmMap, v_uv).r;
        occlusionValue = mix(
            occlusionValue, layerOcclusion, landscapeBlend);
    }
    float occlusion = mix(1.0, occlusionValue,
                          clamp(u_materialParams.y, 0.0, 1.0));
    float specular = clamp(texture2D(s_specularMap, v_uv).r, 0.0, 1.0) *
                     clamp(u_metallicFactor.y, 0.0, 1.0);
    vec3 emissive = u_emissiveFactor.rgb * texture2D(s_emissiveMap, v_uv).rgb;

    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos);
    vec3 L = normalize(-u_lightDirection.xyz);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3_splat(0.04 * specular), albedo, metallic);

    float a2 = rough * rough;
    a2 *= a2;
    float NoV = saturate(abs(dot(N, V)) + 0.00001);
    float NoL = saturate(dot(N, L));
    vec3 specularDirect = D_GGX(a2, saturate(dot(N, H))) *
        Vis_SmithJointApprox(a2, NoV, NoL) *
        F_Schlick(F0, saturate(dot(H, V)));

    vec3 radiance = u_lightColorIntensity.rgb * u_lightColorIntensity.a;
    vec3 direct = (albedo * (1.0 - metallic) / PI + specularDirect) * radiance * NoL;

    // Simple IBL (safe)
    vec2 envUV = N.xy * 0.5 + 0.5;
    vec3 irradiance = texture2D(s_irradianceMap, envUV).rgb;
    vec3 diffuseIBL = irradiance * albedo * (1.0 - metallic);

    vec3 R = reflect(-V, N);
    vec2 specUV = R.xy * 0.5 + 0.5;
    vec3 prefilteredColor = texture2D(s_envMap, specUV).rgb;
    float NdotV = saturate(abs(dot(N, V)) + 0.00001);
    vec2 brdf = texture2D(s_brdfLUT, vec2(NdotV, rough)).xy;
    vec3 specularIBL = prefilteredColor *
        (F0 * brdf.x + saturate(50.0 * F0.g) * brdf.y);
    float specularOcclusion = saturate(
        pow(NdotV + occlusion, rough * rough) - 1.0 + occlusion);

    vec3 ambient = diffuseIBL * occlusion + specularIBL * specularOcclusion;

    vec3 color = ambient + direct + emissive;

    // Tonemapping is handled by the post-process pass; the sRGB backbuffer
    // performs the final gamma encoding. Output HDR directly.

    gl_FragColor = vec4(color, 1.0);
}
