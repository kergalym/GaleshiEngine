$input v_uv, v_color0

#include <bgfx_shader.sh>

uniform vec4 u_particleParams; // x: 1 = sample texture, 0 = procedural sprite
SAMPLER2D(s_particleTex, 0);

void main()
{
    // Procedural soft round sprite: alpha falls off towards the edges and the
    // core is slightly brighter to fake an emissive centre.
    vec2 centered = v_uv * 2.0 - 1.0;
    float dist = length(centered);
    float alpha = smoothstep(1.0, 0.15, dist);
    float core = smoothstep(0.65, 0.0, dist) * 0.35;
    vec3 proceduralRgb = v_color0.rgb + core * v_color0.rgb;
    float proceduralAlpha = v_color0.a * alpha;

    // Textured sprite: RGB and alpha come from the texture, modulated by the
    // particle colour so per-particle tinting still works.
    vec4 texSample = texture2D(s_particleTex, v_uv);
    vec3 texturedRgb = texSample.rgb * v_color0.rgb;
    float texturedAlpha = texSample.a * v_color0.a;

    float useTexture = step(0.5, u_particleParams.x);
    vec3 rgb = mix(proceduralRgb, texturedRgb, useTexture);
    float finalAlpha = mix(proceduralAlpha, texturedAlpha, useTexture);

    gl_FragColor = vec4(rgb, finalAlpha);
}
