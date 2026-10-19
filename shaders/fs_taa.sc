$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_color, 0);
SAMPLER2D(s_history, 1);
SAMPLER2D(s_depth, 2);
uniform mat4 u_prevViewProj;
uniform mat4 u_taaInvViewProj;
uniform vec4 u_taaParams; // x = blend factor
void main() {
    vec2 uv = v_texcoord0;
    vec4 current = texture2D(s_color, uv);
    float depth = texture2D(s_depth, uv).r;
    
    if (depth >= 0.9999) { gl_FragData[0] = current; return; }
    
    // Reproject using depth
    vec4 clip = vec4(uv * 2.0 - 1.0, depth, 1.0);
    vec4 world = mul(u_taaInvViewProj, clip);
    world.xyz /= world.w;
    vec4 prevClip = mul(u_prevViewProj, world);
    vec2 prevUV = (prevClip.xy / prevClip.w) * 0.5 + 0.5;
    
    if (prevUV.x < 0.0 || prevUV.x > 1.0 || prevUV.y < 0.0 || prevUV.y > 1.0) {
        gl_FragData[0] = current; return;
    }
    
    vec4 history = texture2D(s_history, prevUV);
    
    // Neighborhood clamping to reduce ghosting
    vec3 minC = current.rgb, maxC = current.rgb;
    for(int i = -1; i <= 1; ++i) {
        for(int j = -1; j <= 1; ++j) {
            vec3 c = texture2D(s_color, uv + vec2(i, j) * 0.001).rgb;
            minC = min(minC, c);
            maxC = max(maxC, c);
        }
    }
    history.rgb = clamp(history.rgb, minC, maxC);
    
    float blend = u_taaParams.x;
    gl_FragData[0] = mix(current, history, blend);
}
