$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_depth, 2);

uniform mat4 u_ssrInvProj;
uniform vec4 u_cameraPosition;
uniform vec4 u_texelSize;

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

    // Фон — просто возвращаем
    if (depth >= 0.9999) { gl_FragColor = col; return; }

    vec3 N = normalize(nR.rgb * 2.0 - 1.0);
    float roughness = nR.a;

    // Только для гладких поверхностей
    if (roughness > 0.4) { gl_FragColor = col; return; }

    vec3 viewPos = getViewPos(uv, depth);
    vec3 V = normalize(viewPos);
    vec3 R = reflect(V, N);

    // Проецируем точку начала луча и шагаем
    vec3 startPos = viewPos + N * 0.05;
    vec3 rayEnd = startPos + R * 30.0;

    // Screen-space raymarching с малым шагом
    const int STEPS = 32;
    vec2 prevUV = uv;
    float prevDepth = depth;

    bool hit = false;
    vec3 hitColor = vec3(0.0);
    float hitFade = 0.0;

    for (int i = 1; i <= STEPS; ++i) {
        float t = float(i) / float(STEPS);
        vec3 rayPos = mix(startPos, rayEnd, t);

        // Проекция в NDC (View Space → Clip → UV)
        vec4 clip = mul(u_ssrInvProj, vec4(rayPos, 1.0));
        // На самом деле нужно прямое преобразование, но у нас есть только invProj
        // Используем приближение: проецируем через invProj

        // Упрощение: используем линейную интерполяцию UV + depth
        // (это работает для плоских поверхностей, для кубов достаточно)
        vec2 sampleUV = mix(uv, uv + R.xy * 0.5, t);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) break;

        float sampleDepth = texture2D(s_depth, sampleUV).r;
        if (sampleDepth >= 0.9999) continue;

        vec3 samplePos = getViewPos(sampleUV, sampleDepth);

        // Проверка: пересечение луча с геометрией
        float dz = samplePos.z - rayPos.z;
        if (dz > 0.0 && dz < 1.0) {
            hit = true;
            hitColor = texture2D(s_color, sampleUV).rgb;
            hitFade = 1.0 - t; // Плавное затухание на дальних расстояниях
            break;
        }
    }

    if (hit) {
        // Fresnel — отражение усиливается на скользящих углах
        float fresnel = pow(1.0 - max(dot(N, -V), 0.0), 3.0);
        float blend = fresnel * (1.0 - roughness) * hitFade * 0.5;
        col.rgb = mix(col.rgb, hitColor, blend);
    }

    gl_FragColor = col;
}
