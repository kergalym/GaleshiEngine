$input v_worldPos, v_normal, v_tangent, v_uv

#include <bgfx_shader.sh>

uniform vec4 u_albedoFactor;
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
uniform vec4 u_emissiveFactor;
uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
// x = normal strength, y = occlusion strength, z = alpha cutoff
uniform vec4 u_materialParams;
// x = separate metallic map, y = separate roughness map,
// z = specular map, w = opacity map
uniform vec4 u_materialFlags;

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

#define PI 3.14159265359

float distributionGGX(vec3 N, vec3 H, float rough)
{
    float a  = rough * rough;
    float a2 = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d  = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySchlickGGX(float NdV, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return NdV / (NdV * (1.0 - k) + k);
}

float geometrySmith(vec3 N, vec3 V, vec3 L, float rough)
{
    float NdV = max(dot(N, V), 0.0);
    float NdL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdV, rough) * geometrySchlickGGX(NdL, rough);
}

vec3 fresnelSchlick(float cosTheta, vec3 F0)
{
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

vec3 aces_tonemap(vec3 color) {
    // Krzysztof Narkowicz ACES approximation
    return clamp((color * (2.51 * color + 0.03)) / (color * (2.43 * color + 0.59) + 0.14), 0.0, 1.0);
}

void main()
{
    vec4 albedoTex = texture2D(s_albedoMap, v_uv);
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
    metallic = clamp(metallic, 0.0, 1.0);
    rough = clamp(rough, 0.04, 1.0);

    vec3 N = normalize(v_normal);
    vec3 T = v_tangent - N * dot(N, v_tangent);
    float tangentLen = length(T);
    if (tangentLen > 1e-4) {
        T /= tangentLen;
        vec3 B = cross(N, T);
        vec3 tangentNormal = texture2D(s_normalMap, v_uv).xyz * 2.0 - 1.0;
        tangentNormal.xy *= u_materialParams.x;
        N = normalize(mul(mat3(T, B, N), tangentNormal));
    }

    float occlusion = mix(1.0, texture2D(s_occlusionMap, v_uv).r,
                          clamp(u_materialParams.y, 0.0, 1.0));
    float specular = clamp(texture2D(s_specularMap, v_uv).r, 0.0, 1.0);
    vec3 emissive = u_emissiveFactor.rgb * texture2D(s_emissiveMap, v_uv).rgb;

    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos);
    vec3 L = normalize(-u_lightDirection.xyz);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3_splat(0.04 * specular), albedo, metallic);

    float NDF = distributionGGX(N, H, rough);
    float G   = geometrySmith(N, V, L, rough);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specularDirect = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3_splat(1.0) - kS) * (1.0 - metallic);

    float NdL = max(dot(N, L), 0.0);
    vec3 radiance = u_lightColorIntensity.rgb * u_lightColorIntensity.a;

    vec3 direct = (kD * albedo / PI + specularDirect) * radiance * NdL;

    // Simple IBL (safe)
    vec2 envUV = N.xy * 0.5 + 0.5;
    vec3 irradiance = texture2D(s_irradianceMap, envUV).rgb;
    vec3 diffuseIBL = irradiance * albedo;

    vec3 R = reflect(-V, N);
    vec2 specUV = R.xy * 0.5 + 0.5;
    vec3 prefilteredColor = texture2D(s_envMap, specUV).rgb;
    float NdotV = max(dot(N, V), 0.0);
    vec2 brdf = texture2D(s_brdfLUT, vec2(NdotV, rough)).xy;
    vec3 specularIBL = prefilteredColor * (F0 * brdf.x + brdf.y);

    vec3 ambient = (diffuseIBL * (1.0 - metallic) + specularIBL + albedo * 0.05) * occlusion;

    vec3 color = ambient + direct + emissive;

    // Tonemapping and Gamma are handled by the post-process pass (RenderTonemap).
    // Output HDR directly to avoid double gamma.

    gl_FragColor = vec4(color, 1.0);
}
