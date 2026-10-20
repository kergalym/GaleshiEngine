$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMr,    0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_worldPos,    2);
SAMPLER2D(s_shadowMap0,  3);
SAMPLER2D(s_shadowMap1,  4);
SAMPLER2D(s_shadowMap2,  5);
SAMPLER2D(s_irradianceMap, 6);
SAMPLER2D(s_envMap,        7);
SAMPLER2D(s_brdfLUT,       8);
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
uniform mat4 u_skyInvViewProj;

uniform vec4 u_pointLightPosRadius[8];
uniform vec4 u_pointLightColorInt[8];
uniform vec4 u_pointLightCount;

uniform vec4 u_spotLightPosRadius[4];
uniform vec4 u_spotLightDirInner[4];
uniform vec4 u_spotLightColorOuter[4];
uniform vec4 u_spotLightCount;
uniform mat4 u_spotLightVP0;
uniform mat4 u_spotLightVP1;
uniform mat4 u_spotLightVP2;
uniform mat4 u_spotLightVP3;

const float PI = 3.14159265359;

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

vec3 calcLight(vec3 N, vec3 V, vec3 albedo, float metallic, float rough, vec3 L, vec3 radiance) {
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.000001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);
    if (NdotL <= 0.0) return vec3(0.0);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
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
    float bias = max(0.002 * (1.0 - NdotL), 0.0005);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 2048.0, 1.0 / 2048.0);

    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth;
            if (cascadeIdx == 0) pcfDepth = texture2D(s_shadowMap0, projCoords.xy + vec2(x, y) * texelSize).r;
            else if (cascadeIdx == 1) pcfDepth = texture2D(s_shadowMap1, projCoords.xy + vec2(x, y) * texelSize).r;
            else pcfDepth = texture2D(s_shadowMap2, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += (currentDepth - bias > pcfDepth) ? 1.0 : 0.0;
        }
    }
    return shadow / 9.0;
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

void main()
{
    vec2 uv = v_texcoord0;
    vec4 wPos = texture2D(s_worldPos, uv);
    
    if (length(wPos.xyz) < 0.001) {
        vec2 ndc = uv * 2.0 - 1.0;
        vec4 clip = vec4(ndc, 1.0, 1.0);
        vec4 world = mul(u_skyInvViewProj, clip);
        vec3 dir = normalize(world.xyz / world.w);
        
        vec3 skyColor = mix(vec3(0.4, 0.6, 1.0), vec3(0.05, 0.1, 0.3), clamp(dir.y, 0.0, 1.0));
        float sun = max(dot(dir, normalize(-u_lightDirection.xyz)), 0.0);
        skyColor += vec3(1.0, 0.9, 0.8) * pow(sun, 128.0) * 10.0;
        gl_FragColor = vec4(skyColor, 1.0);
        return;
    }

    vec4 aMr  = texture2D(s_albedoMr,    uv);
    vec4 nR   = texture2D(s_normalRough, uv);

    vec3 albedo    = aMr.rgb;
    float metallic = aMr.a;
    vec3 N         = normalize(nR.rgb * 2.0 - 1.0);
    float rough    = max(nR.a, 0.05);
    vec3 V         = normalize(u_cameraPosition.xyz - wPos.xyz);

    // 1. Ambient (Базовое освещение, чтобы не было абсолютно черных теней)
    // Увеличено с 0.15 до 0.3 для лучшей видимости в тенях
        // Physically correct IBL (Split-Sum)
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NdotV = max(dot(N, V), 0.000001);
    
    vec3 F = F_SchlickRoughness(NdotV, F0, rough);
    vec3 kS = F;
    vec3 kD = vec3(1.0) - kS;
    kD *= 1.0 - metallic;

    // Diffuse IBL
    vec2 envUV = N.xy * 0.5 + 0.5;
    vec3 irradiance = texture2D(s_irradianceMap, envUV).rgb;
    vec3 diffuseIBL = irradiance * albedo;
    
    // Specular IBL
    vec3 R = reflect(-V, N);
    vec2 specUV = R.xy * 0.5 + 0.5;
    vec3 prefilteredColor = texture2D(s_envMap, specUV).rgb;
    vec2 brdf = texture2D(s_brdfLUT, vec2(NdotV, rough)).xy;
    vec3 specularIBL = prefilteredColor * (F * brdf.x + brdf.y);
    
    vec3 finalColor = (kD * diffuseIBL + specularIBL) + (albedo * 0.05); 

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

    finalColor += calcLight(N, V, albedo, metallic, rough, L_dir, radiance_dir) * visibility;

    // 3. Point Lights (unrolled)
    int pCount = int(u_pointLightCount.x);
    if (pCount > 0) {
        vec3 lightPos = u_pointLightPosRadius[0].xyz;
        float radius = u_pointLightPosRadius[0].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[0].rgb * u_pointLightColorInt[0].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 1) {
        vec3 lightPos = u_pointLightPosRadius[1].xyz;
        float radius = u_pointLightPosRadius[1].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[1].rgb * u_pointLightColorInt[1].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 2) {
        vec3 lightPos = u_pointLightPosRadius[2].xyz;
        float radius = u_pointLightPosRadius[2].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[2].rgb * u_pointLightColorInt[2].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 3) {
        vec3 lightPos = u_pointLightPosRadius[3].xyz;
        float radius = u_pointLightPosRadius[3].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[3].rgb * u_pointLightColorInt[3].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 4) {
        vec3 lightPos = u_pointLightPosRadius[4].xyz;
        float radius = u_pointLightPosRadius[4].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[4].rgb * u_pointLightColorInt[4].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 5) {
        vec3 lightPos = u_pointLightPosRadius[5].xyz;
        float radius = u_pointLightPosRadius[5].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[5].rgb * u_pointLightColorInt[5].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 6) {
        vec3 lightPos = u_pointLightPosRadius[6].xyz;
        float radius = u_pointLightPosRadius[6].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[6].rgb * u_pointLightColorInt[6].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
        }
    }
    if (pCount > 7) {
        vec3 lightPos = u_pointLightPosRadius[7].xyz;
        float radius = u_pointLightPosRadius[7].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist <= radius) {
            vec3 L = toLight / dist;
            float atten = 1.0 / (dist * dist + 1.0);
            float window = saturate(1.0 - pow(dist / radius, 4.0));
            atten *= window * window;
            vec3 radiance = u_pointLightColorInt[7].rgb * u_pointLightColorInt[7].a * atten;
            finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
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
                finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance) * (1.0 - spotShadow);
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
                finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance) * (1.0 - spotShadow);
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
                finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance) * (1.0 - spotShadow);
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
                finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance) * (1.0 - spotShadow);
            }
        }
    }

    // Tonemapping moved to post-process

    gl_FragColor = vec4(finalColor, 1.0);
}
