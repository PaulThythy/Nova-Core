#include "NovaUniforms.slang"

// VertexColor for SceneInstanced.vert.slang: same varyings including the unused
// material index so SPIR-V locations match (avoids the unused-output warning).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
    nointerpolation uint v_MaterialIndex;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    return input.v_Color;
}