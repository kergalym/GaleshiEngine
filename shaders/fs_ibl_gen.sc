$input v_texcoord0
#include <bgfx_shader.sh>

uniform vec4 u_iblParams; // x = roughness (0=env, 1=irradiance)
uniform vec4 u_lightDirection;

vec3 getSkyColor(vec3 dir) {
    vec3 skyColor = mix(vec3(0.4, 0.6, 1.0), vec3(0.05, 0.1, 0.3), clamp(dir.y, 0.0, 1.0));
    float sun = max(dot(dir, normalize(-u_lightDirection.xyz)), 0.0);
    skyColor += vec3(1.0, 0.9, 0.8) * pow(sun, 128.0) * 10.0;
    return skyColor;
}

void main() {
    vec2 uv = v_texcoord0;
    // Simple spherical direction from UV
    float theta = uv.y * 3.14159265;
    float phi = (uv.x - 0.5) * 2.0 * 3.14159265;
    vec3 dir = vec3(sin(theta) * cos(phi), cos(theta), sin(theta) * sin(phi));

    if (u_iblParams.x > 0.5) {
        // Irradiance (simple 5-sample blur)
        vec3 col = getSkyColor(dir);
        col += getSkyColor(normalize(dir + vec3(0.2, 0.0, 0.0)));
        col += getSkyColor(normalize(dir - vec3(0.2, 0.0, 0.0)));
        col += getSkyColor(normalize(dir + vec3(0.0, 0.2, 0.0)));
        col += getSkyColor(normalize(dir - vec3(0.0, 0.2, 0.0)));
        gl_FragColor = vec4(col / 5.0, 1.0);
    } else {
        gl_FragColor = vec4(getSkyColor(dir), 1.0);
    }
}
