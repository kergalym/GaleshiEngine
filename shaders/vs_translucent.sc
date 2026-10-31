$input a_position, a_normal, a_tangent, a_texcoord0, a_indices, a_weight
$output v_worldPos, v_normal, v_tangent, v_uv, v_clip

#include <bgfx_shader.sh>

SAMPLER2D(s_boneMatrices, 3);

mat4 getBoneMatrix(int idx) {
    float y = (float(idx) + 0.5) / 256.0;
    vec4 r0 = texture2D(s_boneMatrices, vec2(0.125, y));
    vec4 r1 = texture2D(s_boneMatrices, vec2(0.375, y));
    vec4 r2 = texture2D(s_boneMatrices, vec2(0.625, y));
    vec4 r3 = texture2D(s_boneMatrices, vec2(0.875, y));
    return mat4(r0, r1, r2, r3);
}

void main()
{
    int i0 = int(a_indices.x + 0.5);
    int i1 = int(a_indices.y + 0.5);
    int i2 = int(a_indices.z + 0.5);
    int i3 = int(a_indices.w + 0.5);

    mat4 skinMat = getBoneMatrix(i0) * a_weight.x +
                   getBoneMatrix(i1) * a_weight.y +
                   getBoneMatrix(i2) * a_weight.z +
                   getBoneMatrix(i3) * a_weight.w;

    vec4 skinnedPos = mul(skinMat, vec4(a_position, 1.0));
    vec4 worldPos   = mul(u_model[0], skinnedPos);

    v_worldPos = worldPos.xyz;
    v_normal   = mul(u_model[0], mul(skinMat, vec4(a_normal, 0.0))).xyz;
    v_tangent  = mul(u_model[0], mul(skinMat, vec4(a_tangent, 0.0))).xyz;
    v_uv       = a_texcoord0;

    v_clip = mul(u_viewProj, worldPos);
    gl_Position = v_clip;
}
