$input a_position, i_data0, i_data1, i_data2, i_data3

#include <bgfx_shader.sh>

void main()
{
    mat4 instanceModel = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    mat4 model = mul(u_model[0], instanceModel);
    gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
}
