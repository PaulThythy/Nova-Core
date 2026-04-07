#ifndef RHI_SHADER_UNIFORMS_H
#define RHI_SHADER_UNIFORMS_H

/**
 * CPU mirrors of engine shader types (NovaUniforms.slang).
 *
 * Shaders use `ParameterBlock<NovaEngine> nova : register(space0);` so Vulkan/GL
 * bindings 0..N-1 follow the struct field order automatically.
 *
 * Vulkan descriptor set index for this block:
 *   kEngineDescriptorSet = 0
 *
 * App / custom shaders: put your own ParameterBlock in register(space1) and
 * use pipeline layout set 1 — no clash with engine bindings.
 */

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    inline constexpr uint32_t kEngineDescriptorSet = 0;
    inline constexpr uint32_t kUserDescriptorSet = 1;

    // Matches NovaEngine field order in NovaUniforms.slang (bindings 0..Count-1 in set 0).
    enum class EngineResourceSlot : uint32_t {
        FrameUniforms = 0,
        Mvp = 1,
        Instances = 2,
        Material = 3,
        Count = 4
    };

    struct NV_API FrameUniforms {
        alignas(16) glm::vec3 iResolution{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     _padAfterRes{ 0.0f };
        alignas(4)  float     iTime{ 0.0f };
        alignas(4)  float     iTimeDelta{ 0.0f };
        alignas(4)  float     iFrameRate{ 0.0f };
        alignas(4)  int       iFrame{ 0 };
        alignas(4)  int       u_UseInstancing{ 0 };
        alignas(8)  glm::ivec2 _Offset0{ 0, 0 };
        alignas(16) glm::vec3 u_CameraPos{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     _padAfterCameraPos{ 0.0f };
        alignas(16) glm::vec4 iMouse{ 0.0f, 0.0f, 0.0f, 0.0f };
        alignas(16) glm::vec4 iDate{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct NV_API MVP {
        alignas(16) glm::mat4 model{ 1.0f };
        alignas(16) glm::mat4 view{ 1.0f };
        alignas(16) glm::mat4 proj{ 1.0f };
        alignas(16) glm::mat4 viewProj{ 1.0f };
        alignas(16) glm::mat4 invViewProj{ 1.0f };
    };

    struct NV_API Instance {
        alignas(16) glm::mat4 model{ 1.0f };
        alignas(16) glm::vec4 color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct NV_API Material {
        alignas(4)  float       base{ 0.8f };
        alignas(16) glm::vec3   baseColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       diffuseRoughness{ 0.0f };
        alignas(4)  float       metalness{ 0.0f };
        alignas(16) glm::vec3   metalColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       specular{ 1.0f };
        alignas(16) glm::vec3   specularColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       specularRoughness{ 0.2f };
        alignas(4)  float       specularIOR{ 1.5f };
        alignas(4)  float       specularAnisotropy{ 0.0f };
        alignas(4)  float       specularRotation{ 0.0f };
        alignas(4)  float       transmission{ 0.0f };
        alignas(16) glm::vec3   transmissionColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       subsurface{ 0.0f };
        alignas(16) glm::vec3   subsurfaceColor{ 1.0f, 1.0f, 1.0f };
        alignas(16) glm::vec3   subsurfaceRadius{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       subsurfaceScale{ 1.0f };
        alignas(4)  float       subsurfaceAnisotropy{ 0.0f };
        alignas(4)  float       sheen{ 0.0f };
        alignas(16) glm::vec3   sheenColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       sheenRoughness{ 0.3f };
        alignas(4)  float       coat{ 0.0f };
        alignas(16) glm::vec3   coatColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       coatRoughness{ 0.1f };
        alignas(4)  float       coatAnisotropy{ 0.0f };
        alignas(4)  float       coatRotation{ 0.0f };
        alignas(4)  float       coatIOR{ 1.5f };
        alignas(4)  float       coatAffectColor{ 0.0f };
        alignas(4)  float       coatAffectRoughness{ 0.0f };
        alignas(4)  float       emission{ 0.0f };
        alignas(16) glm::vec3   emissionColor{ 1.0f, 1.0f, 1.0f };
        alignas(16) glm::vec3   opacity{ 1.0f, 1.0f, 1.0f };
        alignas(4)  int         thinWalled{ 0 };
        alignas(4)  int         isOpaque{ 1 };
        alignas(8)  glm::uvec2  _padCbufferAlign{ 0u, 0u };
    };

    inline std::unordered_map<std::string, size_t> GetMaterialParameterLayout() {
        return {
            { "base",                 offsetof(Material, base) },
            { "baseColor",            offsetof(Material, baseColor) },
            { "diffuseRoughness",     offsetof(Material, diffuseRoughness) },
            { "metalness",            offsetof(Material, metalness) },
            { "metalColor",           offsetof(Material, metalColor) },
            { "specular",             offsetof(Material, specular) },
            { "specularColor",        offsetof(Material, specularColor) },
            { "specularRoughness",    offsetof(Material, specularRoughness) },
            { "specularIOR",          offsetof(Material, specularIOR) },
            { "specularAnisotropy",   offsetof(Material, specularAnisotropy) },
            { "specularRotation",     offsetof(Material, specularRotation) },
            { "transmission",         offsetof(Material, transmission) },
            { "transmissionColor",    offsetof(Material, transmissionColor) },
            { "subsurface",           offsetof(Material, subsurface) },
            { "subsurfaceColor",      offsetof(Material, subsurfaceColor) },
            { "subsurfaceRadius",     offsetof(Material, subsurfaceRadius) },
            { "subsurfaceScale",      offsetof(Material, subsurfaceScale) },
            { "subsurfaceAnisotropy", offsetof(Material, subsurfaceAnisotropy) },
            { "sheen",                offsetof(Material, sheen) },
            { "sheenColor",           offsetof(Material, sheenColor) },
            { "sheenRoughness",       offsetof(Material, sheenRoughness) },
            { "coat",                 offsetof(Material, coat) },
            { "coatColor",            offsetof(Material, coatColor) },
            { "coatRoughness",        offsetof(Material, coatRoughness) },
            { "coatAnisotropy",       offsetof(Material, coatAnisotropy) },
            { "coatRotation",         offsetof(Material, coatRotation) },
            { "coatIOR",              offsetof(Material, coatIOR) },
            { "coatAffectColor",      offsetof(Material, coatAffectColor) },
            { "coatAffectRoughness",  offsetof(Material, coatAffectRoughness) },
            { "emission",             offsetof(Material, emission) },
            { "emissionColor",        offsetof(Material, emissionColor) },
            { "opacity",              offsetof(Material, opacity) },
            { "thinWalled",           offsetof(Material, thinWalled) },
            { "isOpaque",             offsetof(Material, isOpaque) },
            { "_padCbufferAlign",     offsetof(Material, _padCbufferAlign) },
        };
    }

    inline std::unordered_map<std::string, size_t> GetFrameUniformsLayout() {
        return {
            { "iResolution",     offsetof(FrameUniforms, iResolution) },
            { "_padAfterRes",    offsetof(FrameUniforms, _padAfterRes) },
            { "iTime",           offsetof(FrameUniforms, iTime) },
            { "iTimeDelta",      offsetof(FrameUniforms, iTimeDelta) },
            { "iFrameRate",      offsetof(FrameUniforms, iFrameRate) },
            { "iFrame",          offsetof(FrameUniforms, iFrame) },
            { "u_UseInstancing", offsetof(FrameUniforms, u_UseInstancing) },
            { "u_CameraPos",     offsetof(FrameUniforms, u_CameraPos) },
            { "_padAfterCameraPos", offsetof(FrameUniforms, _padAfterCameraPos) },
            { "iMouse",          offsetof(FrameUniforms, iMouse) },
            { "iDate",           offsetof(FrameUniforms, iDate) },
        };
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_UNIFORMS_H
