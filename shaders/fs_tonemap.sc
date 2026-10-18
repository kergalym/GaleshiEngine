$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
uniform vec4 u_colorParams;
vec3 ACESFilm(vec3 x) {
    float a = 2.51; float b = 0.03; float c = 2.43; float d = 0.59; float e = 0.14;
    return clamp((x * (a * x + b)) / (x * (c * x + d) + e), 0.0, 1.0);
}
void main() {
    vec3 c = texture2D(s_color, v_texcoord0).rgb;
    c *= pow(2.0, u_colorParams.x);
    c = ACESFilm(c);
    float luma = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(luma), c, u_colorParams.z);
    c = (c - 0.5) * u_colorParams.y + 0.5;
    c = pow(max(c, vec3(0.0)), vec3(1.0/2.2));
    gl_FragColor = vec4(c, 1.0);
}
