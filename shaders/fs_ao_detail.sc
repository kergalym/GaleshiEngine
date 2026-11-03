$input v_texcoord0
#include <bgfx_shader.sh>

// Full-resolution detail AO. Multiplies the upsampled AO by a tiny-radius
// occlusion term to recover contact shadows.

SAMPLER2D(s_aoSource,    0);
SAMPLER2D(s_worldPos,    1);
SAMPLER2D(s_normalRough, 2);

uniform mat4 u_aoView;
uniform mat4 u_aoProj;
uniform vec4 u_aoViewport; // xy=full resolution, zw=1/full resolution

vec3 detailKernel(int index)
{
    float t = (float(index) + 0.5) / 4.0;
    float angle = float(index) * 2.39996323;
    float radius = sqrt(t);
    return vec3(cos(angle) * radius, sin(angle) * radius,
                0.45 + 0.4 * radius);
}

vec3 randomVector(vec3 p)
{
    return vec3(
        fract(sin(dot(p, vec3(12.9898, 78.233, 37.719))) * 43758.5453),
        fract(sin(dot(p, vec3(39.346, 11.135, 83.155))) * 24634.6345),
        fract(sin(dot(p, vec3(73.156, 52.235, 9.151))) * 56445.2345)
    ) * 2.0 - 1.0;
}

void main()
{
    vec2 uv = v_texcoord0;
    ivec2 coord = ivec2(gl_FragCoord.xy);

    float previousAO = texture2D(s_aoSource, uv).r;

    vec4 wp = texture2D(s_worldPos, uv);
    if (wp.w < 0.5) {
        gl_FragColor = vec4(previousAO, previousAO, previousAO, 1.0);
        return;
    }

    vec3 viewPos = mul(u_aoView, vec4(wp.xyz, 1.0)).xyz;
    float viewDist = -viewPos.z;
    if (viewDist > 150.0) {
        gl_FragColor = vec4(previousAO, previousAO, previousAO, 1.0);
        return;
    }
    viewDist = max(viewDist, 0.001);

    vec3 viewNormal = normalize(
        mul(mat3(u_aoView), texture2D(s_normalRough, uv).xyz * 2.0 - 1.0));
    float kernelScale = min(5.0, 10.0 / viewDist);

    const float sampleRadius = 6.0;
    const float rangeLimit = 0.2;
    float stepSize = sampleRadius * u_aoViewport.z * 30.0 / kernelScale;
    float depthBias = 0.0005 + 0.01 / kernelScale;
    vec3 noise = randomVector(vec3(gl_FragCoord.xy, 1.0));

    float occlusion = 0.0;
    float validWeight = 0.0;

    for (int i = 0; i < 4; ++i) {
        vec3 direction = normalize(detailKernel(i) + noise + vec3(0.00001));
        if (dot(direction, viewNormal) < 0.0) direction = -direction;

        vec3 candidate = viewPos + direction * stepSize;
        vec4 clip = mul(u_aoProj, vec4(candidate, 1.0));
        if (clip.w <= 0.0001) continue;

        vec3 ndc = clip.xyz / clip.w;
        vec2 candidateUV = vec2(ndc.x * 0.5 + 0.5,
                                0.5 - ndc.y * 0.5);
        if (candidateUV.x < 0.0 || candidateUV.x > 1.0 ||
            candidateUV.y < 0.0 || candidateUV.y > 1.0) continue;

        vec4 sampledWorld = texture2D(s_worldPos, candidateUV);
        if (sampledWorld.w < 0.5) continue;

        vec3 sampledView = mul(u_aoView, vec4(sampledWorld.xyz, 1.0)).xyz;
        float candidateDepth = -candidate.z;
        float sampledDepth = -sampledView.z;

        float inRange = step(abs(candidateDepth - sampledDepth), rangeLimit);
        validWeight += inRange * 0.5 + 0.5;
        occlusion += inRange * step(sampledDepth + depthBias, candidateDepth);
    }

    occlusion /= max(0.1, validWeight);
    float contact = pow(1.0 - occlusion, 5.0);
    float ao = previousAO * contact;
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
