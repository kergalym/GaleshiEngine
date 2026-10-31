$input v_worldPos, v_normal, v_tangent, v_uv, v_clip
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMap,            0);
SAMPLER2D(s_metallicRoughnessMap, 1);
SAMPLER2D(s_normalMap,            2);
SAMPLER2D(s_translucentShadow0,   4);
SAMPLER2D(s_translucentShadow1,   5);
SAMPLER2D(s_translucentShadow2,   6);
SAMPLERCUBE(s_translucentEnvCube, 7);
SAMPLER2D(s_translucentDepth,     8);
SAMPLER2D(s_opacityMap,           9);
SAMPLER2D(s_emissiveMap,         10);
SAMPLER2D(s_occlusionMap,        11);
SAMPLER2D(s_specularMap,         12);
SAMPLER2D(s_metallicMap,         13);
SAMPLER2D(s_roughnessMap,        14);

uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
uniform vec4 u_cascadeSplits;
uniform vec4 u_pssmParams;
uniform mat4 u_lightViewProj0;
uniform mat4 u_lightViewProj1;
uniform mat4 u_lightViewProj2;

uniform vec4 u_albedoFactor;
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
uniform vec4 u_emissiveFactor;
// x = normal strength, y = occlusion strength, z = alpha cutoff,
// w = two-sided material flag
uniform vec4 u_materialParams;
// x = separate metallic map, y = separate roughness map,
// z = specular map, w = opacity map
uniform vec4 u_materialFlags;

uniform vec4 u_translucencyParams; // x=opacity, y=strength, z=thickness, w=distortion
uniform vec4 u_translucencyColor;  // rgb = transmitted tint
uniform vec4 u_translucencyShadow; // x = shadow-depth absorption scale

uniform vec4 u_pointLightPosRadius[4];
uniform vec4 u_pointLightColorInt[4];
uniform vec4 u_pointLightCount;

#define PI 3.14159265359

float distributionGGX(vec3 N, vec3 H, float rough)
{
    float a  = rough * rough;
    float a2 = a * a;
    float NdH = max(dot(N, H), 0.0);
    float d  = NdH * NdH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.000001);
}

float geometrySchlickGGX(float NdV, float rough)
{
    float r = rough + 1.0;
    float k = (r * r) / 8.0;
    return NdV / max(NdV * (1.0 - k) + k, 0.000001);
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

vec3 calcLight(vec3 N, vec3 V, vec3 albedo, float metallic, float rough,
               float specularStrength, vec3 L, vec3 radiance)
{
    vec3 H = normalize(L + V);
    float NdL = max(dot(N, L), 0.0);
    float NdV = max(dot(N, V), 0.000001);
    if (NdL <= 0.0) return vec3(0.0);
    vec3 F0 = mix(vec3_splat(0.04 * specularStrength), albedo, metallic);
    float D = distributionGGX(N, H, rough);
    float G = geometrySmith(N, V, L, rough);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 spec = (D * G * F) / max(4.0 * NdV * NdL, 0.000001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    return (kD * albedo / PI + spec) * radiance * NdL;
}

float shadowDepthAt(vec2 uv, int cascade)
{
    if (cascade == 0) return texture2D(s_translucentShadow0, uv).r;
    if (cascade == 1) return texture2D(s_translucentShadow1, uv).r;
    return texture2D(s_translucentShadow2, uv).r;
}

// Returns x = PCF visibility, y = depth gap behind the nearest caster.
vec2 sampleShadow(vec4 lightSpacePos, int cascade, float bias)
{
    vec3 proj = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001);
    proj.xy = proj.xy * 0.5 + 0.5;
    if (u_pssmParams.w > 0.5) proj.y = 1.0 - proj.y;
    if (proj.z < 0.0 || proj.z > 1.0 ||
        proj.x < 0.0 || proj.x > 1.0 ||
        proj.y < 0.0 || proj.y > 1.0) {
        return vec2(1.0, 0.0);
    }

    vec2 texel = vec2(1.0 / 2048.0, 1.0 / 2048.0);
    float radius = max(u_pssmParams.y, 0.5);
    float occ = 0.0;
    occ += proj.z - bias > shadowDepthAt(proj.xy, cascade) ? 1.0 : 0.0;
    occ += proj.z - bias > shadowDepthAt(proj.xy + vec2( radius, 0.0) * texel, cascade) ? 1.0 : 0.0;
    occ += proj.z - bias > shadowDepthAt(proj.xy + vec2(-radius, 0.0) * texel, cascade) ? 1.0 : 0.0;
    occ += proj.z - bias > shadowDepthAt(proj.xy + vec2(0.0,  radius) * texel, cascade) ? 1.0 : 0.0;
    occ += proj.z - bias > shadowDepthAt(proj.xy + vec2(0.0, -radius) * texel, cascade) ? 1.0 : 0.0;
    float visibility = 1.0 - occ / 5.0;
    float gap = max(proj.z - shadowDepthAt(proj.xy, cascade), 0.0);
    return vec2(visibility, gap);
}

void main()
{
    // The HDR target is color-only in the transparent pass, so compare
    // against the copied opaque depth manually.
    vec2 screenUV = v_clip.xy / max(v_clip.w, 0.0001) * 0.5 + 0.5;
    float sceneDepth = texture2D(s_translucentDepth, screenUV).r;
    float fragmentDepth = v_clip.z / max(v_clip.w, 0.0001);
    if (fragmentDepth > sceneDepth + 0.0008) discard;

    vec4 albedoTex = texture2D(s_albedoMap, v_uv);
    float opacityTex = texture2D(s_opacityMap, v_uv).r;
    float alpha = u_translucencyParams.x * u_albedoFactor.a *
                  albedoTex.a * opacityTex;
    if (alpha < 0.001) discard;

    vec3 albedo = albedoTex.rgb * u_albedoFactor.rgb;

    vec4 mr = texture2D(s_metallicRoughnessMap, v_uv);
    float metallic = mr.b * u_metallicFactor.x;
    float rough = mr.g * u_roughnessFactor.x;
    if (u_materialFlags.x > 0.5) {
        metallic = texture2D(s_metallicMap, v_uv).r * u_metallicFactor.x;
    }
    if (u_materialFlags.y > 0.5) {
        rough = texture2D(s_roughnessMap, v_uv).r * u_roughnessFactor.x;
    }
    metallic = clamp(metallic, 0.0, 1.0);
    rough = clamp(rough, 0.04, 1.0);

    bool backFace = u_materialParams.w > 0.5 && !gl_FrontFacing;
    vec3 N = normalize(v_normal);
    if (backFace) N = -N;
    vec3 T = v_tangent - N * dot(N, v_tangent);
    if (length(T) > 1e-4) {
        T = normalize(T);
        if (backFace) T = -T;
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
    vec3 radiance = u_lightColorIntensity.rgb * max(u_lightColorIntensity.a, 0.0);

    float camDist = length(v_worldPos - u_cameraPosition.xyz);
    int cascade = 0;
    if (camDist > u_cascadeSplits.y) cascade = 2;
    else if (camDist > u_cascadeSplits.x) cascade = 1;

    vec4 lightSpacePos;
    if (cascade == 0) lightSpacePos = mul(u_lightViewProj0, vec4(v_worldPos, 1.0));
    else if (cascade == 1) lightSpacePos = mul(u_lightViewProj1, vec4(v_worldPos, 1.0));
    else lightSpacePos = mul(u_lightViewProj2, vec4(v_worldPos, 1.0));

    // PSSM slope bias, converted to the same NDC depth scale as the deferred
    // lighting path. The slight increase avoids self-shadow acne on cards.
    float NdotL = max(dot(N, L), 0.0);
    float bias = max(0.00035 * (1.0 - NdotL), 0.00004);
    vec2 shadowData = sampleShadow(lightSpacePos, cascade, bias);
    float visibility = shadowData.x;
    float shadowDepthGap = shadowData.y;

    vec3 color = vec3(0.0);
    color += calcLight(N, V, albedo, metallic, rough, specular, L, radiance)
             * visibility;

    // Backlight transmission. The depth gap behind the nearest shadow caster
    // is used as an inexpensive thickness estimate: thin geometry stays
    // bright when lit from behind, while an opaque object between the light
    // and the surface absorbs the transmitted lobe.
    float distortion = clamp(u_translucencyParams.w, 0.0, 1.0);
    float backExp = mix(8.0, 1.0, distortion);
    float back = pow(clamp(dot(V, -L), 0.0, 1.0), backExp);
    float thickness = clamp(u_translucencyParams.z, 0.02, 1.0);
    float depthAtten = exp(-shadowDepthGap * 180.0 * thickness);
    float transVisibility = mix(1.0, depthAtten, clamp(u_translucencyShadow.x, 0.0, 1.0));
    vec3 transmitted = u_translucencyColor.rgb * albedo *
        (u_translucencyParams.y * back * transVisibility);
    color += transmitted * radiance;

    // Point lights use the same transmission lobe without a shadow-depth
    // lookup. They are cheap and make candles/lanterns read through cloth.
    int pointCount = int(u_pointLightCount.x);
    for (int i = 0; i < 4; ++i) {
        if (i >= pointCount) break;
        vec3 lightPos = u_pointLightPosRadius[i].xyz;
        float radius = u_pointLightPosRadius[i].w;
        vec3 toLight = lightPos - v_worldPos;
        float dist = length(toLight);
        if (dist >= radius) continue;
        vec3 Lp = toLight / max(dist, 0.0001);
        float atten = 1.0 / (dist * dist + 1.0);
        float window = saturate(1.0 - pow(dist / radius, 4.0));
        atten *= window * window;
        vec3 lpRadiance = u_pointLightColorInt[i].rgb *
                          u_pointLightColorInt[i].a * atten;
        color += calcLight(N, V, albedo, metallic, rough, specular, Lp, lpRadiance);
        float backP = pow(clamp(dot(V, -Lp), 0.0, 1.0), backExp);
        color += u_translucencyColor.rgb * albedo *
                 (u_translucencyParams.y * backP) * lpRadiance;
    }

    // Small ambient/reflection term keeps the translucent surface readable
    // in shadow without requiring another irradiance binding.
    vec3 envN = textureCube(s_translucentEnvCube, N).rgb;
    vec3 R = reflect(-V, N);
    vec3 envR = textureCube(s_translucentEnvCube, R).rgb;
    color += (envN * 0.22 + envR * 0.10) * albedo * occlusion;
    color += emissive;

    gl_FragColor = vec4(color, clamp(alpha, 0.0, 1.0));
}
