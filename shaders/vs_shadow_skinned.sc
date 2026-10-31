$input a_position, a_normal, a_indices, a_weight

#include <bgfx_shader.sh>

SAMPLER2D(s_boneMatrices, 3);
uniform vec4 u_shadowDepthOffset;

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

    vec4 worldPos = mul(u_model[0], mul(skinMat, vec4(a_position, 1.0)));
    if (abs(u_shadowDepthOffset.x) > 1e-6) {
        vec3 worldNormal = normalize(
            mul(u_model[0], mul(skinMat, vec4(a_normal, 0.0))).xyz);
        worldPos.xyz += worldNormal * u_shadowDepthOffset.x;
    }
    gl_Position = mul(u_viewProj, worldPos);
}
