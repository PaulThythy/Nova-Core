#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

// Temporary App-owned directional light (bound from AppLayer via CreateConstantBuffer).
// Declared after `nova` so Slang assigns the next descriptor space (set 1).
struct DirectionalLight {
    float3 direction; // light travel direction (world space); L = -normalize(direction)
    float intensity;
    float3 color;
    float _pad;
};

struct AppResources {
    ConstantBuffer<DirectionalLight> light;
};

ParameterBlock<AppResources> user;

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    float3 N = normalize(input.v_Normal);
    float3 V = normalize(nova.frame.u_CameraPos - input.v_Pos);
    float3 base = nova.material.base * nova.material.baseColor;

    float3 ambient = 0.03 * base;

    float3 L = normalize(-user.light.direction);
    float3 radiance = evalPBR(nova.material, N, V, L, user.light.color * user.light.intensity);

    float3 emissive = nova.material.emissionColor.rgb * nova.material.emission;

    float3 color = ambient + radiance + emissive;
    return float4(color, 1.0);
}