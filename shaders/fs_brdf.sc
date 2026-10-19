$input v_texcoord0
#include <bgfx_shader.sh>

void main() {
    vec2 uv = v_texcoord0;
    float NdotV = uv.x;
    float roughness = uv.y;

    // Simplified BRDF approximation (Fresnel + roughness curve)
    float A = 1.0 - roughness * 0.5;
    float B = roughness * roughness * 0.3;

    gl_FragColor = vec4(A, B, 0.0, 1.0);
}
