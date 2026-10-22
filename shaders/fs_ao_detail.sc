$input v_texcoord0
#include <bgfx_shader.sh>

// Full-resolution detail AO. Multiplies the upsampled AO by a tiny-radius
// occlusion term to recover contact shadows.
// Ported from tobspr/RenderPipeline: rpplugins/ao/shader/small_scale_ao.frag.glsl

SAMPLER2D(s_aoSource,    0);
SAMPLER2D(s_worldPos,    1);
SAMPLER2D(s_normalRough, 2);

uniform mat4 u_aoView;
uniform mat4 u_aoProj;
uniform vec4 u_aoViewport; // xy=full resolution, zw=1/full resolution

vec3 poisson3D(int i)
{
    if (i == 0)  return vec3(-0.0601252, -0.0902530,  0.2425850);
    if (i == 4)  return vec3(-0.4639580, -0.6033550, -0.6450490);
    if (i == 8)  return vec3( 0.1753630,  0.3268070, -0.9230220);
    return vec3(-0.7097460, 0.0374277, 0.6820500);
}

vec3 hash33(vec3 p)
{
    p = fract(p * vec3(0.1031, 0.1030, 0.0973));
    p += dot(p, p.yxz + 33.33);
    return fract((p.xxy + p.yxx) * p.zyx) * 2.0 - 1.0;
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

    vec3 viewN = normalize(mul(mat3(u_aoView), texture2D(s_normalRough, uv).xyz * 2.0 - 1.0));
    float kernelScale = min(5.0, 10.0 / viewDist);

    const float sampleRadius = 6.0;
    const float maxRange = 0.2;
    float sampleOffset = sampleRadius * u_aoViewport.z * 30.0 / kernelScale;
    float bias = 0.0005 + 0.01 / kernelScale;

    vec3 noise = hash33(vec3(gl_FragCoord.xy, 1.0));

    float accum = 0.0;
    float rangeAccum = 0.0;

    for (int i = 0; i < 4; ++i) {
        vec3 offset = poisson3D(i * 4);
        offset = normalize(offset + noise + vec3(0.00001));
        offset = dot(offset, viewN) < 0.0 ? -offset : offset;

        vec3 offsetPos = viewPos + offset * sampleOffset;

        vec4 clip = mul(u_aoProj, vec4(offsetPos, 1.0));
        if (clip.w <= 0.0001) continue;

        vec3 ndc = clip.xyz / clip.w;
        vec2 sampleUV = vec2(ndc.x * 0.5 + 0.5, 0.5 - ndc.y * 0.5);
        if (sampleUV.x < 0.0 || sampleUV.x > 1.0 ||
            sampleUV.y < 0.0 || sampleUV.y > 1.0) continue;

        vec4 sampleWp = texture2D(s_worldPos, sampleUV);
        if (sampleWp.w < 0.5) continue;

        vec3 sampleViewPos = mul(u_aoView, vec4(sampleWp.xyz, 1.0)).xyz;
        float linzA = -offsetPos.z;
        float linzB = -sampleViewPos.z;

        float modifier = step(abs(linzA - linzB), maxRange);
        rangeAccum += modifier * 0.5 + 0.5;
        modifier *= step(linzB + bias, linzA);
        accum += modifier;
    }

    accum /= max(0.1, rangeAccum);
    accum = 1.0 - accum;
    accum = pow(accum, 5.0);

    float ao = previousAO * accum;
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
