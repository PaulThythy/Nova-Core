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

// Camera matrices + world-space position (uploaded once per scene via PushSceneUniforms).
struct CameraGPU {
    float4x4 m_View;
    float4x4 m_Proj;
    float4x4 m_ViewProj;
    float4x4 m_InvViewProj;
    float3 m_Position;
    float _pad;
};

// Scene-level constants (camera + light count).
struct SceneUniforms {
    CameraGPU m_Camera;
    int m_LightCount;
    float _padAfterLightCount;
};

// Per-draw model matrix. Lit / editor draws read the camera from `nova.scene.uniforms`.
// Shadow pass reads `lightViewProj` from `nova.scene.lights[m_LightIndex]`.
struct ModelUniforms {
    float4x4 m_Model;
    int m_LightIndex;
    float3 _padLightIndex;
};

// Scene resources: camera/light-count UBO + light list SSBO.
struct SceneParameterBlock {
    ConstantBuffer<SceneUniforms> uniforms;
    StructuredBuffer<LightGPU> lights;
};

struct NovaEngine {
    SceneParameterBlock scene;
    ConstantBuffer<ModelUniforms> model;
    ConstantBuffer<MaterialGPU> material;
    Texture2DArray shadowMaps;
    SamplerComparisonState shadowSampler;
};

ParameterBlock<NovaEngine> nova;