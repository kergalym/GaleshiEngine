$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_albedoMr,    0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_worldPos,    2);

uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;

const float PI = 3.14159265359;

float D_GGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float G_Smith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return (NdotV / (NdotV * (1.0 - k) + k)) * (NdotL / (NdotL * (1.0 - k) + k));
}

vec3 F_Schlick(float VdotH, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
}

void main()
{
    vec2 uv = v_texcoord0;
    vec4 wPos = texture2D(s_worldPos, uv);
    
    // ЗАЩИТА ОТ ФОНА (NaN): если позиция 0,0,0 - это небо
    if (length(wPos.xyz) < 0.001) {
        gl_FragColor = vec4(0.05, 0.05, 0.07, 1.0); // Темно-серый фон
        return;
    }

    vec4 aMr  = texture2D(s_albedoMr,    uv);
    vec4 nR   = texture2D(s_normalRough, uv);

    vec3 albedo    = aMr.rgb;
    float metallic = aMr.a;
    vec3 N         = normalize(nR.rgb * 2.0 - 1.0);
    float rough    = max(nR.a, 0.05);

    vec3 V = normalize(u_cameraPosition.xyz - wPos.xyz);
    vec3 L = normalize(-u_lightDirection.xyz);
    vec3 H = normalize(L + V);
    vec3 lightCol = u_lightColorIntensity.rgb * u_lightColorIntensity.a;

    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.001);
    float NdotH = max(dot(N, H), 0.0);
    float VdotH = max(dot(V, H), 0.0);

    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float D = D_GGX(NdotH, rough);
    float G = G_Smith(NdotV, NdotL, rough);
    vec3 F  = F_Schlick(VdotH, F0);

    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 0.001);
    vec3 kD = (1.0 - F) * (1.0 - metallic);
    vec3 diffuse = kD * albedo / PI;

    // Искусственный IBL (чтобы металл не был черным)
    vec3 fakeAmbient = vec3(0.1, 0.12, 0.15); 
    vec3 ambientDiffuse = albedo * 0.1;
    vec3 ambientSpecular = F0 * fakeAmbient * (1.0 - rough);

    vec3 color = ambientDiffuse + ambientSpecular + (diffuse + specular) * lightCol * NdotL;
    color = color / (color + vec3(1.0)); // Reinhard
    color = pow(color, vec3(1.0 / 2.2)); // sRGB

    gl_FragColor = vec4(color, 1.0);
}
