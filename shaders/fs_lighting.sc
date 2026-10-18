$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMr,    0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_worldPos,    2);

uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;

// Point Lights (max 8)
uniform vec4 u_pointLightPosRadius[8];
uniform vec4 u_pointLightColorInt[8];
uniform vec4 u_pointLightCount;

// Spot Lights (max 4)
uniform vec4 u_spotLightPosRadius[4];
uniform vec4 u_spotLightDirInner[4];   // xyz = dir, w = cos(inner)
uniform vec4 u_spotLightColorOuter[4]; // rgb = color, a = cos(outer)
uniform vec4 u_spotLightCount;

const float PI = 3.14159265359;

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}
float G_Smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0; float k = (r * r) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}
vec3 F_Schlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

// Универсальная функция PBR для любого направленного/точечного света
vec3 calcLight(vec3 N, vec3 V, vec3 albedo, float metallic, float rough, vec3 L, vec3 radiance) {
    vec3 H = normalize(L + V);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    if (NdotL <= 0.0) return vec3(0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float D = D_GGX(NdotH, rough);
    float G = G_Smith(NdotV, NdotL, rough);
    vec3 F  = F_Schlick(VdotH, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * radiance * NdotL;
}

void main()
{
    vec2 uv = v_texcoord0;
    vec4 wPos = texture2D(s_worldPos, uv);
    
    if (length(wPos.xyz) < 0.001) {
        gl_FragColor = vec4(0.05, 0.05, 0.07, 1.0);
        return;
    }

    vec4 aMr  = texture2D(s_albedoMr,    uv);
    vec4 nR   = texture2D(s_normalRough, uv);

    vec3 albedo    = aMr.rgb;
    float metallic = aMr.a;
    vec3 N         = normalize(nR.rgb * 2.0 - 1.0);
    float rough    = max(nR.a, 0.05);
    vec3 V         = normalize(u_cameraPosition.xyz - wPos.xyz);

    vec3 finalColor = albedo * 0.05; // Базовый ambient

    // --- 1. Directional Light (Sun) ---
    vec3 L_dir = normalize(-u_lightDirection.xyz);
    vec3 radiance_dir = u_lightColorIntensity.rgb * u_lightColorIntensity.a;
    finalColor += calcLight(N, V, albedo, metallic, rough, L_dir, radiance_dir);

    // --- 2. Point Lights ---
    int pCount = int(u_pointLightCount.x);
    for (int i = 0; i < 8; ++i) {
        if (i >= pCount) break;
        vec3 lightPos = u_pointLightPosRadius[i].xyz;
        float radius = u_pointLightPosRadius[i].w;
        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist > radius) continue;

        vec3 L = toLight / dist;
        float atten = 1.0 / (dist * dist + 1.0);
        float window = saturate(1.0 - pow(dist / radius, 4.0));
        atten *= window * window;

        vec3 radiance = u_pointLightColorInt[i].rgb * u_pointLightColorInt[i].a * atten;
        finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
    }

    // --- 3. Spot Lights ---
    int sCount = int(u_spotLightCount.x);
    for (int i = 0; i < 4; ++i) {
        if (i >= sCount) break;
        vec3 lightPos = u_spotLightPosRadius[i].xyz;
        float radius = u_spotLightPosRadius[i].w;
        vec3 spotDir = normalize(u_spotLightDirInner[i].xyz);
        float cosInner = u_spotLightDirInner[i].w;
        float cosOuter = u_spotLightColorOuter[i].a;

        vec3 toLight = lightPos - wPos.xyz;
        float dist = length(toLight);
        if (dist > radius) continue;

        vec3 L = toLight / dist;

        // Угловое затухание (cone attenuation)
        float cosAngle = dot(-L, spotDir);
        float spotAtten = clamp((cosAngle - cosOuter) / (cosInner - cosOuter), 0.0, 1.0);
        if (spotAtten <= 0.0) continue;

        // Дистанционное затухание
        float atten = 1.0 / (dist * dist + 1.0);
        float window = saturate(1.0 - pow(dist / radius, 4.0));
        atten *= window * window * spotAtten;

        vec3 radiance = u_spotLightColorOuter[i].rgb * atten;
        finalColor += calcLight(N, V, albedo, metallic, rough, L, radiance);
    }

    // Tone Mapping + Gamma
    finalColor = finalColor / (finalColor + vec3(1.0));
    finalColor = pow(finalColor, vec3(1.0 / 2.2));

    gl_FragColor = vec4(finalColor, 1.0);
}
