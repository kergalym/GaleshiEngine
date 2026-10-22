$input v_texcoord0
#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_texDepth, 1);
uniform mat4 u_mbPrevVP;
uniform mat4 u_mbInvVP;
uniform vec4 u_motionBlurParams; // x = intensity

void main() {
    float depth = texture2D(s_texDepth, v_texcoord0).r;
    if (depth >= 0.9999) {
        gl_FragColor = texture2D(s_texColor, v_texcoord0);
        return;
    }

    vec4 clip = vec4(v_texcoord0 * 2.0 - 1.0, depth, 1.0);
    vec4 world = mul(u_mbInvVP, clip);
    world.xyz /= world.w;

    vec4 prevClip = mul(u_mbPrevVP, world);
    prevClip.xy /= prevClip.w;
    vec2 prevUV = vec2(prevClip.x * 0.5 + 0.5, 0.5 - prevClip.y * 0.5);

    vec2 velocity = (v_texcoord0 - prevUV) * u_motionBlurParams.x;
    float speed = length(velocity);

    if (speed < 0.001) {
        gl_FragColor = texture2D(s_texColor, v_texcoord0);
        return;
    }

    vec4 sum = vec4(0.0);
    float wsum = 0.0;
    for (int i = -4; i <= 4; ++i) {
        float w = 1.0 - abs(float(i)) / 5.0;
        vec2 uv = v_texcoord0 + velocity * (float(i) / 4.0);
        sum += texture2D(s_texColor, uv) * w;
        wsum += w;
    }
    gl_FragColor = sum / wsum;
}
