$input v_texcoord0
#include <bgfx_shader.sh>

// Reduced-resolution screen-space ambient occlusion.
// The G-buffer stores world position and world normal, so view-space
// positions are reconstructed without assuming a depth-buffer convention.

SAMPLER2D(s_worldPos,    0);
SAMPLER2D(s_normalRough, 1);

uniform mat4 u_aoView;
uniform mat4 u_aoProj;
uniform vec4 u_aoParams;   // x=sample radius in pixels, y=bias, z=max distance, w=strength
uniform vec4 u_aoViewport; // xy=full resolution, zw=1/full resolution
uniform vec4 u_aoSource;   // xy=AO source size, zw=1/AO source size

vec3 hemisphereKernel(int index)
{
    float t = (float(index) + 0.5) / 16.0;
    float phi = t * 12.5663706;
    float radius = sqrt(t);
    return vec3(cos(phi) * radius, sin(phi) * radius,
                sqrt(max(0.0, 1.0 - t)));
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
    ivec2 coord = ivec2(gl_FragCoord.xy);

    // Map the reduced-resolution AO pixel to its centre in full-screen UV.
    vec2 uv = (vec2(coord) + 0.5) * u_aoSource.zw;

    vec4 wp = texture2D(s_worldPos, uv);
    if (wp.w < 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 viewPos = mul(u_aoView, vec4(wp.xyz, 1.0)).xyz;
    float viewDist = max(-viewPos.z, 0.001);
    vec3 viewNormal = normalize(
        mul(mat3(u_aoView), texture2D(s_normalRough, uv).xyz * 2.0 - 1.0));

    float kernelScale = min(5.0, 10.0 / viewDist);
    float stepSize = u_aoParams.x * u_aoViewport.z * 30.0 /
                     (0.8 * kernelScale);
    float depthBias = u_aoParams.y * 0.5 / kernelScale;
    float rangeLimit = u_aoParams.z * 0.2;
    vec3 noise = randomVector(vec3(gl_FragCoord.xy, 0.0));

    float occlusion = 0.0;
    float validWeight = 0.0;

    for (int i = 0; i < 16; ++i) {
        vec3 direction = mix(hemisphereKernel(i), noise, 0.25) * 0.6;
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
    occlusion *= 0.4;

    float ao = 1.0 - occlusion;
    ao = pow(saturate(ao), u_aoParams.w);

    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
