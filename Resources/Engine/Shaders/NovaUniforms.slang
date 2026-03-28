// Aligné sur RHI_ShaderUniforms.h (UBO / SSBO bindings 0–3)

struct Globals {
    float3 iResolution;
    float _padAfterRes;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    int iFrame;
    int u_UseInstancing;
    int2 _Offset0;
    float _padAlignMouse;
    float4 iMouse;
    float4 iDate;
};

[[vk::binding(0)]]
ConstantBuffer<Globals> u_globals;

struct UBO_MVP {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 invViewProj;
};

[[vk::binding(1)]]
ConstantBuffer<UBO_MVP> u_mvp;

struct SSBO_InstanceData {
    float4x4 model;
    float4 color;
};

[[vk::binding(2)]]
StructuredBuffer<SSBO_InstanceData> u_Instances;

struct UBO_Material {
    float4 u_Color;
};

[[vk::binding(3)]]
ConstantBuffer<UBO_Material> u_material;
