vec3 a_position  : POSITION;
vec3 a_normal    : NORMAL;
vec3 a_tangent   : TANGENT;
vec2 a_texcoord0 : TEXCOORD0;
vec4 i_data0     : COLOR0;
vec4 i_data1     : COLOR1;
vec4 i_data2     : COLOR2;
vec4 i_data3     : COLOR3;

vec3 v_worldPos  : TEXCOORD3 = vec3(0.0, 0.0, 0.0);
vec3 v_normal    : TEXCOORD4 = vec3(0.0, 0.0, 1.0);
vec3 v_tangent   : TEXCOORD5 = vec3(1.0, 0.0, 0.0);
vec2 v_uv        : TEXCOORD6 = vec2(0.0, 0.0);
vec2 v_motion    : TEXCOORD7 = vec2(0.0, 0.0);
