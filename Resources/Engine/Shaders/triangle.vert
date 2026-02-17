#version 450

#ifndef NV_CLIP_Y_FLIP
    #define NV_CLIP_Y_FLIP 1.0
#endif

vec2 pos[3] = vec2[](
    vec2( 0.0, -0.5),
    vec2( 0.5,  0.5),
    vec2(-0.5,  0.5)
);

void main()
{
    vec2 p = pos[gl_VertexIndex];
    p.y *= NV_CLIP_Y_FLIP;
    gl_Position = vec4(p, 0.0, 1.0);
}