$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_history, 1);
SAMPLER2D(s_texDepth, 2);

uniform mat4 u_prevViewProj;
uniform mat4 u_taaInvViewProj;
uniform vec4 u_taaParams;    // x = blend factor
uniform vec4 u_taaTexelSize; // xy = 1/width, 1/height

void main() {
    vec2 uv = v_texcoord0;
    vec4 current = texture2D(s_texColor, uv);
    float depth = texture2D(s_texDepth, uv).r;
    
    // Фон (небо) не нужно сглаживать, отдаем как есть
    if (depth >= 0.9999) { 
        gl_FragColor = current; 
        return; 
    }
    
    // Reproject using depth (Vulkan: Z in [0,1], so we don't flip Y)
    vec4 clip = vec4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, depth, 1.0);
    vec4 world = mul(u_taaInvViewProj, clip);
    world.xyz /= max(world.w, 0.000001);
    vec4 prevClip = mul(u_prevViewProj, world);
    vec2 prevUV = vec2((prevClip.x / max(prevClip.w, 0.000001)) * 0.5 + 0.5,
                       0.5 - (prevClip.y / max(prevClip.w, 0.000001)) * 0.5);
    
    // Если точка вышла за экран, отдаем текущий кадр
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        gl_FragColor = current; 
        return;
    }
    
    vec4 history = texture2D(s_history, prevUV);
    
    // --- AABB VARIANCE CLIPPING (Устранение Ghosting) ---
    // Вычисляем минимальный и максимальный цвет в окрестности 3x3
    vec3 minC = current.rgb, maxC = current.rgb;
    vec2 texel = u_taaTexelSize.xy;
    for(int i = -1; i <= 1; ++i) {
        for(int j = -1; j <= 1; ++j) {
            vec3 c = texture2D(s_texColor, uv + vec2(float(i), float(j)) * texel).rgb;
            minC = min(minC, c);
            maxC = max(maxC, c);
        }
    }
    
    // Ограничиваем историю цветом текущей сцены (защита от размазывания)
    history.rgb = clamp(history.rgb, minC, maxC);
    
    // Смешиваем с историей (Adjusted blend)
    float blend = u_taaParams.x;
    gl_FragColor = mix(current, history, blend);
}
