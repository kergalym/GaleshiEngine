$input v_worldPos, v_normal, v_uv
#include <bgfx_shader.sh>
SAMPLER2D(s_albedoMap, 0);
SAMPLER2D(s_metallicRoughnessMap, 1);
uniform vec4 u_albedoFactor;
uniform vec4 u_metallicFactor;
uniform vec4 u_roughnessFactor;
void main()
{
    vec4 a  = texture2D(s_albedoMap, v_uv);
    vec4 mr = texture2D(s_metallicRoughnessMap, v_uv);
    vec3 albedo    = a.rgb * u_albedoFactor.rgb;
    float metallic = mr.b * u_metallicFactor.r;
    float rough    = mr.g * u_roughnessFactor.r;
    vec3 N = normalize(v_normal);

    gl_FragData[0] = vec4(albedo, metallic);
    gl_FragData[1] = vec4(N * 0.5 + 0.5, rough);
    gl_FragData[2] = vec4(v_worldPos, 1.0);
    gl_FragData[3] = vec4(0.0);   // some bgfx versions need explicit padding
}
