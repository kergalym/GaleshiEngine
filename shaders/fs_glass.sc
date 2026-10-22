$input v_worldPos, v_normal, v_tangent, v_uv
#include <bgfx_shader.sh>
uniform vec4 u_cameraPosition;
void main()
{
    vec3 N = normalize(v_normal);
    vec3 V = normalize(u_cameraPosition.xyz - v_worldPos.xyz);
    float fresnel = pow(1.0 - max(dot(N, V), 0.0), 3.0);
    // Яркий голубой цвет + альфа-канал
    vec3 glassColor = mix(vec3(0.1, 0.3, 0.5), vec3(1.0), fresnel);
    gl_FragColor = vec4(glassColor, 0.5 + fresnel * 0.5);
}
