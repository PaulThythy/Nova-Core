#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

static const float3 lightPosition = float3(2.0, 2.0, 2.0);
static const float3 lightColor = float3(1.0, 1.0, 1.0);
static const float lightIntensity = 3.0;

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    float3 N = normalize(input.v_Normal);
    float3 V = normalize(nova.frame.u_CameraPos - input.v_Pos);
    float3 base = nova.material.base * nova.material.baseColor;

    float3 ambient = 0.03 * base;

    float3 toLight = lightPosition - input.v_Pos;
    float distSq = max(dot(toLight, toLight), 1e-4);
    float dist = sqrt(distSq);
    float3 L = toLight / dist;
    float attenuation = 1.0 / (1.0 + 0.09 * dist + 0.032 * distSq);
    float3 radiance = evalPBR(nova.material, N, V, L, lightColor * lightIntensity * attenuation);

    float3 emissive = nova.material.emissionColor.rgb * nova.material.emission;

    float3 color = ambient + radiance + emissive;
    return float4(color, 1.0);
}