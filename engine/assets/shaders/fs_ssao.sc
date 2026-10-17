// SSAO BGFX Shader Stub
$input v_texcoord0

#include <bgfx_shader.sh>

SAMPLER2D(s_depth, 0);
SAMPLER2D(s_normal, 1);

void main() {
    // Здесь будет реализация SSAO
    gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
}
