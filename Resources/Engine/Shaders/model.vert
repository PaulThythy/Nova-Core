#version 450 core

layout(location = 0) in vec3 a_Position;
layout(location = 1) in vec3 a_Normal;
layout(location = 2) in vec2 a_TexCoord;   // pas utilisé pour l'instant
layout(location = 3) in vec3 a_Color;
layout(location = 4) in vec3 a_Tangent;    // pas utilisé
layout(location = 5) in vec3 a_Bitangent;  // pas utilisé

layout(location = 0) out vec3 v_WorldPos;
layout(location = 1) out vec3 v_Normal;
layout(location = 2) out vec3 v_Color;

#ifdef NOVA_VULKAN
    // Vulkan : on expose les set= dans les layouts
    #define NOVA_SET(x)            set = x,
    #define NOVA_PUSH_CONSTANT     layout(push_constant) uniform
#else
    // OpenGL SPIR-V : set= n'existe pas, on l'ignore
    #define NOVA_SET(x)
    #define NOVA_PUSH_CONSTANT     layout(std140, binding = 15) uniform
#endif

// UBO  : NOVA_UBO(set, binding) uniform BlockName { ... };
#define NOVA_UBO(setIdx, bindIdx) \
    layout(std140, NOVA_SET(setIdx) binding = bindIdx) uniform

// SSBO : NOVA_SSBO(set, binding) buffer BlockName { ... };
#define NOVA_SSBO(setIdx, bindIdx) \
    layout(std430, NOVA_SET(setIdx) binding = bindIdx) buffer

NOVA_UBO(0, 0) UBO_PerObject
{
    mat4 u_Model;
    mat4 u_View;
    mat4 u_Proj;
};

void main()
{
    vec4 worldPos = u_Model * vec4(a_Position, 1.0);
    v_WorldPos    = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(u_Model)));
    v_Normal = normalize(normalMatrix * a_Normal);

    v_Color     = a_Color;
    gl_Position = u_Proj * u_View * worldPos;
}