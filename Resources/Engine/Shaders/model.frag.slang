struct PSIn {
    [[vk::location(0)]] float3 v_Normal;
    [[vk::location(1)]] float4 v_Color;
};

[shader("fragment")]
float4 main(PSIn input) : SV_Target0 {
    float3 lightDir = normalize(float3(1.0, 2.0, 1.0));
    float3 normal = normalize(input.v_Normal);
    float diff = max(dot(normal, lightDir), 0.15);
    return float4(input.v_Color.rgb * diff, input.v_Color.a);
}
