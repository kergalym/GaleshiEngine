$input v_texcoord0
#include <bgfx_shader.sh>

// Separable bilateral blur for the full-resolution AO buffer.
// One pass is horizontal, the next one is vertical. Based on
// tobspr/RenderPipeline: rpcore/shader/bilateral_blur.frag.glsl

SAMPLER2D(s_aoSource,    0);
SAMPLER2D(s_worldPos,    1);
SAMPLER2D(s_normalRough, 2);

uniform mat4 u_aoView;
uniform vec4 u_aoViewport;   // xy=full resolution, zw=1/full resolution
uniform vec4 u_aoBlurParams; // x=normal factor, y=depth factor
uniform vec4 u_aoBlurDir;    // xy=blur direction

float viewDepth(vec3 worldPos)
{
    return -mul(u_aoView, vec4(worldPos, 1.0)).z;
}

vec2 sampleBilateral(vec2 uv, vec3 midN, float midDepth)
{
    vec4 wp = texture2D(s_worldPos, uv);
    if (wp.w < 0.5) return vec2(0.0, 0.0);

    vec3 n = normalize(texture2D(s_normalRough, uv).xyz * 2.0 - 1.0);
    float depth = viewDepth(wp.xyz);

    float normalWeight = 1.0 - saturate(u_aoBlurParams.x * distance(n, midN));
    float depthWeight = 1.0 - saturate(u_aoBlurParams.y * abs(depth - midDepth) * 3.0);
    float weight = normalWeight * depthWeight;

    float ao = texture2D(s_aoSource, uv).r;
    return vec2(ao * weight, weight);
}

void main()
{
    vec2 uv = v_texcoord0;

    vec4 midWp = texture2D(s_worldPos, uv);
    if (midWp.w < 0.5) {
        gl_FragColor = vec4(1.0, 1.0, 1.0, 1.0);
        return;
    }

    vec3 midN = normalize(texture2D(s_normalRough, uv).xyz * 2.0 - 1.0);
    float midDepth = viewDepth(midWp.xyz);

    vec2 accum = vec2(texture2D(s_aoSource, uv).r, 1.0);
    vec2 axis = u_aoBlurDir.xy * u_aoViewport.zw;

    for (int i = -6; i <= 6; ++i) {
        if (i == 0) continue;

        float gaussian = exp(-float(i * i) * 0.15);
        accum += gaussian * sampleBilateral(uv + float(i) * axis, midN, midDepth);
    }

    float ao = accum.x / max(0.04, accum.y);
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
