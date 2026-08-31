// from https://github.com/Autodesk/Aurora/blob/main/Libraries/Aurora/Source/Shaders/Material.slang

#ifndef __MATERIAL_H__
#define __MATERIAL_H__

#include "Globals.slang"

struct Material
{
    float m_Base;
    float3 m_BaseColor;
    float m_DiffuseRoughness;
    float m_Metalness;
    float3 m_MetalColor;
    float m_Specular;
    float3 m_SpecularColor;
    float m_SpecularRoughness;
    float m_SpecularIOR;
    float m_SpecularAnisotropy;
    float m_SpecularRotation;
    float m_Transmission;
    float3 m_TransmissionColor;
    float m_Subsurface;
    float3 m_SubsurfaceColor;
    float3 m_SubsurfaceRadius;
    float m_SubsurfaceScale;
    float m_SubsurfaceAnisotropy;
    float m_Sheen;
    float3 m_SheenColor;
    float m_SheenRoughness;
    float m_Coat;
    float3 m_CoatColor;
    float m_CoatRoughness;
    float m_CoatAnisotropy;
    float m_CoatRotation;
    float m_CoatIOR;
    float m_CoatAffectColor;
    float m_CoatAffectRoughness;
    float m_Emission;
    float3 m_EmissionColor;
    float3 m_Opacity;
    bool m_ThinWalled;
    bool m_IsOpaque;
    uint2 m_PadCbufferAlign;
};

Material defaultMaterial()
{
    Material material;
    material.m_Base                 = 0.8f;
    material.m_BaseColor            = float3(WHITE, WHITE, WHITE);
    material.m_DiffuseRoughness     = 0.0f;
    material.m_Metalness            = 0.0f;
    material.m_MetalColor           = float3(WHITE, WHITE, WHITE);
    material.m_Specular             = 1.0f;
    material.m_SpecularColor        = float3(WHITE, WHITE, WHITE);
    material.m_SpecularRoughness    = 0.2f;
    material.m_SpecularIOR          = 1.5f;
    material.m_SpecularAnisotropy   = 0.0f;
    material.m_SpecularRotation     = 0.0f;
    material.m_Transmission         = 0.0f;
    material.m_TransmissionColor    = float3(WHITE, WHITE, WHITE);
    material.m_Subsurface           = 0.0f;
    material.m_SubsurfaceColor      = float3(WHITE, WHITE, WHITE);
    material.m_SubsurfaceRadius     = float3(WHITE, WHITE, WHITE);
    material.m_SubsurfaceScale      = 1.0f;
    material.m_SubsurfaceAnisotropy = 0.0f;
    material.m_Sheen                = 0.0f;
    material.m_SheenColor           = float3(WHITE, WHITE, WHITE);
    material.m_SheenRoughness       = 0.3f;
    material.m_Coat                 = 0.0f;
    material.m_CoatColor            = float3(WHITE, WHITE, WHITE);
    material.m_CoatRoughness        = 0.1f;
    material.m_CoatAnisotropy       = 0.0f;
    material.m_CoatRotation         = 0.0f;
    material.m_CoatIOR              = 1.5f;
    material.m_CoatAffectColor      = 0.0f;
    material.m_CoatAffectRoughness  = 0.0f;
    material.m_Emission             = 0.0f;
    material.m_EmissionColor        = float3(WHITE, WHITE, WHITE);
    material.m_Opacity              = float3(WHITE, WHITE, WHITE);
    material.m_ThinWalled           = false;
    material.m_IsOpaque             = true;
    material.m_PadCbufferAlign      = uint2(0u, 0u);
    return material;
}

float saturate(float x) { return clamp(x, 0.0, 1.0); }
float3 saturate3(float3 v) { return clamp(v, float3(0.0), float3(1.0)); }

float3 fresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float D_GGX(float NdotH, float alpha)
{
    float a2 = alpha * alpha;
    float denom = (NdotH * NdotH) * (a2 - 1.0) + 1.0;
    return a2 / (M_PI * denom * denom);
}

float G_SchlickGGX(float NdotV, float k)
{
    return NdotV / (NdotV * (1.0 - k) + k);
}

float G_Smith(float NdotV, float NdotL, float roughness)
{
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    return G_SchlickGGX(NdotV, k) * G_SchlickGGX(NdotL, k);
}


float3 evalPBR(Material mat, float3 N, float3 V, float3 L, float3 lightColor)
{
    float3 H = normalize(V + L);

    float NdotL = saturate(dot(N, L));
    float NdotV = saturate(dot(N, V));
    float NdotH = saturate(dot(N, H));
    float VdotH = saturate(dot(V, H));

    float rough = clamp(mat.m_SpecularRoughness, 0.04, 1.0);
    float alpha = rough * rough;

    float3 baseColor = mat.m_Base * mat.m_BaseColor;
    float metallic = saturate(mat.m_Metalness);

    float3 F0 = lerp(float3(0.04, 0.04, 0.04), baseColor, metallic);
    float3 F = fresnelSchlick(VdotH, F0);
    float  D = D_GGX(NdotH, alpha);
    float  G = G_Smith(NdotV, NdotL, rough);

    float3 numerator = D * G * F;
    float denom = max(4.0 * NdotV * NdotL, 1e-4);
    float3 spec = numerator / denom;

    float3 kS = F;
    float3 kD = (1.0 - kS) * (1.0 - metallic);

    float3 diffuse = kD * baseColor / M_PI;

    return (diffuse + spec) * lightColor * NdotL;
}

#endif // __MATERIAL_H__