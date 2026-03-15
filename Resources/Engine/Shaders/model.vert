#version 450
#extension GL_ARB_shading_language_include : require

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Color;
layout(location = 4) in vec3 a_Tangent;
layout(location = 5) in vec3 a_Bitangent;

#include "NovaUniforms.glsl"

layout(location = 0) out vec3 v_Normal;
layout(location = 1) out vec4 v_Color;

void main() {
    mat4 model = NOVA_MODEL;
    vec4 color = u_Color;
    if (NOVA_UseInstancing != 0) {
        model = u_Instances.instances[gl_InstanceIndex].model;
        color = u_Instances.instances[gl_InstanceIndex].color;
    }

    v_Normal = normalize(mat3(transpose(inverse(model))) * a_Normal);
    v_Color = color;
    gl_Position = NOVA_PROJ * NOVA_VIEW * model * vec4(a_Position, 1.0);
}
