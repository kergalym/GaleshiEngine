$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
void main() {
    // Пока SSR просто возвращает цвет (чтобы не ломать сцену)
    gl_FragColor = texture2D(s_color, v_texcoord0);
}
