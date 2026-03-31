// Minimal PBR material + helpers.
// Intended to be included from engine shaders (e.g. NovaUniforms.slang).

struct Material
{
    float4 baseColorFactor;   // linear RGBA
    float4 emissiveFactor;    // xyz used

    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;

    float emissiveStrength;
    float alphaCutoff;
    int   alphaMode;          // 0=Opaque, 1=Mask, 2=Blend (placeholder)
    int   _pad0;
};

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float3 saturate3(float3 v) { return clamp(v, float3(0.0), float3(1.0)); }

// Fresnel Schlick approximation.
float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

// Trowbridge-Reitz GGX normal distribution.
float D_GGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (3.14159265 * denom * denom);
}

// Schlick-GGX geometry term (Smith).
float G_SchlickGGX(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0; // UE4 style
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}

// Simple single-light Cook-Torrance BRDF.
float3 evalPBR(Material mat, float3 N, float3 V, float3 L, float3 lightColor)
{
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float rough = clamp(mat.roughnessFactor, 0.04, 1.0);
    float alpha = rough * rough;

    float3 baseColor = mat.baseColorFactor.rgb;
    float metallic = saturate(mat.metallicFactor);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 F = fresnelSchlick(VdotH, F0);
    float  D = D_GGX(NdotH, alpha);
    float  G = G_Smith(NdotV, NdotL, rough);

    float3 numerator = D * G * F;
    float denom = max(4.0 * NdotV * NdotL, 1e-4);
    float3 spec = numerator / denom;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 diffuse = kD * baseColor / 3.14159265;

    return (diffuse + spec) * lightColor * NdotL;
}

