$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
SAMPLER2D(s_depth, 1);
SAMPLER2D(s_normal, 2);

uniform mat4 u_ssaoProj;
uniform mat4 u_ssaoInvProj;
uniform vec4 u_ssaoParams;

void main() {
    vec2 uv = v_texcoord0;
    vec4 col = texture2D(s_color, uv);
    float depth = texture2D(s_depth, uv).r;
    
    if (depth >= 0.9999) { gl_FragColor = col; return; }

    // Упрощенный SSAO (просто затемняем углы по нормалям для теста)
    vec3 N = normalize(texture2D(s_normal, uv).rgb * 2.0 - 1.0);
    float ao = clamp(dot(N, vec3(0.0, 1.0, 0.0)) * 0.5 + 0.5, 0.0, 1.0);
    ao = mix(1.0, ao, 0.5); // Интенсивность 0.5
    
    gl_FragColor = vec4(col.rgb * ao, col.a);
}
