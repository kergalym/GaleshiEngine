$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_ssao, 0);
SAMPLER2D(s_depth, 1);
uniform vec4 u_texelSize;

void main() {
    vec2 texel = u_texelSize.xy;
    float centerDepth = texture2D(s_depth, v_texcoord0).r;
    
    float result = 0.0;
    float totalWeight = 0.0;

    // 5x5 Bilateral Blur
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec2 offset = vec2(x, y) * texel;
            float sampleDepth = texture2D(s_depth, v_texcoord0 + offset).r;
            
            // Если глубина соседа сильно отличается, пропускаем его (защита от ореолов)
            if (abs(centerDepth - sampleDepth) < 0.001) {
                result += texture2D(s_ssao, v_texcoord0 + offset).r;
                totalWeight += 1.0;
            }
        }
    }
    
    if (totalWeight > 0.0) result /= totalWeight;
    else result = texture2D(s_ssao, v_texcoord0).r;

    gl_FragColor = vec4(result, result, result, 1.0);
}
