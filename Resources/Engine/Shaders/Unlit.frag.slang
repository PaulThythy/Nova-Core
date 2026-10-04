#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    float3 base = nova.material.m_Base * nova.material.m_BaseColor;
    float3 emissive = nova.material.m_EmissionColor.rgb * nova.material.m_Emission;
    return float4(base + emissive, 1.0);
}