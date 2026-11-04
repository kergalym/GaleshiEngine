$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);

vec3 srgbToLinear(vec3 c)
{
    return mix(c / 12.92,
               pow((c + 0.055) / 1.055, vec3_splat(2.4)),
               step(vec3_splat(0.04045), c));
}

void main()
{
    // Scene textures are already linear HDR. ImGui style colors are authored
    // in sRGB, so decode them before the sRGB backbuffer re-encodes the result.
    vec4 color = v_color0;
    color.rgb = srgbToLinear(color.rgb);
    gl_FragColor = texture2D(s_texColor, v_texcoord0) * color;
}
