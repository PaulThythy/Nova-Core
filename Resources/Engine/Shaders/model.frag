#version 450
#extension GL_ARB_shading_language_include : require

layout(location = 0) in vec3 v_Normal;
layout(location = 1) in vec4 v_Color;

#include "NovaUniforms.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));
    vec3 normal = normalize(v_Normal);
    float diff = max(dot(normal, lightDir), 0.15);
    outColor = vec4(v_Color.rgb * diff, v_Color.a);
}