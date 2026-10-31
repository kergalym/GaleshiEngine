$input a_position, i_data0, i_data1, i_data2, i_data3

#include <bgfx_shader.sh>

// Generic instanced position-only vertex shader for shadow maps. Materials
// with a non-zero shadow-depth offset are submitted through the per-entity
// path because the offset is per material, not per instance.
void main()
{
    mat4 instanceModel = mtxFromCols(i_data0, i_data1, i_data2, i_data3);
    mat4 model = mul(u_model[0], instanceModel);
    gl_Position = mul(u_viewProj, mul(model, vec4(a_position, 1.0)));
}
