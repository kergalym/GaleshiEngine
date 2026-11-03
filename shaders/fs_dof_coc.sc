$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_dofDepth, 0);
uniform vec4 u_dofParams; // x=focusDist(m), y=invNear, z=invFar, w=maxCoC(px).

void main() {
    float depth = texture2D(s_dofDepth, v_texcoord0).r;

    // Sky/far: maximum blur.
    if (depth >= 0.9999) { gl_FragColor = vec4(1.0, 0.0, 0.0, 1.0); return; }

    float focusDist = u_dofParams.x;
    float invNear   = u_dofParams.y;
    float invFar    = u_dofParams.z;

    // Linearize: 1/z = (1-depth)*invNear + depth*invFar (Vulkan RH perspective).
    float invZ   = (1.0 - depth) * invNear + depth * invFar;
    float zWorld = 1.0 / max(invZ, 1e-6);

    // Symmetric thin-lens CoC, normalized to [0,1]:
    // 0 at focus, approaching 1 as z approaches 0 or infinity.
    float coc = abs(zWorld - focusDist) / max(zWorld, focusDist);
    coc = clamp(coc, 0.0, 1.0);

    gl_FragColor = vec4(coc, 0.0, 0.0, 1.0);
}
