$input v_normal, v_worldPos

#include <bgfx_shader.sh>

uniform vec4 u_albedo;
uniform vec4 u_lightDir;
uniform vec4 u_lightColor;
uniform vec4 u_cameraPos;

void main()
{
    vec3 N = normalize(v_normal);
    vec3 L = normalize(-u_lightDir.xyz);
    vec3 V = normalize(u_cameraPos.xyz - v_worldPos);
    vec3 H = normalize(L + V);

    float ndl  = max(dot(N, L), 0.0);
    float spec = pow(max(dot(N, H), 0.0), 64.0);

    vec3 ambient  = u_albedo.xyz * 0.15;
    vec3 diffuse  = u_albedo.xyz * ndl * u_lightColor.xyz * u_lightDir.w;
    vec3 specular = vec3(spec) * u_lightColor.xyz * 0.5;

    vec3 color = ambient + diffuse + specular;
    color = color / (color + vec3(1.0));
    color = pow(color, vec3(1.0 / 2.2));

    gl_FragColor = vec4(color, u_albedo.a);
}
