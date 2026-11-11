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
// Slot 8 is reserved for AO. The split-sum BRDF uses the analytic
// approximation below because all 16 Vulkan descriptor slots are consumed.
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
// x = PCSS enabled, y = light source size (world units),
// z = blocker search radius (texels), w = max penumbra (texels).
uniform vec4 u_pcssParams;
// x = enabled, y = ray length (world units), z = depth thickness.
uniform vec4 u_contactShadowParams;
uniform mat4 u_cameraViewProj;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
uniform vec4 u_lightDirection2;
uniform vec4 u_lightColorIntensity2;
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
// Point shadow faces as a 2D array, one VP per face in +X, -X, +Y, -Y, +Z, -Z order.
SAMPLER2DARRAY(s_pointShadowMap, 16);
uniform vec4 u_pointShadowParams; // x = enabled, y = far plane
uniform mat4 u_pointShadowVP[6];

uniform vec4 u_spotLightPosRadius[4];
uniform vec4 u_spotLightDirInner[4];
uniform vec4 u_spotLightColorOuter[4];
// x = cos(outer cone), y = 1 / (cos(inner cone) - cos(outer cone)).
uniform vec4 u_spotLightCone[4];
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



// UE4 BRDF.ush: GGX / Trowbridge-Reitz with a2 = Pow4(Roughness).
float D_GGX(float a2, float NdotH) {
    float d = (NdotH * a2 - NdotH) * NdotH + 1.0;
    return a2 / max(PI * d * d, 0.000001);
}

// UE4 BRDF.ush: height-correlated joint Smith approximation.
float Vis_SmithJointApprox(float a2, float NdotV, float NdotL) {
    float a = sqrt(a2);
    float Vis_SmithV = NdotL * (NdotV * (1.0 - a) + a);
    float Vis_SmithL = NdotV * (NdotL * (1.0 - a) + a);
    return 0.5 / max(Vis_SmithV + Vis_SmithL, 0.000001);
}

// UE4 BRDF.ush: F90 is derived from F0 so dielectrics keep the physically
// plausible 2% reflectance floor at grazing angles.
vec3 F_Schlick(vec3 F0, float VdotH) {
    float Fc = pow(1.0 - VdotH, 5.0);
    return saturate(50.0 * F0.g) * Fc + (1.0 - Fc) * F0;
}

vec3 calcLight(vec3 N, vec3 V, vec3 albedo, float metallic, float rough, float specularStrength, float clearcoat, vec3 L, vec3 radiance) {
    vec3 H = normalize(L + V);
    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(abs(dot(N, V)) + 0.00001);
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 F0 = mix(vec3(0.04 * specularStrength), albedo, metallic);
    float a2 = rough * rough;
    a2 *= a2;
    float D = D_GGX(a2, NdotH);
    float Vis = Vis_SmithJointApprox(a2, NdotV, NdotL);
    vec3 F = F_Schlick(F0, VdotH);
    // UE4 DefaultLitBxDF: Lambert diffuse is not multiplied by (1-F).
    vec3 diffuse = albedo * (1.0 - metallic) / PI;
    vec3 specular = D * Vis * F;
    vec3 result = (diffuse + specular) * radiance * NdotL;

    // UE4 clearcoat: a tight glossy layer on top. Roughness is fixed at
    // 0.03 (the common authoring choice) and F0 is the dielectric 4%.
    if (clearcoat > 0.001) {
        float ccA2 = 0.03 * 0.03;
        ccA2 *= ccA2;
        float ccF = F_Schlick(vec3_splat(0.04), VdotH).x;
        result += 0.25 * clearcoat *
                  D_GGX(ccA2, NdotH) *
                  Vis_SmithJointApprox(ccA2, NdotV, NdotL) *
                  ccF * radiance * NdotL;
    }
    return result;
}

// UE4 DynamicLightingCommon.ush: squared saturate mask. L points from the
// surface to the light, so -SpotDirection is the light's forward axis.
float SpotAttenuation(float3 L, float3 SpotDirection, vec2 SpotAngles) {
    float mask = saturate((dot(L, -SpotDirection) - SpotAngles.x) * SpotAngles.y);
    return mask * mask;
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
    float pcfRadius = max(u_pssmParams.y, 0.0);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 2048.0, 1.0 / 2048.0);

    // PCSS: the average blocker depth drives a variable penumbra, so
    // shadows are hard at contact and soften with the gap behind the
    // occluder (UE-style). The 2000 factor maps NDC depth gaps to texels
    // for a typical cascade footprint.
    if (u_pcssParams.x > 0.5) {
        float avgBlocker = 0.0;
        int blockers = 0;
        for (int i = 0; i < 8; ++i) {
            vec2 off = getPoissonDisk(i) * texelSize * u_pcssParams.z;
            float d;
            if (cascadeIdx == 0) d = texture2D(s_shadowMap0, projCoords.xy + off).r;
            else if (cascadeIdx == 1) d = texture2D(s_shadowMap1, projCoords.xy + off).r;
            else d = texture2D(s_shadowMap2, projCoords.xy + off).r;
            if (currentDepth - bias > d) { avgBlocker += d; blockers++; }
        }
        if (blockers == 0) return 0.0;
        if (blockers == 8) return 1.0;
        avgBlocker /= float(blockers);
        float penumbra = (currentDepth - avgBlocker) *
                         u_pcssParams.y * 2000.0;
        pcfRadius = clamp(penumbra, 1.0, u_pcssParams.w);
    }

    // Radius 0 means a genuinely hard shadow: one center tap instead of
    // eight coincident PCF taps.
    if (pcfRadius <= 0.001) {
        float centerDepth;
        if (cascadeIdx == 0) centerDepth = texture2D(s_shadowMap0, projCoords.xy).r;
        else if (cascadeIdx == 1) centerDepth = texture2D(s_shadowMap1, projCoords.xy).r;
        else centerDepth = texture2D(s_shadowMap2, projCoords.xy).r;
        return (currentDepth - bias > centerDepth) ? 1.0 : 0.0;
    }

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

// Screen-space contact shadows: march the lit HDR position texture toward
// the sun and occlude when a sample is closer to the light than the surface.
// One sample per step, short distances only (UE contact-shadow analogue).
float contactShadowOcclusion(vec3 worldPos, vec3 lightDir) {
    float maxDist = u_contactShadowParams.y;
    if (maxDist <= 0.0) return 1.0;
    const int steps = 8;
    float thickness = max(u_contactShadowParams.z, 0.0001);

    vec4 c0 = mul(u_cameraViewProj, vec4(worldPos, 1.0));
    vec2 uv0 = c0.xy / max(c0.w, 0.00001) * 0.5 + 0.5;
    if (u_pssmParams.w > 0.5) uv0.y = 1.0 - uv0.y;

    vec4 c1 = mul(u_cameraViewProj, vec4(worldPos + lightDir * maxDist, 1.0));
    vec2 uv1 = c1.xy / max(c1.w, 0.00001) * 0.5 + 0.5;
    if (u_pssmParams.w > 0.5) uv1.y = 1.0 - uv1.y;

    vec2 uvStep = (uv1 - uv0) / float(steps);
    float surfaceDepth = dot(worldPos - u_cameraPosition.xyz, lightDir);
    float occluded = 0.0;
    for (int i = 1; i <= steps; ++i) {
        vec2 suv = uv0 + uvStep * float(i);
        if (suv.x < 0.0 || suv.x > 1.0 ||
            suv.y < 0.0 || suv.y > 1.0) continue;
        vec4 s = texture2D(s_worldPos, suv);
        if (s.w < 0.5) continue;  // sky
        float sDepth = dot(s.xyz - u_cameraPosition.xyz, lightDir);
        if (sDepth < surfaceDepth - thickness) occluded += 1.0;
    }
    return 1.0 - occluded / float(steps);
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





// Shadow for the first point light (u_pointLightPosRadius[0]). Each face is a
// 2D layer rendered with its own view-projection, so this reuses the spot UV path.
float calcPointShadow(vec3 wPos, vec3 N) {
    vec3 lightPos = u_pointLightPosRadius[0].xyz;
    vec3 p = wPos + N * 0.05;
    vec3 d = p - lightPos;
    vec3 a = abs(d);
    int face;
    if (a.x >= a.y && a.x >= a.z) face = d.x >= 0.0 ? 0 : 1;
    else if (a.y >= a.z) face = d.y >= 0.0 ? 2 : 3;
    else face = d.z >= 0.0 ? 4 : 5;

    float dist = length(d);
    if (dist >= u_pointShadowParams.y) return 0.0;

    vec4 clip = mul(u_pointShadowVP[face], vec4(p, 1.0));
    vec2 uv = clip.xy / clip.w * 0.5 + 0.5;
    if (u_pssmParams.w > 0.5) uv.y = 1.0 - uv.y;
    float stored = texture2DArray(s_pointShadowMap, vec3(uv, float(face))).r * u_pointShadowParams.y;
    // shortcut: fixed 5 cm bias, nearest texel, no PCF; upgrade if acne or jagged edges show.
    return dist - 0.05 > stored ? 1.0 : 0.0;
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
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0) *
                     clamp(u_pssmParams.y, 0.0, 1.0);
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
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0) *
                     clamp(u_pssmParams.y, 0.0, 1.0);
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
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0) *
                     clamp(u_pssmParams.y, 0.0, 1.0);
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
    vec2 texelSize = vec2(1.0 / 1024.0, 1.0 / 1024.0) *
                     clamp(u_pssmParams.y, 0.0, 1.0);
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

vec2 envBrdfApprox(float ndv, float roughness)
{
    const vec4 bias = vec4(1.0, 0.0425, 1.04, -0.04);
    const vec4 slope = vec4(-1.0, -0.0275, -0.572, 0.022);
    vec4 fit = bias + roughness * slope;
    float q = fit.y + fit.x * min(fit.x * fit.x, exp2(-9.28 * ndv));
    return vec2(fit.w, fit.z) + vec2(-1.04, 1.04) * q;
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
    // Materials without SSS pack clearcoat strength into the blue channel
    // instead (a zero radius marks the pack).
    vec4 sssData = texture2D(s_materialFeatures, uv);
    vec3 sssColor = sssData.rgb;
    float sssRadiusNorm = sssData.a;
    float clearcoat = sssData.a > 0.001 ? 0.0 : sssData.b;
    float sssAmount = clamp(max(sssColor.r, max(sssColor.g, sssColor.b)), 0.0, 1.0);

    // Ambient occlusion from the half-res SSAO chain multiplied by the
    // material's occlusion map. White (1.0) when the effect is disabled.
    // Only ambient/IBL light is modulated; direct light keeps using shadow maps.
    float ao = saturate(texture2D(s_aoTex, uv).r) * saturate(matData.a);

    // Physically correct IBL (split-sum). fs_irradiance_conv stores
    // cosine-weighted average radiance to match UE4's SH irradiance
    // convention, so the Lambert 1/PI is already folded into the lookup.
    vec3 F0 = mix(vec3(0.04 * materialSpecular), albedo, metallic);
    float NdotV = saturate(abs(dot(N, V)) + 0.00001);

    vec3 irradiance = textureCube(s_irradianceCube, N).rgb;
    vec3 diffuseIBL = irradiance * albedo * (1.0 - metallic);

    vec3 R = reflect(-V, N);
    vec3 prefilteredColor = textureCubeLod(s_prefilteredCube, R, rough * 4.0).rgb;
    vec2 brdf = envBrdfApprox(NdotV, rough);
    // UE4 EnvBRDF: the second split-sum term uses F90, not a constant 1.
    vec3 specularIBL = prefilteredColor *
        (F0 * brdf.x + saturate(50.0 * F0.g) * brdf.y);

    // Clearcoat also picks up environment reflections (tight rough lobe).
    if (clearcoat > 0.001) {
        vec3 ccPrefiltered = textureCubeLod(s_prefilteredCube, R, 1.0).rgb;
        vec2 ccBrdf = envBrdfApprox(NdotV, 0.03);
        specularIBL += ccPrefiltered * (0.04 * ccBrdf.x + ccBrdf.y) *
                       clearcoat * 0.25;
    }

    // UE4 GetSpecularOcclusion: reflections are less occluded than diffuse
    // as roughness and NdotV increase.
    float specularOcclusion = saturate(
        pow(NdotV + ao, rough * rough) - 1.0 + ao);

    vec3 finalColor = diffuseIBL * ao + specularIBL * specularOcclusion;

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
    // Contact shadows refine the sun shadow with a short screen-space march.
    if (u_contactShadowParams.x > 0.5 && visibility > 0.01) {
        visibility *= contactShadowOcclusion(wPos.xyz, L_dir);
    }

    // Standard direct light. SSS materials reduce the geometric diffuse
    // slightly so the wrapped/back-scattered lobe does not double-expose.
    finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, clearcoat, L_dir, radiance_dir)
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

    // Secondary directional fill. The primary light owns the PSSM
    // cascades, so the fill stays unshadowed.
    if (u_lightColorIntensity2.a > 0.0) {
        vec3 L_fill = normalize(-u_lightDirection2.xyz);
        vec3 radiance_fill =
            u_lightColorIntensity2.rgb * u_lightColorIntensity2.a;
        finalColor += calcLight(N, V, albedo, metallic, rough,
                                materialSpecular, clearcoat,
                                L_fill, radiance_fill);
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
            float pointShadow = (lightIdx == 0u && u_pointShadowParams.x > 0.5) ? calcPointShadow(wPos.xyz, N) : 0.0;
            vec3 radiance = u_pointLightColorInt[lightIdx].rgb * u_pointLightColorInt[lightIdx].a * atten * (1.0 - pointShadow);
            finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, clearcoat, L, radiance);
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
            float intensity = u_spotLightColorOuter[0].w;
            vec3 lightColor = u_spotLightColorOuter[0].rgb;
            float spotFactor = SpotAttenuation(L, spotDir, u_spotLightCone[0].xy);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP0, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow0(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, clearcoat, L, radiance) * (1.0 - spotShadow);
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
            float intensity = u_spotLightColorOuter[1].w;
            vec3 lightColor = u_spotLightColorOuter[1].rgb;
            float spotFactor = SpotAttenuation(L, spotDir, u_spotLightCone[1].xy);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP1, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow1(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, clearcoat, L, radiance) * (1.0 - spotShadow);
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
            float intensity = u_spotLightColorOuter[2].w;
            vec3 lightColor = u_spotLightColorOuter[2].rgb;
            float spotFactor = SpotAttenuation(L, spotDir, u_spotLightCone[2].xy);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP2, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow2(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, clearcoat, L, radiance) * (1.0 - spotShadow);
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
            float intensity = u_spotLightColorOuter[3].w;
            vec3 lightColor = u_spotLightColorOuter[3].rgb;
            float spotFactor = SpotAttenuation(L, spotDir, u_spotLightCone[3].xy);
            if (spotFactor > 0.0) {
                float atten = 1.0 / (dist * dist + 1.0);
                float window = saturate(1.0 - pow(dist / radius, 4.0));
                atten *= window * window;
                vec3 radiance = lightColor * intensity * atten * spotFactor;
                vec4 spotLightSpace = mul(u_spotLightVP3, vec4(wPos.xyz + N * 0.1, 1.0));
                float spotShadow = calcSpotShadow3(spotLightSpace, max(dot(N, L), 0.0));
                finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, clearcoat, L, radiance) * (1.0 - spotShadow);
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
