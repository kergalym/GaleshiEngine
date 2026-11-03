$input v_texcoord0
#include <bgfx_shader.sh>

// Camera motion blur. Samples are distributed along the per-pixel
// screen-space velocity and weighted by directional agreement with the
// center pixel to suppress bleeding from neighbouring motion.

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_mbVelocity, 1);
uniform vec4 u_motionBlurParams; // x = maxVelocityUV, y = minVelocityUV, z = viewportWidth

float pixelJitter(vec2 p) {
    return fract(sin(dot(p, vec2(12.9898, 78.233))) * 43758.5453);
}

void main() {
    vec2 uv = v_texcoord0;
    vec4 center = texture2D(s_texColor, uv);
    vec2 velocity = texture2D(s_mbVelocity, uv).xy;

    float velocityLen = length(velocity);
    if (velocityLen < u_motionBlurParams.y) {
        gl_FragColor = center;
        return;
    }

    if (velocityLen > u_motionBlurParams.x) {
        velocity *= u_motionBlurParams.x / velocityLen;
        velocityLen = u_motionBlurParams.x;
    }

    const int tapCount = 12;
    float jitter = pixelJitter(gl_FragCoord.xy);
    float weightSum = 1e-3;
    vec3 colorSum = center.rgb * weightSum;

    for (int tap = -tapCount + 1; tap < tapCount; ++tap) {
        vec2 tapUV = clamp(
            uv + velocity * ((float(tap) + 0.5 * jitter) / float(tapCount)),
            vec2(0.0), vec2(1.0));
        vec2 tapVelocity = texture2D(s_mbVelocity, tapUV).xy;
        float weight = saturate(dot(tapVelocity, velocity) *
                                u_motionBlurParams.z * 3.0);
        colorSum += texture2D(s_texColor, tapUV).rgb * weight;
        weightSum += weight;
    }

    colorSum /= max(weightSum, 1e-3);
    gl_FragColor = vec4(max(colorSum, vec3(0.0)), 1.0);
}
