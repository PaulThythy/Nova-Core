#include "NovaUniforms.slang"

// Instanced Scene.vert.slang: the model matrix and material index come from
// `nova.instances[SV_VulkanInstanceID]` (includes the draw's firstInstance) instead of
// the per-draw `nova.model`.
//
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

// Scene.vert.slang VSOut + the material index last, so fragment shaders written against
// Scene.vert.slang (e.g. VertexColor.frag.slang) still match the first locations.
struct VSOut {
    float4 sv_position : SV_Position;
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
    nointerpolation uint v_MaterialIndex;
};

[shader("vertex")]
VSOut main(VSIn input, uint instanceIndex : SV_VulkanInstanceID) {
    InstanceGPU instance = nova.instances[instanceIndex];
    float4x4 model = instance.m_Model;

    float3x3 m3 = (float3x3)model;
    float3x3 invT = transpose3x3(inverse3x3(m3));
    float3 n = normalize(mul(invT, input.a_Normal));

    VSOut o;
    o.v_Normal = n;
    o.v_Color = float4(input.a_Color, 1.0);
    float4 world = mul(model, float4(input.a_Position, 1.0));
    o.v_Pos = world.xyz;
    float4 viewPos = mul(nova.scene.uniforms.m_Camera.m_View, world);
    o.sv_position = mul(nova.scene.uniforms.m_Camera.m_Proj, viewPos);
    o.v_MaterialIndex = instance.m_MaterialIndex;
    return o;
}