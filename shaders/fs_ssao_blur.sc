$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_ssaoTex, 0);
SAMPLER2D(s_depthTex, 1);

uniform vec4 u_texelSize; // x: 1/width, y: 1/height

void main()
{
    vec2 texelSize = u_texelSize.xy;
    vec2 uv = v_texcoord0;
    
    float result = 0.0;
    float totalWeight = 0.0;
    
    float centerDepth = texture2D(s_depthTex, uv).r;
    
    // Защита фона (нет SSAO на небе)
    if (centerDepth >= 0.9999) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    for (int x = -2; x <= 2; ++x)
    {
        for (int y = -2; y <= 2; ++y)
        {
            vec2 offset = vec2(float(x), float(y)) * texelSize;
            float neighborDepth = texture2D(s_depthTex, uv + offset).r;
            float neighborSSAO  = texture2D(s_ssaoTex, uv + offset).r;
            
            // Плавная защита от перетекания (без жестких обрывов, чтобы не мерцало)
            float depthDiff = abs(centerDepth - neighborDepth);
            float depthWeight = 1.0 / (1.0 + depthDiff * 10000.0);
            
            result += neighborSSAO * depthWeight;
            totalWeight += depthWeight;
        }
    }
    
    float finalSSAO = result / max(totalWeight, 0.0001);
    gl_FragColor = vec4(finalSSAO, finalSSAO, finalSSAO, 1.0);
}
