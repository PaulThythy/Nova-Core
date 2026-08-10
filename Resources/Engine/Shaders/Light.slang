#ifndef __LIGHT_H__
#define __LIGHT_H__

#include "Globals.slang"

static const int MAX_LIGHTS = 16;
static const int MAX_SHADOW_MAPS = 4;
static const float SHADOW_MAP_RESOLUTION = 2048.0;

static const int LIGHT_DIRECTIONAL = 0;
static const int LIGHT_POINT = 1;
static const int LIGHT_SPOT = 2;

struct LightGPU {
    int type;
    int castShadow;
    int shadowMapIndex;
    float intensity;
    float3 color;
    float range;
    float3 direction;
    float innerConeCos;
    float3 position;
    float outerConeCos;
    float shadowBiasConstant;
    float shadowBiasSlope;
    float shadowNormalBias;
    float _padBias;
    float4x4 lightViewProj;
};

float SpotAttenuation(LightGPU light, float3 L) {
    float cosTheta = dot(normalize(-light.direction), L);
    float inner = light.innerConeCos;
    float outer = light.outerConeCos;
    return saturate((cosTheta - outer) / max(inner - outer, 1e-4));
}

/** Resolve light vector L (toward the light) and photometric attenuation. */
void EvalLightVector(LightGPU light, float3 worldPos, out float3 L, out float atten) {
    L = float3(0.0, 0.0, 0.0);
    atten = 1.0;

    if (light.type == LIGHT_DIRECTIONAL) {
        L = normalize(-light.direction);
        return;
    }

    float3 toLight = light.position - worldPos;
    float dist = length(toLight);
    L = toLight / max(dist, 1e-4);
    float rangeAtten = saturate(1.0 - dist / max(light.range, 1e-4));
    atten = rangeAtten * rangeAtten;

    if (light.type == LIGHT_SPOT)
        atten *= SpotAttenuation(light, L);
}

// Poisson disk (16 taps) — softer edges than a 3x3 box, fewer square artifacts.
static const float2 kPoisson[16] = {
    float2(-0.94201624, -0.39906216),
    float2( 0.94558609, -0.76890725),
    float2(-0.09418410, -0.92938870),
    float2( 0.34495938,  0.29387760),
    float2(-0.91588581,  0.45771432),
    float2(-0.81544232, -0.87912464),
    float2(-0.38277543,  0.27676845),
    float2( 0.97484398,  0.75648379),
    float2( 0.44323325, -0.97511554),
    float2( 0.53742981, -0.47373420),
    float2(-0.26496902, -0.41893023),
    float2( 0.79197514,  0.19090188),
    float2(-0.24188840,  0.99706507),
    float2(-0.81409955,  0.91437590),
    float2( 0.19984126,  0.78641367),
    float2( 0.14383161, -0.14100790)
};

float ShadowFactor(
    LightGPU light,
    float3 worldPos,
    float3 N,
    float3 L,
    Texture2DArray shadowMaps,
    SamplerComparisonState shadowSampler)
{
    if (light.castShadow == 0 || light.shadowMapIndex < 0)
        return 1.0;

    float nDotL = saturate(dot(N, L));
    // Grazing light: a fixed light-space depth bias projects to a large gap on the ground (~1/N·L).
    // Scale offsets by N·L (floored) so contact shadows stay glued when the light leans sideways.
    float angleKeep = max(nDotL, 0.25);

    // Perspective (spot): world normal offset also shifts light-space XY (parallax) and
    // detaches contact shadows. Keep it tiny; rely mostly on depth-only receiver bias.
    float normalBias = light.shadowNormalBias * angleKeep;
    float recvScale = 1.0;
    float pcfRadius = 2.5;
    if (light.type == LIGHT_SPOT) {
        normalBias *= 0.15;
        recvScale = 0.15;
        pcfRadius = 1.5;
    }

    float3 samplePos = worldPos + N * normalBias;

    float4 lightClip = mul(light.lightViewProj, float4(samplePos, 1.0));
    float3 ndc = lightClip.xyz / lightClip.w;
    float2 uv = ndc.xy * 0.5 + 0.5;

    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0 || ndc.z < 0.0 || ndc.z > 1.0)
        return 1.0;

    // Map artist-facing constant (~0.5) into a small depth-buffer epsilon, scaled by angle.
    float recvBias = max(light.shadowBiasConstant, 0.0) * 0.0008 * angleKeep * recvScale;
    float compareDepth = ndc.z - recvBias;

    float2 texel = (pcfRadius / SHADOW_MAP_RESOLUTION).xx;

    float shadow = 0.0;
    for (int i = 0; i < 16; ++i) {
        shadow += shadowMaps.SampleCmp(
            shadowSampler,
            float3(uv + kPoisson[i] * texel, light.shadowMapIndex),
            compareDepth);
    }
    return shadow / 16.0;
}

#endif // __LIGHT_H__