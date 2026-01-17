#ifndef VK_EXTENSIONS_H
#define VK_EXTENSIONS_H

#include <vulkan/vulkan.h>
#include <unordered_set>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    // Log all extensions supported by the given physical device
    void LogDeviceExtensions(VkPhysicalDevice physicalDevice);

    // Check if the given physical device supports a specific extension
    bool HasDeviceExtension(VkPhysicalDevice physicalDevice, const char* extName);

    bool HasDeviceExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions);

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_EXTENSIONS_H