#version 450
#extension GL_ARB_shading_language_include : require
// or GL_GOOGLE_include_directive

#include "triangle.glsl"

void main() { gl_Position = vec4(kTrianglePos[gl_VertexIndex], 0.0, 1.0); }