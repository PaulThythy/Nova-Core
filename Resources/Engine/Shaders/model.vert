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

// Uniforms explicites pour SPIR-V OpenGL
layout(location = 0) uniform mat4 u_Model; // occupe 0..3
layout(location = 4) uniform mat4 u_View;  // occupe 4..7
layout(location = 8) uniform mat4 u_Proj;  // occupe 8..11

void main()
{
    vec4 worldPos = u_Model * vec4(a_Position, 1.0);
    v_WorldPos = worldPos.xyz;

    mat3 normalMatrix = transpose(inverse(mat3(u_Model)));
    v_Normal = normalize(normalMatrix * a_Normal);

    v_Color = a_Color;

    gl_Position = u_Proj * u_View * worldPos;
}