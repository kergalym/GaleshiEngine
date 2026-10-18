$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
SAMPLER2D(s_normalRough, 1);
SAMPLER2D(s_depth, 2); // D24S8

uniform vec4 u_texelSize;
uniform vec4 u_cameraPos;

// Простейший SSR: читаем нормаль, отражаем и сэмплируем в экранном пространстве
void main()
{
    vec2 uv = v_texcoord0;
    vec3 col = texture2D(s_color, uv).rgb;
    vec4 nR = texture2D(s_normalRough, uv);

    if (length(nR.rgb) > 0.01) { // Если не фон
        vec3 N = normalize(nR.rgb * 2.0 - 1.0);
        vec2 reflUV = uv + N.xy * 0.1; // Псевдо-отражение
        
        if (reflUV.x > 0.0 && reflUV.x < 1.0 && reflUV.y > 0.0 && reflUV.y < 1.0) {
            vec3 reflCol = texture2D(s_color, reflUV).rgb;
            float fresnel = pow(1.0 - max(N.z, 0.0), 4.0);
            col = mix(col, reflCol, fresnel * 0.5);
        }
    }
    gl_FragColor = vec4(col, 1.0);
}
