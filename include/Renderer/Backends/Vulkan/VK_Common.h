#ifndef VK_COMMON_H
#define VK_COMMON_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

#include "Core/Log.h"
#include "Renderer/RHI/RHI_ShaderTypes.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static inline void CheckVkResult(VkResult err) {
        if (err == VK_SUCCESS) return;
        NV_LOG_ERROR((std::string("Vulkan error: ") + std::to_string((int)err)).c_str());
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

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_COMMON_H