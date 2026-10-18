$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMr,    0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_worldPos,    2);
SAMPLER2D(s_shadowMap0,  3);
SAMPLER2D(s_shadowMap1,  4);
SAMPLER2D(s_shadowMap2,  5);

uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;
uniform mat4 u_lightViewProj[3];
uniform mat4 u_skyInvViewProj;

uniform vec4 u_pointLightPosRadius[8];
uniform vec4 u_pointLightColorInt[8];
uniform vec4 u_pointLightCount;

uniform vec4 u_spotLightPosRadius[4];
uniform vec4 u_spotLightDirInner[4];
uniform vec4 u_spotLightColorOuter[4];
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

float calculateShadow(vec4 lightSpacePos, float NdotL, int cascadeIdx) {
    vec3 projCoords = lightSpacePos.xyz / lightSpacePos.w;
    projCoords.xy = projCoords.xy * 0.5 + 0.5;

    if (projCoords.z > 1.0 || projCoords.z < 0.0 ||
        projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = max(0.005 * (1.0 - NdotL), 0.0005);
    float shadow = 0.0;
    vec2 texelSize = vec2(1.0 / 2048.0, 1.0 / 2048.0);

    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth;
            if (cascadeIdx == 0) pcfDepth = texture2D(s_shadowMap0, projCoords.xy + vec2(x, y) * texelSize).r;
            else if (cascadeIdx == 1) pcfDepth = texture2D(s_shadowMap1, projCoords.xy + vec2(x, y) * texelSize).r;
            else pcfDepth = texture2D(s_shadowMap2, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
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
    vec3 finalColor = albedo * 0.3; 

    // 2. Directional Light с тенями
    vec3 L_dir = normalize(-u_lightDirection.xyz);
    vec3 radiance_dir = u_lightColorIntensity.rgb * u_lightColorIntensity.a;
    float NdotL = max(dot(N, L_dir), 0.0);

    float camDist = length(wPos.xyz - u_cameraPosition.xyz);
    int cascadeIdx = 0;
    if (camDist > 40.0) cascadeIdx = 2;
    else if (camDist > 15.0) cascadeIdx = 1;

    // Normal Offset: сдвигаем позицию пикселя по нормали, чтобы избежать acne
    vec4 lightSpacePos = mul(u_lightViewProj[cascadeIdx], vec4(wPos.xyz + N * 0.1, 1.0));
    float shadowFactor = calculateShadow(lightSpacePos, NdotL, cascadeIdx);
    float visibility = 1.0 - shadowFactor;

    finalColor += calcLight(N, V, albedo, metallic, rough, L_dir, radiance_dir) * visibility;

    // 3. Point Lights
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

    // Tonemapping moved to post-process

    gl_FragColor = vec4(finalColor, 1.0);
}
