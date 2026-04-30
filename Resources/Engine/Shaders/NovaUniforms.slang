// Engine uniform layout shared with C++ (see RHI_ShaderUniforms.h).
//
// All engine resources are grouped in one ParameterBlock: Slang assigns bindings
// 0, 1, 2, … in field order inside descriptor set 0 — no [[vk::binding]] needed.
//
// Custom shaders (e.g. in Nova-App): #include this file to get `nova`, then add
// your own resources in a *second* descriptor set, e.g.:
//   struct MyData { Texture2D albedo; }
//   [[vk::binding(0, 1)]] ParameterBlock<MyData> user;
// Bind set 0 = engine (this file), set 1 = your ParameterBlock in your pipeline.
//
// Do not `import glsl` here: the GLSL module reserves descriptor set 0 for its own
// resources, which would shift this ParameterBlock to set 1 and break Vulkan layouts
// that bind engine data at set 0.

#include "Globals.slang"
#include "Material.slang"

// Frame uniforms (time, resolution, inputs)
struct FrameUniforms {
    float3 iResolution;
    float _padAfterRes;
    float iTime;
    float iTimeDelta;
    float iFrameRate;
    int iFrame;
    int u_UseInstancing;
    int2 _Offset0;
    float3 u_CameraPos;
    float _padAlignMouse;
    float4 iMouse;
    float4 iDate;
};

struct MVP {
    float4x4 model;
    float4x4 view;
    float4x4 proj;
    float4x4 viewProj;
    float4x4 invViewProj;
};

struct Instance {
    float4x4 model;
    float4 color;
};

struct NovaEngine {
    ConstantBuffer<FrameUniforms> frame;
    ConstantBuffer<MVP> mvp;
    StructuredBuffer<Instance> instances;
    ConstantBuffer<Material> material;
};

[[vk::binding(0, 0)]] ParameterBlock<NovaEngine> nova;