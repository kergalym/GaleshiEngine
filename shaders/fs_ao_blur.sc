$input v_texcoord0
#include <bgfx_shader.sh>

// Separable bilateral blur for the full-resolution AO buffer. One pass is
// horizontal and the next is vertical.

SAMPLER2D(s_aoSource,    0);
SAMPLER2D(s_worldPos,    1);
SAMPLER2D(s_normalRough, 2);

uniform mat4 u_aoView;
uniform vec4 u_aoViewport;   // xy=full resolution, zw=1/full resolution
uniform vec4 u_aoBlurParams; // x=normal factor, y=depth factor
uniform vec4 u_aoBlurDir;    // xy=blur direction

float linearDepth(vec3 worldPos)
{
    return -mul(u_aoView, vec4(worldPos, 1.0)).z;
}

vec2 bilateralTap(vec2 uv, vec3 centerNormal, float centerDepth)
{
    vec4 world = texture2D(s_worldPos, uv);
    if (world.w < 0.5) return vec2(0.0);

    vec3 normal = normalize(texture2D(s_normalRough, uv).xyz * 2.0 - 1.0);
    float depth = linearDepth(world.xyz);
    float normalWeight =
        1.0 - saturate(u_aoBlurParams.x * distance(normal, centerNormal));
    float depthWeight =
        1.0 - saturate(u_aoBlurParams.y * abs(depth - centerDepth) * 3.0);
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

    vec3 centerNormal =
        normalize(texture2D(s_normalRough, uv).xyz * 2.0 - 1.0);
    float centerDepth = linearDepth(midWp.xyz);
    vec2 total = vec2(texture2D(s_aoSource, uv).r, 1.0);
    vec2 axis = u_aoBlurDir.xy * u_aoViewport.zw;

    for (int tap = -6; tap <= 6; ++tap) {
        if (tap == 0) continue;
        float weight = exp(-float(tap * tap) * 0.15);
        total += weight * bilateralTap(
            uv + float(tap) * axis, centerNormal, centerDepth);
    }

    float ao = total.x / max(0.04, total.y);
    gl_FragColor = vec4(ao, ao, ao, 1.0);
}
