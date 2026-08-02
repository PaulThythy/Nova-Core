#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    // Raw depth-buffer values are nonlinear and sit near 1.0 for most of the scene,
    // which looks solid white. Visualize linear view-space distance instead.
    float viewZ = -mul(nova.mvp.view, float4(input.v_Pos, 1.0)).z;
    // Remap to a readable grayscale (matches typical editor far plane ~100).
    float t = saturate(viewZ / 50.0);
    return float4(t, t, t, 1.0);
}