#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
// SceneInstanced.vert.slang appends v_MaterialIndex after these, so locations still match.
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    return input.v_Color;
}