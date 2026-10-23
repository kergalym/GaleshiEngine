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
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
uniform mat4 u_lightViewProj0;
uniform mat4 u_lightViewProj1;
uniform mat4 u_lightViewProj2;
uniform mat4 u_skyCustomInvVP;

uniform vec4 u_pointLightPosRadius[64];
uniform vec4 u_pointLightColorInt[64];
uniform vec4 u_pointLightCount;
uniform vec4 u_tileParams;

BUFFER_RO(s_tileLightCounts, uint, 14);
BUFFER_RO(s_tileLightIndices, uint, 15);

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

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.0005 * (1.0 - NdotL), 0.0001);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 2048.0, 1.0 / 2048.0);

    // Rotated Poisson Disk (8 samples)
    float angle = fract(sin(dot(projCoords.xy, vec2(12.9898, 78.233))) * 43758.5453) * 6.2831853;
    float s = sin(angle);
    float c = cos(angle);
    mat2 rot = mat2(c, -s, s, c);

    for (int i = 0; i < 8; ++i) {
        vec2 offset = mul(rot, getPoissonDisk(i)) * texelSize * 1.5;
        float pcfDepth;
        if (cascadeIdx == 0) pcfDepth = texture2D(s_shadowMap0, projCoords.xy + offset).r;
        else if (cascadeIdx == 1) pcfDepth = texture2D(s_shadowMap1, projCoords.xy + offset).r;
        else pcfDepth = texture2D(s_shadowMap2, projCoords.xy + offset).r;
        shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
    }
    return shadow / 8.0;
}





float calcSpotShadow0(vec4 lightSpacePos, float NdotL) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

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

    // Normal Offset: сдвигаем позицию пикселя по нормали, чтобы избежать acne
    vec4 lightSpacePos;
    if (cascadeIdx == 0) lightSpacePos = mul(u_lightViewProj0, vec4(wPos.xyz + N * 0.1, 1.0));
    else if (cascadeIdx == 1) lightSpacePos = mul(u_lightViewProj1, vec4(wPos.xyz + N * 0.1, 1.0));
    else lightSpacePos = mul(u_lightViewProj2, vec4(wPos.xyz + N * 0.1, 1.0));
    float shadowFactor = calculateShadow(lightSpacePos, NdotL, cascadeIdx);
    float visibility = 1.0 - shadowFactor;

    finalColor += calcLight(N, V, albedo, metallic, rough, materialSpecular, L_dir, radiance_dir) * visibility;

    // 3. Point Lights (Tiled)
    uvec2 pixel = uvec2(gl_FragCoord.xy);
    uvec2 tile = pixel / 16u;
    uint tileIdx = tile.y * uint(u_tileParams.x) + tile.x;
    uint count = s_tileLightCounts[tileIdx];

    for (uint i = 0; i < count; ++i) {
        uint lightIdx = s_tileLightIndices[tileIdx * 64u + i];
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
            }
        }
    }

    // Emissive comes from the material G-buffer RT (already scaled by the
    // emissive factor and emissive map in fs_gbuffer).
    finalColor += matData.rgb;

    // Tonemapping moved to post-process

    gl_FragColor = vec4(finalColor, 1.0);
}
