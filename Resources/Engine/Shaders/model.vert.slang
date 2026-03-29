#include "NovaUniforms.slang"

struct VSIn {
    [[vk::location(0)]] float3 a_Position;
    [[vk::location(1)]] float3 a_Normal;
};

struct VSOut {
    float4 sv_position : SV_Position;
    [[vk::location(0)]] float3 v_Normal;
    [[vk::location(1)]] float4 v_Color;
};

[shader("vertex")]
VSOut main(VSIn input, uint instanceID : SV_InstanceID) {
    float4x4 model = nova.mvp.model;
    float4 color = nova.material.color;
    if (nova.globals.u_UseInstancing != 0) {
        model = nova.instances[instanceID].model;
        color = nova.instances[instanceID].color;
    }

    float3x3 m3 = (float3x3)model;
    float3x3 invT = transpose3x3(inverse3x3(m3));
    float3 n = normalize(mul(invT, input.a_Normal));

    VSOut o;
    o.v_Normal = n;
    o.v_Color = color;
    float4 world = mul(model, float4(input.a_Position, 1.0));
    float4 viewPos = mul(nova.mvp.view, world);
    o.sv_position = mul(nova.mvp.proj, viewPos);
    return o;
}
