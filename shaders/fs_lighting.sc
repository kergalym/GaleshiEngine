$input v_texcoord0
#include <bgfx_shader.sh>
#include <bgfx_compute.sh>

SAMPLER2D(s_albedoMr,    0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_worldPos,    2);
SAMPLER2D(s_shadowMap0,  3);
SAMPLER2D(s_shadowMap1,  4);
SAMPLER2D(s_shadowMap2,  5);
SAMPLERCUBE(s_irradianceCube, 6);
SAMPLERCUBE(s_prefilteredCube, 7);
// Slot 8 is reserved for AO. The split-sum BRDF is evaluated with the
// analytic UE4 approximation below, because all 16 Vulkan descriptor slots
// are already consumed by the G-buffer, shadow maps, IBL and tile-light SSBOs.
SAMPLER2D(s_aoTex,          8);
// Slot 9 holds the material RT: emissive.rgb + material AO.
SAMPLER2D(s_materialTex,    9);
SAMPLER2D(s_spotShadowMap0, 10);
SAMPLER2D(s_spotShadowMap1, 11);
SAMPLER2D(s_spotShadowMap2, 12);
SAMPLER2D(s_spotShadowMap3, 13);

uniform vec4 u_cameraPosition;
uniform vec4 u_cascadeSplits;
uniform vec4 u_cascadeNormalOffsets; // xyz = world-space normal offset per cascade
uniform vec4 u_cascadeDepthBias;     // xyz = NDC depth bias per cascade
uniform vec4 u_pssmParams;           // x=slope bias, y=PCF radius, z=normal slope, w=Y flip
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
uniform vec4 u_fogColorDensity; // rgb = fog color, w = density
uniform vec4 u_fogParams;      // x = height falloff, y = base height, z = start distance, w = max distance
uniform vec4 u_fogParams2;     // x = sun scattering, y = anisotropy, z = volumetric, w = steps
uniform mat4 u_lightViewProj0;
uniform mat4 u_lightViewProj1;
uniform mat4 u_lightViewProj2;
uniform mat4 u_skyCustomInvVP;

uniform vec4 u_pointLightPosRadius[64];
uniform vec4 u_pointLightColorInt[64];
uniform vec4 u_pointLightCount;
uniform vec4 u_tileParams;

// Slot 14 is the subsurface material-feature texture.
SAMPLER2D(s_materialFeatures, 14);
// Counts at the start of the buffer, indices at totalTiles + tile*64.
BUFFER_RO(s_tileLights, uint, 15);

uniform vec4 u_spotLightPosRadius[4];
uniform vec4 u_spotLightDirInner[4];
uniform vec4 u_spotLightColorOuter[4];
uniform vec4 u_spotLightCount;
uniform mat4 u_spotLightVP0;
uniform mat4 u_spotLightVP1;
uniform mat4 u_spotLightVP2;
uniform mat4 u_spotLightVP3;

const float PI = 3.14159265359;
vec2 getPoissonDisk(int i) {
    if (i == 0) return vec2(-0.613,  0.617);
    if (i == 1) return vec2(-0.125, -0.049);
    if (i == 2) return vec2(-0.383, -0.569);
    if (i == 3) return vec2( 0.342,  0.378);
    if (i == 4) return vec2( 0.883,  0.279);
    if (i == 5) return vec2( 0.049, -0.884);
    if (i == 6) return vec2(-0.927, -0.227);
    return vec2( 0.633, -0.701);
}



float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / max(PI * d * d, 0.000001);
}
float G_Smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0; float k = (r * r) / 8.0;
    return (NdotV / max(NdotV * (1.0 - k) + k, 0.000001)) * (NdotL / max(NdotL * (1.0 - k) + k, 0.000001));
}
vec3 F_Schlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

vec3 F_SchlickRoughness(float NdotV, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - NdotV, 0.0, 1.0), 5.0);
}

vec3 calcLight(vec3 N, vec3 V, vec3 albedo, float metallic, float rough, float specularStrength, vec3 L, vec3 radiance) {
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.000001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 F0 = mix(vec3(0.04 * specularStrength), albedo, metallic);
    float D = D_GGX(NdotH, rough);
    float G = G_Smith(NdotV, NdotL, rough);
    vec3 F  = F_Schlick(VdotH, F0);
    vec3 specular = (D * G * F) / max(4.0 * NdotV * NdotL, 0.000001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

float calculateShadow(vec4 lightSpacePos, float NdotL, int cascadeIdx) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // Vulkan render targets use a top-left texture origin, while the
    // projected NDC Y axis points up. OpenGL uses a bottom-left origin.
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float cascadeBias = 0.0;
    if (cascadeIdx == 0) cascadeBias = u_cascadeDepthBias.x;
    else if (cascadeIdx == 1) cascadeBias = u_cascadeDepthBias.y;
    else cascadeBias = u_cascadeDepthBias.z;
    float bias = max(cascadeBias + u_pssmParams.x * (1.0 - NdotL), 0.000001);
    float pcfRadius = max(u_pssmParams.y, 0.5);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 2048.0, 1.0 / 2048.0);

    // Rotated Poisson Disk (8 samples)
    float angle = fract(sin(dot(projCoords.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float s = sin(angle);
    float c = cos(angle);
    mat2 rot = mat2(c, -s, s, c);

    for (int i = 0; i < 8; ++i) {
        vec2 offset = mul(rot, getPoissonDisk(i)) * texelSize * pcfRadius;
        float pcfDepth;
        if (cascadeIdx == 0) pcfDepth = texture2D(s_shadowMap0, projCoords.xy + offset).r;
        else if (cascadeIdx == 1) pcfDepth = texture2D(s_shadowMap1, projCoords.xy + offset).r;
        else pcfDepth = texture2D(s_shadowMap2, projCoords.xy + offset).r;
        shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / 8.0;
}

// Distance between the receiving fragment and the nearest shadow-map depth.
// For a thin surface lit from behind this is a useful (and free) thickness
// estimate, so both subsurface and transmission shading can use the shadow
// depth instead of a separate thickness map.
float sampleShadowDepthGap(vec4 lightSpacePos, int cascadeIdx) {
    vec3 projCoords = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001);
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;
    if (projCoords.z < 0.0 || projCoords.z > 1.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 0.0;
    }
    float shadowDepth;
    if (cascadeIdx == 0) shadowDepth = texture2D(s_shadowMap0, projCoords.xy).r;
    else if (cascadeIdx == 1) shadowDepth = texture2D(s_shadowMap1, projCoords.xy).r;
    else shadowDepth = texture2D(s_shadowMap2, projCoords.xy).r;
    return max(projCoords.z - shadowDepth, 0.0);
}

// Cheap single-tap shadow lookup used inside the volumetric fog march. The
// normal-offset PCF path above is intentionally not reused here: 24 steps
// times 8 PCF taps would be far too expensive for a full-screen effect.
float fogSunVisibility(vec3 worldPos) {
    float cameraDistance = length(worldPos - u_cameraPosition.xyz);
    int cascade = 0;
    if (cameraDistance > u_cascadeSplits.y) cascade = 2;
    else if (cameraDistance > u_cascadeSplits.x) cascade = 1;

    vec4 lightSpacePos;
    if (cascade == 0) lightSpacePos = mul(u_lightViewProj0, vec4(worldPos, 1.0));
    else if (cascade == 1) lightSpacePos = mul(u_lightViewProj1, vec4(worldPos, 1.0));
    else lightSpacePos = mul(u_lightViewProj2, vec4(worldPos, 1.0));

    vec3 projCoords = lightSpacePos.xyz / max(lightSpacePos.w, 0.0001);
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;

    if (projCoords.z < 0.0 || projCoords.z > 1.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) {
        return 1.0;
    }

    float bias = 0.0015;
    if (cascade == 0) bias += max(u_cascadeDepthBias.x, 0.0) * 0.5;
    else if (cascade == 1) bias += max(u_cascadeDepthBias.y, 0.0) * 0.5;
    else bias += max(u_cascadeDepthBias.z, 0.0) * 0.5;

    float shadowDepth;
    if (cascade == 0) shadowDepth = texture2D(s_shadowMap0, projCoords.xy).r;
    else if (cascade == 1) shadowDepth = texture2D(s_shadowMap1, projCoords.xy).r;
    else shadowDepth = texture2D(s_shadowMap2, projCoords.xy).r;
    return (projCoords.z - bias > shadowDepth) ? 0.0 : 1.0;
}

float fogDensityAt(vec3 worldPos) {
    float falloff = max(u_fogParams.x, 0.0);
    float height = worldPos.y - u_fogParams.y;
    float densityScale = max(u_fogColorDensity.w, 0.0);
    return densityScale * exp(clamp(-falloff * height, -40.0, 40.0));
}

float fogPhase(float cosTheta) {
    float g = clamp(u_fogParams2.y, -0.95, 0.95);
    float g2 = g * g;
    float denom = max(1.0 + g2 - 2.0 * g * cosTheta, 0.0001);
    return (1.0 - g2) / (4.0 * PI * pow(denom, 1.5));
}

// Integral of densityScale * exp(-falloff * (y - base)) along a ray segment.
float fogOpticalDepth(vec3 rayOrigin, vec3 rayDir, float t0, float t1) {
    if (t1 <= t0) return 0.0;
    float falloff = max(u_fogParams.x, 0.0);
    float baseDensity = max(u_fogColorDensity.w, 0.0) *
        exp(clamp(-falloff * (rayOrigin.y - u_fogParams.y), -40.0, 40.0));
    float dy = rayDir.y;
    if (falloff < 0.00001 || abs(dy) < 0.00001) {
        return baseDensity * (t1 - t0);
    }
    float exponent0 = clamp(-falloff * dy * t0, -40.0, 40.0);
    float exponent1 = clamp(-falloff * dy * t1, -40.0, 40.0);
    return baseDensity * (exp(exponent0) - exp(exponent1)) /
        (falloff * dy);
}

void applyFog(inout vec3 color, vec3 rayDir, float distance, float jitter) {
    if (u_fogColorDensity.w <= 0.0) return;

    float t0 = max(u_fogParams.z, 0.0);
    float t1 = min(distance, max(u_fogParams.w, 0.0));
    if (t1 <= t0) return;

    vec3 sunDir = normalize(-u_lightDirection.xyz);
    float phase = fogPhase(dot(rayDir, sunDir));
    vec3 sunRadiance = u_lightColorIntensity.rgb * u_lightColorIntensity.a;

    if (u_fogParams2.z < 0.5) {
        float opticalDepth = fogOpticalDepth(u_cameraPosition.xyz, rayDir, t0, t1);
        float transmittance = exp(-opticalDepth);
        vec3 litFog = u_fogColorDensity.rgb +
            sunRadiance * phase * u_fogParams2.x * 0.35;
        color = color * transmittance + litFog * (1.0 - transmittance);
        return;
    }

    int steps = int(clamp(u_fogParams2.w, 1.0, 64.0));
    float stepLength = (t1 - t0) / float(steps);
    float transmittance = 1.0;
    vec3 accumulated = vec3(0.0);

    for (int i = 0; i < 64; ++i) {
        if (i >= steps) break;
        float t = t0 + (float(i) + jitter) * stepLength;
        vec3 samplePos = u_cameraPosition.xyz + rayDir * t;
        float density = fogDensityAt(samplePos);
        if (density <= 0.000001) continue;

        float sunVisibility = fogSunVisibility(samplePos);
        vec3 inscatter = u_fogColorDensity.rgb * 0.80 +
            sunRadiance * sunVisibility * phase * u_fogParams2.x;
        accumulated += transmittance * inscatter * density * stepLength;
        transmittance *= exp(-density * stepLength);
    }

    color = color * transmittance + accumulated;
}





float calcSpotShadow0(vec4 lightSpacePos, float NdotL) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // Vulkan render targets use a top-left texture origin, while the
    // projected NDC Y axis points up. OpenGL uses a bottom-left origin.
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - NdotL), 0.00005);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0);
    float pcfDepth;

    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2(-1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2( 0.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2( 1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2(-1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2( 0.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2( 1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2(-1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2( 0.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap0, projCoords.xy + vec2( 1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;

    return shadow / 9.0;
}


float calcSpotShadow1(vec4 lightSpacePos, float NdotL) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // Vulkan render targets use a top-left texture origin, while the
    // projected NDC Y axis points up. OpenGL uses a bottom-left origin.
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - NdotL), 0.00005);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0);
    float pcfDepth;

    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2(-1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2( 0.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2( 1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2(-1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2( 0.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2( 1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2(-1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2( 0.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap1, projCoords.xy + vec2( 1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;

    return shadow / 9.0;
}


float calcSpotShadow2(vec4 lightSpacePos, float NdotL) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // Vulkan render targets use a top-left texture origin, while the
    // projected NDC Y axis points up. OpenGL uses a bottom-left origin.
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - NdotL), 0.00005);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0);
    float pcfDepth;

    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2(-1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2( 0.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2( 1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2(-1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2( 0.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2( 1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2(-1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2( 0.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap2, projCoords.xy + vec2( 1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;

    return shadow / 9.0;
}


float calcSpotShadow3(vec4 lightSpacePos, float NdotL) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;
    // Vulkan render targets use a top-left texture origin, while the
    // projected NDC Y axis points up. OpenGL uses a bottom-left origin.
    if (u_pssmParams.w > 0.5) projCoords.y = 1.0 - projCoords.y;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - NdotL), 0.00005);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0);
    float pcfDepth;

    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2(-1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2( 0.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2( 1.0, -1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2(-1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2( 0.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2( 1.0,  0.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2(-1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2( 0.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    pcfDepth = texture2D(s_spotShadowMap3, projCoords.xy + vec2( 1.0,  1.0) * texelSize).r;
    shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;

    return shadow / 9.0;
}

vec2 EnvBRDFApprox(float NoV, float roughness)
{
    const vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
    const vec4 c1 = vec4( 1.0,  0.0425,  1.04, -0.04);
    vec4 r = roughness * c0 + c1;
    float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y;
    return vec2(-1.04, 1.04) * a004 + r.zw;
}

void main()
{
    vec2 uv = v_texcoord0;
    float fogJitter = fract(sin(dot(v_texcoord0.xy, vec2(12.9898, 78.233))) * 43758.5453);
    vec4 wPos = texture2D(s_worldPos, uv);
    
    if (wPos.w < 0.5) {
        // v_texcoord0.y == 0 at the top of the target; NDC y points up.
        vec2 ndc = vec2(v_texcoord0.x * 2.0 - 1.0, 1.0 - v_texcoord0.y * 2.0);
        vec4 clip = vec4(ndc, 1.0, 1.0);
        vec4 world = mul(u_skyCustomInvVP, clip);
        vec3 dir = normalize(world.xyz / max(abs(world.w), 0.000001) * sign(world.w));

        float up = clamp(dir.y, 0.0, 1.0);
        vec3 skyColor = mix(vec3(0.55, 0.70, 0.95), vec3(0.05, 0.12, 0.32), pow(up, 0.45));
        vec3 ground = vec3(0.035, 0.032, 0.028);
        skyColor = mix(ground, skyColor, smoothstep(-0.06, 0.02, dir.y));

        vec3 sunDir = normalize(-u_lightDirection.xyz);
        float sun = max(dot(dir, sunDir), 0.0);
        skyColor += vec3(1.0, 0.88, 0.72) * pow(sun, 512.0) * 32.0;
        skyColor += vec3(1.0, 0.72, 0.45) * pow(sun, 12.0) * 0.35;

        // Apply the same fog model to the sky. This makes volumetric shafts
        // visible against empty air and gives distant geometry a consistent
        // horizon when atmosphere rendering is disabled.
        if (u_fogColorDensity.w > 0.0) {
            float skyDistance = max(u_fogParams.w, 1.0);
            vec3 foggedSky = skyColor;
            applyFog(foggedSky, dir, skyDistance, fogJitter);
            // Keep the upper sky translucent; fog mainly gathers along the
            // horizon instead of turning the whole sky into a flat wall.
            float horizonFog = 1.0 - smoothstep(0.0, 0.55, dir.y);
            skyColor = mix(skyColor, foggedSky, horizonFog);
        }
        gl_FragColor = vec4(skyColor, 1.0);
        return;
    }

    vec4 aMr  = texture2D(s_albedoMr,    uv);
    vec4 nR   = texture2D(s_normalRough, uv);
    vec4 matData = texture2D(s_materialTex, uv);

    vec3 albedo    = aMr.rgb;
    float metallic = aMr.a;
    vec3 N         = normalize(nR.rgb * 2.0 - 1.0);
    float rough    = max(nR.a, 0.05);
    vec3 V         = normalize(u_cameraPosition.xyz - wPos.xyz);
    // RT2.w carries 1 + specular mask (0 stays reserved for sky pixels).
    float materialSpecular = clamp(wPos.w - 1.0, 0.0, 1.0);

    // RT5 = subsurface color (premultiplied by strength) + normalized radius.
    vec4 sssData = texture2D(s_materialFeatures, uv);
    vec3 sssColor = sssData.rgb;
    float sssRadiusNorm = sssData.a;
    float sssAmount = clamp(max(sssColor.r, max(sssColor.g, sssColor.b)), 0.0, 1.0);

    // Ambient occlusion from the half-res SSAO chain multiplied by the
    // material's occlusion map. White (1.0) when the effect is disabled.
    // Only ambient/IBL light is modulated; direct light keeps using shadow maps.
    float ao = saturate(texture2D(s_aoTex, uv).r) * saturate(matData.a);

    // 1. Ambient (Базовое освещение, чтобы не было абсолютно черных теней)
    // Увеличено с 0.15 до 0.3 для лучшей видимости в тенях
        // Physically correct IBL (Split-Sum)
    vec3 F0 = mix(vec3(0.04 * materialSpecular), albedo, metallic);
    float NdotV = max(dot(N, V), 0.000001);
    
    vec3 F = F_SchlickRoughness(NdotV, F0, rough);
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL
    vec3 irradiance = textureCube(s_irradianceCube, N).rgb;
    vec3 diffuseIBL = irradiance * albedo;
    
    // Specular IBL
    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureCubeLod(s_prefilteredCube, R, rough * 4.0).rgb;
    vec2 brdf = EnvBRDFApprox(NdotV, rough);
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);
    
    vec3 finalColor = (kD * diffuseIBL + specularIBL) * ao + (albedo * 0.05) * ao;

    // 2. Directional Light с тенями
    vec3 L_dir = normalize(-u_lightDirection.xyz);
    vec3 radiance_dir = u_lightColorIntensity.rgb * u_lightColorIntensity.a;
    float NdotL = max(dot(N, L_dir), 0.0);

    float camDist = length(wPos.xyz - u_cameraPosition.xyz);
    int cascadeIdx = 0;
    if (camDist > u_cascadeSplits.y) cascadeIdx = 2;
    else if (camDist > u_cascadeSplits.x) cascadeIdx = 1;

    // Normal offset is expressed in shadow-map texels on the CPU and grows
    // with the cascade footprint, so near cascades do not over-offset small
    // details and far cascades still avoid acne.
    float normalOffset = u_cascadeNormalOffsets.z;
    if (cascadeIdx == 0) normalOffset = u_cascadeNormalOffsets.x;
    else if (cascadeIdx == 1) normalOffset = u_cascadeNormalOffsets.y;
    normalOffset *= 1.0 + u_pssmParams.z * (1.0 - NdotL);

    vec3 shadowWorldPos = wPos.xyz + N * normalOffset;
    vec4 lightSpacePos;
    if (cascadeIdx == 0) lightSpacePos = mul(u_lightViewProj0, vec4(shadowWorldPos, 1.0));
    else if (cascadeIdx == 1) lightSpacePos = mul(u_lightViewProj1, vec4(shadowWorldPos, 1.0));
    else lightSpacePos = mul(u_lightViewProj2, vec4(shadowWorldPos, 1.0));
    float shadowFactor = calculateShadow(lightSpacePos, NdotL, cascadeIdx);
    float visibility = 1.0 - shadowFactor;

    // Standard direct light. SSS materials reduce the geometric diffuse
    // slightly so the wrapped/back-scattered lobe does not double-expose.
    finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L_dir, radiance_dir)
                  * visibility * (1.0 - 0.25 * sssAmount);

    if (sssAmount > 0.0001) {
        // The shadow depth gap is an inexpensive thickness proxy. A larger
        // radius lets light scatter further through the surface.
        float depthGap = sampleShadowDepthGap(lightSpacePos, cascadeIdx);
        float gapScale = mix(150.0, 55.0, sssRadiusNorm);
        float transmissionDepth = exp(-depthGap * gapScale);
        float sssVisibility = max(visibility, transmissionDepth * 0.82);
        float wrapWidth = mix(0.16, 0.72, sssRadiusNorm);
        float wrapped = saturate((dot(N, L_dir) + wrapWidth) / (1.0 + wrapWidth));
        float backScatter = pow(saturate(dot(V, -L_dir)), 2.0) *
                            (0.35 + 0.65 * sssRadiusNorm);
        vec3 scatterTint = sssColor * mix(vec3_splat(0.65), albedo, 0.35);
        finalColor += scatterTint * (wrapped * 0.38 + backScatter * 0.90) *
                      radiance_dir * sssVisibility * (1.0 - metallic) * 0.65;
    }

    // 3. Point Lights (Tiled)
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uvec2 tile = pixel / 16u;
    uint tileIdx = tile.y * uint(u_tileParams.x) + tile.x;
    uint totalTiles = uint(u_tileParams.x) * uint(u_tileParams.y);
    uint count = s_tileLights[tileIdx];

    for (uint i = 0; i < count; ++i) {
        uint lightIdx = s_tileLights[totalTiles + tileIdx * 64u + i];
        vec3 lightPos = u_pointLightPosRadius[lightIdx].xyz;
        float radius = u_pointLightPosRadius[lightIdx].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[lightIdx].rgb * u_pointLightColorInt[lightIdx].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L, radiance);
            if (sssAmount > 0.0001) {
                float wrapWidth = mix(0.16, 0.72, sssRadiusNorm);
                float wrapped = saturate((dot(N, L) + wrapWidth) / (1.0 + wrapWidth));
                float backScatter = pow(saturate(dot(V, -L)), 2.0) *
                                    (0.35 + 0.65 * sssRadiusNorm);
                vec3 scatterTint = sssColor * mix(vec3_splat(0.65), albedo, 0.35);
                finalColor += scatterTint * (wrapped * 0.38 + backScatter * 0.90) *
                              radiance * (1.0 - metallic) * 0.55;
            }
        }
    }

    // 4. Spot Lights (4 separate blocks)
    int sCount = int(u_spotLightCount.x);

    if (sCount > 0) {
        vec3 lightPos = u_spotLightPosRadius[0].xyz;
        float radius = u_spotLightPosRadius[0].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            vec3 spotDir = normalize(u_spotLightDirInner[0].xyz);
            float cos_inner = u_spotLightDirInner[0].w;
            float intensity = u_spotLightColorOuter[0].w;
            vec3 lightColor = u_spotLightColorOuter[0].rgb;
            float spotEffect = dot(normalize(lightPos - wPos.xyz), spotDir);
            float outerCone = cos(radians(90.0));
            float spotFactor = smoothstep(outerCone, cos_inner, spotEffect);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP0, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow0(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L, radiance) * (1.0 - spotShadow);
                if (sssAmount > 0.0001) {
                    float wrapWidth = mix(0.16, 0.72, sssRadiusNorm);
                    float wrapped = saturate((dot(N, L) + wrapWidth) / (1.0 + wrapWidth));
                    float backScatter = pow(saturate(dot(V, -L)), 2.0) *
                                        (0.35 + 0.65 * sssRadiusNorm);
                    vec3 scatterTint = sssColor * mix(vec3_splat(0.65), albedo, 0.35);
                    finalColor += scatterTint * (wrapped * 0.38 + backScatter * 0.90) *
                                  radiance * (1.0 - spotShadow) * (1.0 - metallic) * 0.45;
                }
            }
        }
    }
    if (sCount > 1) {
        vec3 lightPos = u_spotLightPosRadius[1].xyz;
        float radius = u_spotLightPosRadius[1].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            vec3 spotDir = normalize(u_spotLightDirInner[1].xyz);
            float cos_inner = u_spotLightDirInner[1].w;
            float intensity = u_spotLightColorOuter[1].w;
            vec3 lightColor = u_spotLightColorOuter[1].rgb;
            float spotEffect = dot(normalize(lightPos - wPos.xyz), spotDir);
            float outerCone = cos(radians(90.0));
            float spotFactor = smoothstep(outerCone, cos_inner, spotEffect);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP1, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow1(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L, radiance) * (1.0 - spotShadow);
                if (sssAmount > 0.0001) {
                    float wrapWidth = mix(0.16, 0.72, sssRadiusNorm);
                    float wrapped = saturate((dot(N, L) + wrapWidth) / (1.0 + wrapWidth));
                    float backScatter = pow(saturate(dot(V, -L)), 2.0) *
                                        (0.35 + 0.65 * sssRadiusNorm);
                    vec3 scatterTint = sssColor * mix(vec3_splat(0.65), albedo, 0.35);
                    finalColor += scatterTint * (wrapped * 0.38 + backScatter * 0.90) *
                                  radiance * (1.0 - spotShadow) * (1.0 - metallic) * 0.45;
                }
            }
        }
    }
    if (sCount > 2) {
        vec3 lightPos = u_spotLightPosRadius[2].xyz;
        float radius = u_spotLightPosRadius[2].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            vec3 spotDir = normalize(u_spotLightDirInner[2].xyz);
            float cos_inner = u_spotLightDirInner[2].w;
            float intensity = u_spotLightColorOuter[2].w;
            vec3 lightColor = u_spotLightColorOuter[2].rgb;
            float spotEffect = dot(normalize(lightPos - wPos.xyz), spotDir);
            float outerCone = cos(radians(90.0));
            float spotFactor = smoothstep(outerCone, cos_inner, spotEffect);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP2, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow2(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L, radiance) * (1.0 - spotShadow);
                if (sssAmount > 0.0001) {
                    float wrapWidth = mix(0.16, 0.72, sssRadiusNorm);
                    float wrapped = saturate((dot(N, L) + wrapWidth) / (1.0 + wrapWidth));
                    float backScatter = pow(saturate(dot(V, -L)), 2.0) *
                                        (0.35 + 0.65 * sssRadiusNorm);
                    vec3 scatterTint = sssColor * mix(vec3_splat(0.65), albedo, 0.35);
                    finalColor += scatterTint * (wrapped * 0.38 + backScatter * 0.90) *
                                  radiance * (1.0 - spotShadow) * (1.0 - metallic) * 0.45;
                }
            }
        }
    }
    if (sCount > 3) {
        vec3 lightPos = u_spotLightPosRadius[3].xyz;
        float radius = u_spotLightPosRadius[3].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            vec3 spotDir = normalize(u_spotLightDirInner[3].xyz);
            float cos_inner = u_spotLightDirInner[3].w;
            float intensity = u_spotLightColorOuter[3].w;
            vec3 lightColor = u_spotLightColorOuter[3].rgb;
            float spotEffect = dot(normalize(lightPos - wPos.xyz), spotDir);
            float outerCone = cos(radians(90.0));
            float spotFactor = smoothstep(outerCone, cos_inner, spotEffect);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP3, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow3(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L, radiance) * (1.0 - spotShadow);
                if (sssAmount > 0.0001) {
                    float wrapWidth = mix(0.16, 0.72, sssRadiusNorm);
                    float wrapped = saturate((dot(N, L) + wrapWidth) / (1.0 + wrapWidth));
                    float backScatter = pow(saturate(dot(V, -L)), 2.0) *
                                        (0.35 + 0.65 * sssRadiusNorm);
                    vec3 scatterTint = sssColor * mix(vec3_splat(0.65), albedo, 0.35);
                    finalColor += scatterTint * (wrapped * 0.38 + backScatter * 0.90) *
                                  radiance * (1.0 - spotShadow) * (1.0 - metallic) * 0.45;
                }
            }
        }
    }

    // Emissive comes from the material G-buffer RT (already scaled by the
    // emissive factor and emissive map in fs_gbuffer).
    finalColor += matData.rgb;

    float fogDistance = length(wPos.xyz - u_cameraPosition.xyz);
    vec3 fogRayDir = (wPos.xyz - u_cameraPosition.xyz) /
        max(fogDistance, 0.0001);
    applyFog(finalColor, fogRayDir, fogDistance, fogJitter);

    // Tonemapping moved to post-process

    gl_FragColor = vec4(finalColor, 1.0);
}
