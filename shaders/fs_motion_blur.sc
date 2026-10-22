$input v_texcoord0
#include <bgfx_shader.sh>

// Camera motion blur, adapted from the RenderPipeline motion_blur plugin.
// The velocity target stores the per-pixel screen-space delta. Samples are
// distributed along the velocity vector and weighted by how well the sample
// velocity aligns with the center velocity. This suppresses bleeding from
// pixels moving in different directions.

SAMPLER2D(s_texColor, 0);
SAMPLER2D(s_mbVelocity, 1);
uniform vec4 u_motionBlurParams; // x = maxVelocityUV, y = minVelocityUV, z = viewportWidth

float hash12(vec2 p) {
    vec3 p3 = fract(vec3(p.xyx) * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
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

    const int numSamples = 12;
    float jitter = hash12(gl_FragCoord.xy);

    // A small center weight guarantees a non-zero denominator and keeps the
    // original pixel from disappearing when all sample weights are low.
    float weightSum = 1e-3;
    vec3 accum = center.rgb * weightSum;

    for (int i = -numSamples + 1; i < numSamples; ++i) {
        vec2 offset = (float(i) + 0.5 * jitter) / float(numSamples) * velocity;
        vec2 sampleUV = clamp(uv + offset, vec2(0.0), vec2(1.0));
        vec2 sampleVelocity = texture2D(s_mbVelocity, sampleUV).xy;

        float weight = saturate(dot(sampleVelocity, velocity) *
                                u_motionBlurParams.z * 3.0);
        accum += texture2D(s_texColor, sampleUV).rgb * weight;
        weightSum += weight;
    }

    accum /= max(weightSum, 1e-3);
    gl_FragColor = vec4(max(accum, vec3(0.0)), 1.0);
}
