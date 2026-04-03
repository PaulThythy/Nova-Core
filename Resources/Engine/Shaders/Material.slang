// from https://github.com/Autodesk/Aurora/blob/main/Libraries/Aurora/Source/Shaders/Material.slang

#ifndef __MATERIAL_H__
#define __MATERIAL_H__

#include "Globals.slang"

struct Material
{
    float base;
    float3 baseColor;
    float diffuseRoughness;
    float metalness;
    float3 metalColor;
    float specular;
    float3 specularColor;
    float specularRoughness;
    float specularIOR;
    float specularAnisotropy;
    float specularRotation;
    float transmission;
    float3 transmissionColor;
    float subsurface;
    float3 subsurfaceColor;
    float3 subsurfaceRadius;
    float subsurfaceScale;
    float subsurfaceAnisotropy;
    float sheen;
    float3 sheenColor;
    float sheenRoughness;
    float coat;
    float3 coatColor;
    float coatRoughness;
    float coatAnisotropy;
    float coatRotation;
    float coatIOR;
    float coatAffectColor;
    float coatAffectRoughness;
    float emission;
    float3 emissionColor;
    float3 opacity;
    bool thinWalled;
    bool isOpaque;
    uint2 _padCbufferAlign;
};

Material defaultMaterial()
{
    Material material;
    material.base                 = 0.8f;
    material.baseColor            = float3(WHITE, WHITE, WHITE);
    material.diffuseRoughness     = 0.0f;
    material.metalness            = 0.0f;
    material.metalColor           = float3(WHITE, WHITE, WHITE);
    material.specular             = 1.0f;
    material.specularColor        = float3(WHITE, WHITE, WHITE);
    material.specularRoughness    = 0.2f;
    material.specularIOR          = 1.5f;
    material.specularAnisotropy   = 0.0f;
    material.specularRotation     = 0.0f;
    material.transmission         = 0.0f;
    material.transmissionColor    = float3(WHITE, WHITE, WHITE);
    material.subsurface           = 0.0f;
    material.subsurfaceColor      = float3(WHITE, WHITE, WHITE);
    material.subsurfaceRadius     = float3(WHITE, WHITE, WHITE);
    material.subsurfaceScale      = 1.0f;
    material.subsurfaceAnisotropy = 0.0f;
    material.sheen                = 0.0f;
    material.sheenColor           = float3(WHITE, WHITE, WHITE);
    material.sheenRoughness       = 0.3f;
    material.coat                 = 0.0f;
    material.coatColor            = float3(WHITE, WHITE, WHITE);
    material.coatRoughness        = 0.1f;
    material.coatAnisotropy       = 0.0f;
    material.coatRotation         = 0.0f;
    material.coatIOR              = 1.5f;
    material.coatAffectColor      = 0.0f;
    material.coatAffectRoughness  = 0.0f;
    material.emission             = 0.0f;
    material.emissionColor        = float3(WHITE, WHITE, WHITE);
    material.opacity              = float3(WHITE, WHITE, WHITE);
    material.thinWalled           = false;
    material.isOpaque             = true;
    material._padCbufferAlign     = uint2(0u, 0u);
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

    float rough = clamp(mat.specularRoughness, 0.04, 1.0);
    float alpha = rough * rough;

    float3 baseColor = mat.base * mat.baseColor;
    float metallic = saturate(mat.metalness);

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