$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
SAMPLER2D(s_depth, 1);
SAMPLER2D(s_normal, 2);

uniform mat4 u_ssaoProj;
uniform mat4 u_ssaoInvProj;
uniform vec4 u_ssaoParams; // x=radius, y=bias, z=intensity

vec3 getViewPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = mul(u_ssaoInvProj, clip);
    return view.xyz / view.w;
}

void main() {
    vec2 uv = v_texcoord0;
    float depth = texture2D(s_depth, uv).r;
    vec4 color = texture2D(s_color, uv);

    if (depth >= 0.9999) { gl_FragColor = color; return; }

    vec3 viewPos = getViewPos(uv, depth);
    vec3 viewNorm = normalize(texture2D(s_normal, uv).rgb * 2.0 - 1.0);

    float radius = u_ssaoParams.x;
    float bias = u_ssaoParams.y;
    float intensity = u_ssaoParams.z;

    // Поэлементная инициализация (HLSL -> SPIR-V для Vulkan)
    vec3 kernel[16];
    kernel[0]  = vec3( 0.538,  0.478,  0.718);
    kernel[1]  = vec3(-0.435, -0.118,  0.893);
    kernel[2]  = vec3(-0.320,  0.756, -0.573);
    kernel[3]  = vec3( 0.895, -0.428, -0.117);
    kernel[4]  = vec3(-0.158, -0.846,  0.510);
    kernel[5]  = vec3( 0.681,  0.732, -0.018);
    kernel[6]  = vec3(-0.746,  0.114,  0.656);
    kernel[7]  = vec3( 0.210, -0.971,  0.113);
    kernel[8]  = vec3( 0.951,  0.283, -0.121);
    kernel[9]  = vec3(-0.434,  0.875,  0.214);
    kernel[10] = vec3(-0.164, -0.459,  0.873);
    kernel[11] = vec3( 0.757, -0.623,  0.191);
    kernel[12] = vec3(-0.904, -0.291,  0.310);
    kernel[13] = vec3( 0.442,  0.198, -0.876);
    kernel[14] = vec3( 0.214,  0.821,  0.532);
    kernel[15] = vec3(-0.734,  0.400, -0.548);

    vec3 helper = abs(viewNorm.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(helper, viewNorm));
    vec3 bitangent = cross(viewNorm, tangent);
    mat3 TBN = mat3(tangent, bitangent, viewNorm);

    float occlusion = 0.0;
    for (int i = 0; i < 16; ++i) {
        vec3 samplePos = viewPos + (TBN * kernel[i]) * radius;
        vec4 projected = mul(u_ssaoProj, vec4(samplePos, 1.0));
        projected.xyz /= projected.w;
        vec2 sampleUV = projected.xy * 0.5 + 0.5;

        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        float sampleDepth = texture2D(s_depth, sampleUV).r;
        vec3 sampleViewPos = getViewPos(sampleUV, sampleDepth);

        float rangeCheck = smoothstep(0.0, 1.0, radius / abs(viewPos.z - sampleViewPos.z));
        occlusion += (sampleViewPos.z >= samplePos.z + bias ? 1.0 : 0.0) * rangeCheck;
    }

    float ao = 1.0 - (occlusion / 16.0) * intensity;
    ao = clamp(ao, 0.0, 1.0);
    gl_FragColor = vec4(color.rgb * ao, color.a);
}
