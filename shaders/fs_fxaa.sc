$input v_texcoord0
#include <bgfx_shader.sh>
SAMPLER2D(s_texColor, 0);
uniform vec4 u_texelSize;
vec3 s(vec2 uv){return texture2D(s_texColor,uv).rgb;}
float lum(vec3 c){return dot(c,vec3(0.299,0.587,0.114));}
void main(){
    vec2 t = u_texelSize.xy;
    vec3 M=s(v_texcoord0);
    float lM=lum(M);
    float lNW=lum(s(v_texcoord0+vec2(-1,-1)*t));
    float lNE=lum(s(v_texcoord0+vec2( 1,-1)*t));
    float lSW=lum(s(v_texcoord0+vec2(-1, 1)*t));
    float lSE=lum(s(v_texcoord0+vec2( 1, 1)*t));
    float lMin=min(lM,min(min(lNW,lNE),min(lSW,lSE)));
    float lMax=max(lM,max(max(lNW,lNE),max(lSW,lSE)));
    if(lMax-lMin < max(0.0312, lMax*0.125)){ gl_FragColor=vec4(M,1.0); return; }
    vec2 dir=vec2(-((lNW+lNE)-(lSW+lSE)), ((lNW+lSW)-(lNE+lSE)));
    float rcp=1.0/(min(abs(dir.x),abs(dir.y))+max((lNW+lNE+lSW+lSE)*0.015625,0.00390625));
    dir=clamp(dir*rcp,vec2(-8,-8),vec2(8,8))*t;
    vec3 A=0.5*(s(v_texcoord0+dir*vec2(1.0/3.0-0.5))+s(v_texcoord0+dir*vec2(2.0/3.0-0.5)));
    vec3 B=A*0.5+0.25*(s(v_texcoord0-dir*0.5)+s(v_texcoord0+dir*0.5));
    gl_FragColor=vec4((lum(B)<lMin||lum(B)>lMax)?A:B,1.0);
}
