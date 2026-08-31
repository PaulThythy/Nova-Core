#include "NovaUniforms.slang"

// Field order and types must match Scene.vert.slang VSOut (excluding SV_Position).
struct PSIn {
    float3 v_Normal;
    float3 v_Pos;
    float4 v_Color;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    float3 N = normalize(input.v_Normal);
    float3 V = normalize(nova.scene.m_CameraPos - input.v_Pos);
    float3 base = nova.material.m_Base * nova.material.m_BaseColor;

    float3 ambient = 0.03 * base;
    float3 lighting = float3(0.0, 0.0, 0.0);

    int numLights = clamp(nova.scene.m_LightCount, 0, MAX_LIGHTS);
    for (int i = 0; i < numLights; ++i) {
        LightGPU light = nova.lights[i];
        float3 L;
        float atten;
        EvalLightVector(light, input.v_Pos, L, atten);
        if (atten <= 0.0)
            continue;

        float3 radianceColor = light.color * light.intensity;
        float shadow = ShadowFactor(
            light, input.v_Pos, N, L, nova.shadowMaps, nova.shadowSampler);
        lighting += evalPBR(nova.material, N, V, L, radianceColor * atten) * shadow;
    }

    float3 emissive = nova.material.m_EmissionColor.rgb * nova.material.m_Emission;
    float3 color = ambient + lighting + emissive;
    return float4(color, 1.0);
}