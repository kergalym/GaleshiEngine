$input a_position
$output v_texcoord0
#include <bgfx_shader.sh>
void main()
{
    gl_Position = vec4(a_position.xy, 0.0, 1.0);
    // Y-flip UV: BGFX FBO textures are stored bottom-up;
    // the fullscreen quad flips Y on readback for ImGui::Image.
    v_texcoord0 = vec2(a_position.x * 0.5 + 0.5,
                       1.0 - (a_position.y * 0.5 + 0.5));
}
