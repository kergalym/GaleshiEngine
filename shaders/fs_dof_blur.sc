$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_dofColor, 0);
SAMPLER2D(s_dofCoc,   1);
uniform vec4 u_dofTexelSize; // xy = 1/w, 1/h, z = maxCoC (px)
uniform vec4 u_dofDir; // (1,0) horizontal blur, (0,1) vertical blur.
// x = blade count (0 = classic Gaussian), y = disk radius scale.
uniform vec4 u_dofBokeh;

const float PI = 3.14159265359;

// Radius of a regular polygon with 'blades' sides at the given angle.
float polygonRadius(float blades, float angle) {
    float half = PI / max(blades, 3.0);
    return cos(half) / max(cos(angle - half * round(angle / half)), 0.01);
}

void main() {
    // Must match u_dofParams.w in the CoC pass.
    float maxCoC = max(u_dofTexelSize.z, 0.0);

    vec2  dir   = u_dofDir.xy;
    vec2  texel = u_dofTexelSize.xy;
    float cocC  = texture2D(s_dofCoc, v_texcoord0).r;
    float blades = u_dofBokeh.x;

    // Cinematic bokeh: the horizontal pass becomes a copy and the vertical
    // pass applies a polygonal disk kernel, so the shape stays round.
    if (blades >= 2.5) {
        if (dir.x > 0.5) {
            gl_FragColor = texture2D(s_dofColor, v_texcoord0);
            return;
        }
        float diskRadius = maxCoC * cocC * max(u_dofBokeh.y, 0.1) * 1.5;
        vec4 sum = texture2D(s_dofColor, v_texcoord0);
        float wsum = 1.0;
        float jitter = fract(sin(dot(v_texcoord0, vec2(12.9898, 78.233))) *
                             43758.5453);
        const int rings = 2;
        const int segments = 8;
        for (int r = 1; r <= rings; ++r) {
            float radius = (float(r) / float(rings)) * diskRadius;
            for (int k = 0; k < segments; ++k) {
                float angle = (float(k) + jitter) / float(segments) *
                              6.2831853;
                vec2 off = vec2(cos(angle), sin(angle)) * radius *
                           polygonRadius(blades, angle) * texel;
                float w = 1.0 / float(rings * segments);
                sum += texture2D(s_dofColor, v_texcoord0 + off) * w;
                wsum += w;
            }
        }
        gl_FragColor = sum / wsum;
        return;
    }

    vec4  sum  = texture2D(s_dofColor, v_texcoord0);
    float wsum = 1.0;

    // 4 sample pairs (9 taps total), scaled by CoC.
    float stepSize = 1.0 + maxCoC * cocC * 0.25;
    for (int i = 1; i <= 4; ++i) {
        float stepScale = float(i) * stepSize;
        vec2  off       = dir * texel * stepScale;
        float w         = 1.0 - float(i) / 5.0; // 0.8, 0.6, 0.4, 0.2
        sum  += texture2D(s_dofColor, v_texcoord0 + off) * w;
        sum  += texture2D(s_dofColor, v_texcoord0 - off) * w;
        wsum += 2.0 * w;
    }
    gl_FragColor = sum / wsum;
}
