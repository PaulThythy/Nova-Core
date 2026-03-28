import glsl;

#include "NovaUniforms.slang"

struct VSIn {
    [[vk::location(0)]] float3 a_Position;
    [[vk::location(1)]] float3 a_Normal;
    [[vk::location(2)]] float2 a_TexCoord;
    [[vk::location(3)]] float3 a_Color;
    [[vk::location(4)]] float3 a_Tangent;
    [[vk::location(5)]] float3 a_Bitangent;
};

struct VSOut {
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float3 v_Normal;
    [[vk::location(1)]] float4 v_Color;
};

[shader("vertex")]
VSOut main(VSIn input, uint instanceID : SV_InstanceID) {
    float4x4 model = u_mvp.model;
    float4 color = u_material.u_Color;
    if (u_globals.u_UseInstancing != 0) {
        model = u_Instances[instanceID].model;
        color = u_Instances[instanceID].color;
    }

    float3x3 m3 = (float3x3)model;
    float3x3 invT = transpose(inverse(m3));
    float3 n = normalize(mul(invT, input.a_Normal));

    VSOut o;
    o.v_Normal = n;
    o.v_Color = color;
    float4 world = mul(model, float4(input.a_Position, 1.0));
    float4 viewPos = mul(u_mvp.view, world);
    o.sv_position = mul(u_mvp.proj, viewPos);
    return o;
}
