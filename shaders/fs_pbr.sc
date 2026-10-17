$input v_worldPos, v_normal, v_tangent, v_uv

#include <bgfx_shader.sh>

uniform vec4 u_albedoFactor;
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
uniform vec4 u_emissiveFactor;
uniform vec4 u_cameraPosition;
uniform vec4 u_lightDirection;
uniform vec4 u_lightColorIntensity;

SAMPLER2D(s_albedoMap,            0);
SAMPLER2D(s_metallicRoughnessMap, 1);
SAMPLER2D(s_normalMap,            2);

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

void main()
{
    vec3 albedo    = u_albedoFactor.rgb * texture2D(s_albedoMap, v_uv).rgb;
    float metallic = u_metallicFactor.x   * texture2D(s_metallicRoughnessMap, v_uv).b;
    float rough    = u_roughnessFactor.x  * texture2D(s_metallicRoughnessMap, v_uv).g;
    rough = clamp(rough, 0.04, 1.0);

    vec3 N = normalize(v_normal);
    vec3 T = normalize(v_tangent - N * dot(N, v_tangent));
    vec3 B = cross(N, T);
    mat3 TBN = mat3(T, B, N);

    vec3 tangentNormal = texture2D(s_normalMap, v_uv).xyz * 2.0 - 1.0;
    N = normalize(mul(TBN, tangentNormal));

    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos);
    vec3 L = normalize(-u_lightDirection.xyz);
    vec3 H = normalize(V + L);

    vec3 F0 = mix(vec3_splat(0.04), albedo, metallic);

    float NDF = distributionGGX(N, H, rough);
    float G   = geometrySmith(N, V, L, rough);
    vec3  F   = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator    = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    vec3 specular     = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3_splat(1.0) - kS) * (1.0 - metallic);

    float NdL = max(dot(N, L), 0.0);
    vec3 radiance = u_lightColorIntensity.rgb * u_lightColorIntensity.a;

    vec3 direct = (kD * albedo / PI + specular) * radiance * NdL;
    vec3 ambient = albedo * 0.08 * (1.0 - metallic);

    vec3 color = ambient + direct + u_emissiveFactor.rgb;

    color = color / (color + vec3_splat(1.0));
    color = pow(color, vec3_splat(1.0 / 2.2));

    gl_FragColor = vec4(color, u_albedoFactor.a);
}
