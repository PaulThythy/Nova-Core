#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
    nointerpolation uint v_MaterialIndex;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    MaterialGPU material = nova.materials[input.v_MaterialIndex];
    float3 base = material.m_Base * material.m_BaseColor;
    float3 emissive = material.m_EmissionColor.rgb * material.m_Emission;
    return float4(base + emissive, 1.0);
}