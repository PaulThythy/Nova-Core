// Engine uniform layout shared with C++ (see RHI_ShaderUniforms.h).
//
// This global `ParameterBlock` uses Slang automatic layout only: no [[vk::binding]].
// As the first (and usually only) top-level block in engine shaders, it maps to Vulkan
// set 0; fields inside `NovaEngine` get bindings 0, 1, 2, … in field order.
//
// `FrameUniforms` is a Vulkan push constant (small, updated every frame). It lives
// outside the ParameterBlock so it does not consume a descriptor binding.
// Access as `frame.m_Time`, etc. — not `nova.frame`.
//
// Custom shaders: #include this file, then declare a second block *after* `nova`
// (same file or later in link order) so it gets the next descriptor space (set 1), e.g.:
//   struct OutlineData { Texture2D selectionMask; SamplerState selectionMaskSampler; }
//   ParameterBlock<OutlineData> outline;
//
// Do not `import glsl` here: the GLSL module reserves descriptor set 0 for its own
// resources, which would shift this ParameterBlock to set 1 and break Vulkan layouts
// that bind engine data at set 0.

#include "Globals.slang"
#include "Material.slang"
#include "Light.slang"

// Per-frame globals (time, resolution, inputs) — push constants (std430).
struct FrameUniforms {
    float3 m_Resolution;
    float _padAfterRes;
    float m_Time;
    float m_TimeDelta;
    float m_FrameRate;
    int m_Frame;
    int _padAfterFrame;
    float4 m_Mouse;
    float4 m_Date;
};

[[vk::push_constant]]
ConstantBuffer<FrameUniforms> frame;

// Scene-level data (camera, light count)
struct SceneUniforms {
    float3 m_CameraPos;
    int m_LightCount;
    float _padAfterLightCount;
};

struct MVP {
    float4x4 m_Model;
    float4x4 m_View;
    float4x4 m_Proj;
    float4x4 m_ViewProj;
    float4x4 m_InvViewProj;
};

struct NovaEngine {
    ConstantBuffer<SceneUniforms> scene;
    ConstantBuffer<MVP> mvp;
    ConstantBuffer<Material> material;
    StructuredBuffer<LightGPU> lights;
    Texture2DArray shadowMaps;
    SamplerComparisonState shadowSampler;
};

ParameterBlock<NovaEngine> nova;