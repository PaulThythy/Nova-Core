// Engine uniform layout shared with C++ (see RHI_ShaderUniforms.h).
//
// All engine resources are grouped in one ParameterBlock: Slang assigns bindings
// 0, 1, 2, … in field order inside descriptor set 0 — no [[vk::binding]] needed.
//
// Custom shaders (e.g. in Nova-App): #include this file to get `nova`, then add
// your own resources in a *second* descriptor set, e.g.:
//   struct MyData { Texture2D albedo; }
//   ParameterBlock<MyData> user : register(space1);
// Bind set 0 = engine (this file), set 1 = your ParameterBlock in your pipeline.
//
// Do not `import glsl` here: the GLSL module reserves descriptor set 0 for its own
// resources, which would shift this ParameterBlock to set 1 and break Vulkan layouts
// that bind engine data at set 0.

float3x3 transpose3x3(float3x3 m) {
    return float3x3(
        float3(m[0][0], m[1][0], m[2][0]),
        float3(m[0][1], m[1][1], m[2][1]),
        float3(m[0][2], m[1][2], m[2][2])
    );
}

/// 3×3 inverse via cofactors (no `import glsl` / no HLSL `inverse` for float3x3 here).
float3x3 inverse3x3(float3x3 m) {
    float a = m[0][0], b = m[0][1], c = m[0][2];
    float d = m[1][0], e = m[1][1], f = m[1][2];
    float g = m[2][0], h = m[2][1], i = m[2][2];
    float det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g);
    float invDet = 1.0 / det;
    float3 row0 = float3(e * i - f * h, c * h - b * i, b * f - c * e) * invDet;
    float3 row1 = float3(f * g - d * i, a * i - c * g, c * d - a * f) * invDet;
    float3 row2 = float3(d * h - e * g, b * g - a * h, a * e - b * d) * invDet;
    return float3x3(row0, row1, row2);
}

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

struct Material {
    float4 color;
};

struct NovaEngine {
    ConstantBuffer<Globals> globals;
    ConstantBuffer<MVP> mvp;
    StructuredBuffer<Instance> instances;
    ConstantBuffer<Material> material;
};

ParameterBlock<NovaEngine> nova : register(space0);
