#include "NovaUniforms.slang"

struct PSIn {
    [[vk::location(0)]] float3 v_Normal;
    [[vk::location(1)]] float3 v_Pos;
    [[vk::location(2)]] float4 v_Color;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    // Simple directional light in world space.
    float3 L = normalize(float3(1.0, 2.0, 1.0));
    float3 N = normalize(input.v_Normal);
    float3 V = normalize(nova.globals.u_CameraPos - input.v_Pos);

    float3 lightColor = float3(4.0, 4.0, 4.0);
    float3 ambient = 0.03 * nova.material.baseColorFactor.rgb;

    float3 radiance = evalPBR(nova.material, N, V, L, lightColor);
    float3 emissive = nova.material.emissiveFactor.rgb * nova.material.emissiveStrength;

    float3 color = ambient + radiance + emissive;
    return float4(color, nova.material.baseColorFactor.a);
}