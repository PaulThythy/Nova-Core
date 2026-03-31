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
        Globals = 0,
        Mvp = 1,
        Instances = 2,
        Material = 3,
        Count = 4
    };

    struct NV_API Globals {
        alignas(16) glm::vec3 iResolution{ 0.0f, 0.0f, 0.0f };
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
        alignas(16) glm::vec4 baseColorFactor{ 1.0f, 1.0f, 1.0f, 1.0f };
        alignas(16) glm::vec4 emissiveFactor{ 0.0f, 0.0f, 0.0f, 1.0f }; // xyz used
        alignas(4)  float     metallicFactor{ 0.0f };
        alignas(4)  float     roughnessFactor{ 1.0f };
        alignas(4)  float     normalScale{ 1.0f };
        alignas(4)  float     occlusionStrength{ 1.0f };
        alignas(4)  float     emissiveStrength{ 0.0f };
        alignas(4)  float     alphaCutoff{ 0.5f };
        alignas(4)  int       alphaMode{ 0 };
        alignas(4)  int       _pad0{ 0 };
    };

    inline std::unordered_map<std::string, size_t> GetMaterialParameterLayout() {
        return {
        // Base color
            { "u_BaseColorFactor", offsetof(Material, baseColorFactor) },

            { "u_EmissiveFactor", offsetof(Material, emissiveFactor) },
            { "u_MetallicFactor", offsetof(Material, metallicFactor) },
            { "u_RoughnessFactor", offsetof(Material, roughnessFactor) },
            { "u_NormalScale", offsetof(Material, normalScale) },
            { "u_OcclusionStrength", offsetof(Material, occlusionStrength) },
            { "u_EmissiveStrength", offsetof(Material, emissiveStrength) },
            { "u_AlphaCutoff", offsetof(Material, alphaCutoff) },
            { "u_AlphaMode", offsetof(Material, alphaMode) },
        };
    }

    inline std::unordered_map<std::string, size_t> GetGlobalsLayout() {
        return {
            { "iResolution",  offsetof(Globals, iResolution)  },
            { "iTime",        offsetof(Globals, iTime)        },
            { "iTimeDelta",   offsetof(Globals, iTimeDelta)   },
            { "iFrameRate",   offsetof(Globals, iFrameRate)   },
            { "iFrame",       offsetof(Globals, iFrame)       },
            { "u_UseInstancing", offsetof(Globals, u_UseInstancing) },
            { "u_CameraPos",  offsetof(Globals, u_CameraPos)  },
            { "iMouse",       offsetof(Globals, iMouse)       },
            { "iDate",        offsetof(Globals, iDate)        },
        };
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_UNIFORMS_H
