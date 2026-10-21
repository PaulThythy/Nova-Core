#include "NovaUniforms.slang"

// Depth-only shadow pass: world matrix from `nova.instances`, light projection from
// `nova.scene.lights[nova.scene.uniforms.m_LightIndex]`.

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
};

[shader("vertex")]
VSOut main(VSIn input, uint instanceIndex : SV_VulkanInstanceID) {
    InstanceGPU instance = nova.instances[instanceIndex];
    float4 world = mul(instance.m_Model, float4(input.a_Position, 1.0));

    int lightIndex = clamp(nova.scene.uniforms.m_LightIndex, 0, MAX_LIGHTS - 1);
    float4x4 lightViewProj = nova.scene.lights[lightIndex].lightViewProj;

    VSOut o;
    o.sv_position = mul(lightViewProj, world);
    return o;
}