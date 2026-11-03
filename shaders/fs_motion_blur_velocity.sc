$input v_texcoord0
#include <bgfx_shader.sh>

// Camera velocity generation: reconstruct the current world position,
// project it with the previous view-projection matrix, and store the
// screen-space delta in UV space for fs_motion_blur.sc.

SAMPLER2D(s_worldPos, 0);
SAMPLER2D(s_mbObjectMotion, 1);
uniform mat4 u_mbPrevVP;
uniform mat4 u_mbInvVP;
uniform vec4 u_mbVelocityParams; // x = velocity scale (blur factor * 60/fps)

void main() {
    vec2 uv = v_texcoord0;
    vec4 worldSample = texture2D(s_worldPos, uv);

    // Opaque geometry already carries per-object UV motion.
    if (worldSample.w >= 0.5) {
        vec2 objectMotion = texture2D(s_mbObjectMotion, uv).xy;
        gl_FragColor = vec4(objectMotion * u_mbVelocityParams.x, 0.0, 1.0);
        return;
    }

    // Sky has no world position. Reconstruct a far-plane point so camera
    // rotation still produces background parallax.
    vec2 ndc = vec2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
    vec4 farPoint = mul(u_mbInvVP, vec4(ndc, 1.0, 1.0));
    if (abs(farPoint.w) < 1e-5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }
    vec3 world = farPoint.xyz / farPoint.w;

    vec4 prevClip = mul(u_mbPrevVP, vec4(world, 1.0));
    if (prevClip.w <= 1e-5) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec2 prevUV = prevClip.xy / prevClip.w;
    prevUV = vec2(prevUV.x * 0.5 + 0.5, 0.5 - prevUV.y * 0.5);

    // Reject points that were outside the previous frame. Sampling those
    // positions would introduce black streaks.
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        gl_FragColor = vec4(0.0, 0.0, 0.0, 1.0);
        return;
    }

    vec2 velocity = (prevUV - uv) * u_mbVelocityParams.x;
    gl_FragColor = vec4(velocity, 0.0, 1.0);
}
