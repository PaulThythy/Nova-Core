#version 450
#extension GL_ARB_shading_language_include : require

layout(location = 0) in vec3 v_Normal;

#include "NovaUniforms.glsl"

layout(location = 0) out vec4 outColor;

void main() {
    vec3 lightDir = normalize(vec3(1.0, 2.0, 1.0));
    vec3 normal = normalize(v_Normal);
    float diff = max(dot(normal, lightDir), 0.15);
    outColor = vec4(u_Color.rgb * diff, u_Color.a);
}