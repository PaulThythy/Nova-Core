#include "NovaUniforms.slang"

// Vertex locations and VS->FS varyings: Slang assigns SPIR-V locations in field order
// (must match VkVertexInputAttributeDescription order in C++ and PSIn field order).
struct VSIn {
    float3 a_Position;
    float3 a_Normal;
    float2 a_TexCoord;
    float3 a_Color;
    float3 a_Tangent;
    float3 a_Bitangent;
};

struct VSOut {
    float4 sv_position : SV_Position;
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

[shader("vertex")]
VSOut main(VSIn input, uint instanceID : SV_InstanceID) {
    float4x4 model = nova.mvp.model;
    float4 color = float4(nova.material.baseColor, 1.0);
    if (nova.frame.u_UseInstancing != 0) {
        model = nova.instances[instanceID].model;
        color = nova.instances[instanceID].color;
    }

    float3x3 m3 = (float3x3)model;
    float3x3 invT = transpose3x3(inverse3x3(m3));
    float3 n = normalize(mul(invT, input.a_Normal));

    VSOut o;
    o.v_Normal = n;
    o.v_Color = float4(input.a_Color, 1.0);
    float4 world = mul(model, float4(input.a_Position, 1.0));
    o.v_Pos = world.xyz;
    float4 viewPos = mul(nova.mvp.view, world);
    o.sv_position = mul(nova.mvp.proj, viewPos);
    return o;
}
