#ifndef VK_COMMON_H
#define VK_COMMON_H

#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>

#include "Core/Log.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static inline void CheckVkResult(VkResult err) {
        if (err == VK_SUCCESS) return;
        //NV_LOG_ERROR((std::string("Vulkan error: ") + std::to_string((int)err)).c_str());
    }

    // Returns the first memory type index that is allowed by `typeBits` and exposes every flag in
    // `required`, or UINT32_MAX when none matches.
    static inline uint32_t FindMemoryTypeIndex(const VkPhysicalDeviceMemoryProperties& memProps, uint32_t typeBits, VkMemoryPropertyFlags required) {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeBits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & required) == required)
                return i;
        }
        return UINT32_MAX;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_COMMON_H