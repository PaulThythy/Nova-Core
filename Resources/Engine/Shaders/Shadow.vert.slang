#include "NovaUniforms.slang"

struct VSIn {
    float3 a_Position;
    float3 a_Normal;
    float2 a_TexCoord;
    float3 a_Color;
    float3 a_Tangent;
    float3 a_Bitangent;
};

[shader("vertex")]
float4 main(VSIn input) : SV_Position {
    float4 world = mul(nova.model.m_Model, float4(input.a_Position, 1.0));
    float4x4 lightViewProj = nova.scene.lights[nova.model.m_LightIndex].lightViewProj;
    return mul(lightViewProj, world);
}