// Engine uniform layout shared with C++ (see RHI_ShaderUniforms.h).
//
// This global `ParameterBlock` uses Slang automatic layout only: no [[vk::binding]].
// As the first (and usually only) top-level block in engine shaders, it maps to Vulkan
// set 0; fields inside `NovaEngine` get bindings 0, 1, 2, … in field order.
//
// Custom shaders: #include this file, then declare a second block *after* `nova`
// (same file or later in link order) so it gets the next descriptor space (set 1), e.g.:
//   struct MyData { Texture2D albedo; }
//   ParameterBlock<MyData> user;
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

ParameterBlock<NovaEngine> nova;