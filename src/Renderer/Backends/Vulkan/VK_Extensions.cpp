#include "Renderer/Backends/Vulkan/VK_Extensions.h"
#include "Core/Log.h"

#include <vector>
#include <algorithm>
#include <cstring>
#include <string>

namespace Nova::Core::Renderer::Backends::Vulkan {

    void LogDeviceExtensions(VkPhysicalDevice physicalDevice) {
        uint32_t count = 0;
        VkResult res = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
        if (res != VK_SUCCESS) {
            NV_LOG_ERROR("vkEnumerateDeviceExtensionProperties failed while logging device extensions");
            return;
        }

        std::vector<VkExtensionProperties> props(count);
        res = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, props.data());
        if (res != VK_SUCCESS) {
            NV_LOG_ERROR("vkEnumerateDeviceExtensionProperties failed (stage 2)");
            return;
        }

        NV_LOG_INFO((std::string("Device supports ") + std::to_string(count) + " extensions:").c_str());
        for (const auto& p : props) {
            NV_LOG_INFO((std::string("  - ") + p.extensionName +
                " (spec " + std::to_string(p.specVersion) + ")").c_str());
        }
    }

    bool HasDeviceExtension(VkPhysicalDevice physicalDevice, const char* extName) {
        uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }

        std::vector<VkExtensionProperties> props(count);
        if (vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, props.data()) != VK_SUCCESS) {
            return false;
        }

        return std::any_of(props.begin(), props.end(), [&](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, extName) == 0;
        });
    }

    bool HasDeviceExtensions(VkPhysicalDevice physicalDevice, const std::vector<const char*>& requiredExtensions) {
        uint32_t count = 0;
        VkResult res = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
        if (res != VK_SUCCESS || count == 0)
            return false;

        std::vector<VkExtensionProperties> props(count);
        res = vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, props.data());
        if (res != VK_SUCCESS)
            return false;

        std::unordered_set<std::string> available;
        available.reserve(count);

        for (const auto& p : props)
            available.insert(p.extensionName);

        for (const char* req : requiredExtensions) {
            if (available.find(req) == available.end())
                return false;
        }

        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan