#ifndef VK_COMMON_H
#define VK_COMMON_H

#include <vulkan/vulkan.h>
#include <string>

#include "Core/Log.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static inline void CheckVkResult(VkResult err) {
        if (err == VK_SUCCESS) return;
        NV_LOG_ERROR((std::string("Vulkan error: ") + std::to_string((int)err)).c_str());
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_COMMON_H