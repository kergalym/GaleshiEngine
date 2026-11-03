$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_dofColor, 0);
SAMPLER2D(s_dofCoc,   1);
uniform vec4 u_dofTexelSize; // xy = 1/w, 1/h, z = maxCoC (px)
uniform vec4 u_dofDir; // (1,0) horizontal blur, (0,1) vertical blur.

void main() {
    // Must match u_dofParams.w in the CoC pass.
    float maxCoC = max(u_dofTexelSize.z, 0.0);

    vec2  dir   = u_dofDir.xy;
    vec2  texel = u_dofTexelSize.xy;
    float cocC  = texture2D(s_dofCoc, v_texcoord0).r;

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
