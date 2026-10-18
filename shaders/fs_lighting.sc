$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_albedoMr, 0);
void main()
{
    vec4 c = texture2D(s_albedoMr, v_texcoord0);
    if (length(c.rgb) < 0.001) {
        gl_FragColor = vec4(1.0, 0.0, 1.0, 1.0);   // MAGENTA = RT0 empty
    } else {
        gl_FragColor = vec4(c.rgb * 1.5, 1.0);     // boosted albedo
    }
}
