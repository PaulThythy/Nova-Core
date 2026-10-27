#ifndef VK_COMMON_H
#define VK_COMMON_H

#include <volk.h>
#include <cstdint>
#include <string>

#include "Core/Log.h"
#include "Renderer/RHI/RHI_RenderGraphTypes.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"
#include "Renderer/RHI/RHI_ShaderTypes.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static inline const char* VkResultToString(VkResult err) {
        switch (err) {
            case VK_SUCCESS: return "VK_SUCCESS";
            case VK_NOT_READY: return "VK_NOT_READY";
            case VK_TIMEOUT: return "VK_TIMEOUT";
            case VK_EVENT_SET: return "VK_EVENT_SET";
            case VK_EVENT_RESET: return "VK_EVENT_RESET";
            case VK_INCOMPLETE: return "VK_INCOMPLETE";
            case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
            case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
            case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
            case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
            case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
            case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
            case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
            case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
            case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
            case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
            case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
            case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
            case VK_ERROR_UNKNOWN: return "VK_ERROR_UNKNOWN";
            default: return "VK_RESULT_UNKNOWN";
        }
    }

    static inline void CheckVkResult(VkResult err) {
        if (err == VK_SUCCESS) return;
        NV_LOG_ERROR((std::string("Vulkan error: ") + VkResultToString(err) +
                      " (" + std::to_string(static_cast<int>(err)) + ")").c_str());
    }

    static inline VkShaderStageFlags ToVkStageFlags(RHI::RHI_ShaderStageMask mask) {
        VkShaderStageFlags out = 0;
        const uint32_t m = static_cast<uint32_t>(mask);
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Vertex)) out |= VK_SHADER_STAGE_VERTEX_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Fragment)) out |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Geometry)) out |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::TessCtrl)) out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::TessEval)) out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Compute)) out |= VK_SHADER_STAGE_COMPUTE_BIT;
        return out;
    }

    static inline VkDescriptorType ToVkDescriptorType(const RHI::RHI_BindingInfo& b) {
        using RK = RHI::RHI_ResourceKind;
        switch (b.m_Kind) {
            case RK::ConstantBuffer:         return b.m_IsDynamicUniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case RK::StructuredBuffer:       return b.m_IsDynamicUniformBuffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case RK::Texture:                return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case RK::Sampler:                return VK_DESCRIPTOR_TYPE_SAMPLER;
            case RK::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case RK::RWTexture:              return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case RK::RWStructuredBuffer:     return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            default:                         return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }

    static inline VkFormat ToVkFormat(RHI::RHI_TextureFormat format) {
        switch (format) {
            case RHI::RHI_TextureFormat::RGBA8:           return VK_FORMAT_R8G8B8A8_UNORM;
            case RHI::RHI_TextureFormat::RGBA16F:         return VK_FORMAT_R16G16B16A16_SFLOAT;
            case RHI::RHI_TextureFormat::RGBA32F:         return VK_FORMAT_R32G32B32A32_SFLOAT;
            case RHI::RHI_TextureFormat::Depth32:         return VK_FORMAT_D32_SFLOAT;
            case RHI::RHI_TextureFormat::Depth24Stencil8: return VK_FORMAT_D24_UNORM_S8_UINT;
            default:                                      return VK_FORMAT_UNDEFINED;
        }
    }

    static inline bool IsDepthFormat(RHI::RHI_TextureFormat format) {
        return format == RHI::RHI_TextureFormat::Depth32
            || format == RHI::RHI_TextureFormat::Depth24Stencil8;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_COMMON_H