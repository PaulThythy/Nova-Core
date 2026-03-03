#version 450

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;
layout(location = 3) in vec3 a_Color;
layout(location = 4) in vec3 a_Tangent;
layout(location = 5) in vec3 a_Bitangent;

// ---- Platform-agnostic MVP block ----
#ifdef NOVA_VULKAN
    layout(push_constant) uniform PushConstants {
        mat4 model;
        mat4 view;
        mat4 proj;
    } u_PC;
    #define NOVA_MODEL u_PC.model
    #define NOVA_VIEW  u_PC.view
    #define NOVA_PROJ  u_PC.proj
#else
    layout(std140, binding = 0) uniform UBO_MVP {
        mat4 model;
        mat4 view;
        mat4 proj;
    } u_PC;
    #define NOVA_MODEL u_PC.model
    #define NOVA_VIEW  u_PC.view
    #define NOVA_PROJ  u_PC.proj
#endif

layout(location = 0) out vec3 v_Color;
layout(location = 1) out vec3 v_Normal;

void main() {
    v_Color  = a_Color;
    v_Normal = normalize(mat3(transpose(inverse(NOVA_MODEL))) * a_Normal);
    gl_Position = NOVA_PROJ * NOVA_VIEW * NOVA_MODEL * vec4(a_Position, 1.0);
}