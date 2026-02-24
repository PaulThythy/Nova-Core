#version 450 core

layout(location = 0) in vec3 v_WorldPos;
layout(location = 1) in vec3 v_Normal;
layout(location = 2) in vec3 v_Color;

layout(location = 0) out vec4 o_Color;

void main()
{
    vec3 N = normalize(v_Normal);

    // Lumière directionnelle simple
    vec3 L = normalize(vec3(0.5, 1.0, 0.3));
    float ndotl = max(dot(N, L), 0.0);

    // Si la couleur vertex n'est pas renseignée (0,0,0), fallback vers gris clair
    vec3 baseColor = (length(v_Color) > 0.001) ? v_Color : vec3(0.8, 0.8, 0.85);

    float ambient = 0.20;
    vec3 litColor = baseColor * (ambient + ndotl * 0.80);

    o_Color = vec4(litColor, 1.0);
}