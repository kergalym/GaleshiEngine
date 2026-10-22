$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor,   0); // sharp / original
SAMPLER2D(s_dofCoc,   1); // coc [0,1]
SAMPLER2D(s_dofBlur,    2); // blurred (bound via s_taaColor handle, slot 2)

void main() {
    vec4  sharp   = texture2D(s_texColor, v_texcoord0);
    vec4  blurred = texture2D(s_dofBlur,    v_texcoord0);
    float coc     = texture2D(s_dofCoc,   v_texcoord0).r;
    gl_FragColor = mix(sharp, blurred, smoothstep(0.0, 1.0, coc));
}
