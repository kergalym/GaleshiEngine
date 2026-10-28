$input v_uv, v_color0

#include <bgfx_shader.sh>

void main()
{
    // Soft round sprite: alpha falls off towards the edges and the core is
    // slightly brighter to fake an emissive centre.
    vec2 centered = v_uv * 2.0 - 1.0;
    float dist = length(centered);
    float alpha = smoothstep(1.0, 0.15, dist);
    float core = smoothstep(0.65, 0.0, dist) * 0.35;
    vec3 rgb = v_color0.rgb + core * v_color0.rgb;
    gl_FragColor = vec4(rgb, v_color0.a * alpha);
}
