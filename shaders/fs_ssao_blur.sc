$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
SAMPLER2D(s_depth, 1);
SAMPLER2D(s_normal, 2);

uniform vec4 u_texelSize; // xy = 1/width, 1/height

void main() {
    vec2 uv = v_texcoord0;
    vec2 texel = u_texelSize.xy;

    float centerDepth = texture2D(s_depth, uv).r;
    vec3 centerNormal = normalize(texture2D(s_normal, uv).rgb * 2.0 - 1.0);

    if (centerDepth >= 0.9999) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    float totalWeight = 1.0;
    float totalSSAO = texture2D(s_color, uv).r;

    // 5x5 Bilateral Blur
    for (int i = -2; i <= 2; ++i) {
        for (int j = -2; j <= 2; ++j) {
            if (i == 0 && j == 0) continue;

            vec2 offset = vec2(float(i), float(j)) * texel * 1.5;
            vec2 sampleUV = uv + offset;

            float sampleDepth = texture2D(s_depth, sampleUV).r;
            vec3 sampleNormal = normalize(texture2D(s_normal, sampleUV).rgb * 2.0 - 1.0);
            float sampleSSAO = texture2D(s_color, sampleUV).r;

            if (sampleDepth >= 0.9999) continue;

            // Depth Weight: не размываем через границы объектов
            float depthDiff = abs(centerDepth - sampleDepth);
            float depthWeight = exp(-depthDiff * 1000.0);

            // Normal Weight: не размываем через изломы нормалей
            float normalDot = dot(centerNormal, sampleNormal);
            float normalWeight = pow(max(normalDot, 0.0), 32.0);

            float weight = depthWeight * normalWeight;

            totalSSAO += sampleSSAO * weight;
            totalWeight += weight;
        }
    }

    gl_FragColor = vec4(totalSSAO / totalWeight, 1.0, 1.0, 1.0);
}
