$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_depth, 2);

uniform mat4 u_ssrInvProj;
uniform vec4 u_cameraPosition;

vec3 getViewPos(vec2 uv, float depth) {
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 view = mul(u_ssrInvProj, clip);
    return view.xyz / view.w;
}

void main() {
    vec2 uv = v_texcoord0;
    vec4 col = texture2D(s_color, uv);
    vec4 nR = texture2D(s_normalRough, uv);
    float depth = texture2D(s_depth, uv).r;

    if (depth >= 0.9999) { gl_FragColor = col; return; }

    vec3 N = normalize(nR.rgb * 2.0 - 1.0);
    float roughness = nR.a;
    vec3 viewPos = getViewPos(uv, depth);

    if (roughness > 0.6) { gl_FragColor = col; return; }

    // Raymarching в экранном пространстве
    vec3 V = normalize(viewPos);
    vec3 R = reflect(V, N);

    float stepSize = 0.05;
    float hit = 0.0;
    vec3 hitColor = vec3(0.0);

    for (int i = 1; i <= 24; ++i) {
        float t = float(i) / 24.0;
        vec3 rayPos = viewPos + R * t * 10.0;

        vec4 proj = mul(u_ssrInvProj, vec4(rayPos, 1.0));
        // Инверсно: rayPos уже в view space; проецируем в UV
        // (используем u_ssrInvProj, но с текущим z — упрощение)
        vec2 sampleUV = uv + R.xy * stepSize * t;
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sampleDepth = texture2D(s_depth, sampleUV).r;
        vec3 samplePos = getViewPos(sampleUV, sampleDepth);

        // Проверяем пересечение
        float depthDiff = samplePos.z - rayPos.z;
        if (depthDiff > 0.0 && depthDiff < 0.5) {
            hit = 1.0;
            hitColor = texture2D(s_color, sampleUV).rgb;
            break;
        }
    }

    if (hit > 0.5) {
        // Смешиваем с отражением по Fresnel
        float fresnel = pow(1.0 - max(dot(N, -V), 0.0), 4.0);
        col.rgb = mix(col.rgb, hitColor, fresnel * 0.6);
    }

    gl_FragColor = col;
}
