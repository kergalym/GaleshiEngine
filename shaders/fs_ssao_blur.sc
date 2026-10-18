$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_color, 0);
uniform vec4 u_texelSize;

void main() {
    vec2 t = u_texelSize.xy;
    vec4 center = texture2D(s_color, v_texcoord0);
    vec4 result = center;
    float totalW = 1.0;

    // 3x3 box blur (билатеральный здесь избыточен, так как AO уже сглажен range check'ом)
    for (int x = -1; x <= 1; ++x) {
        for (int y = -1; y <= 1; ++y) {
            if (x == 0 && y == 0) continue;
            result.rgb += texture2D(s_color, v_texcoord0 + vec2(x, y) * t).rgb;
            totalW += 1.0;
        }
    }
    gl_FragColor = vec4(result.rgb / totalW, center.a);
}
