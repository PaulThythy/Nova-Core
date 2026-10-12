#include "Renderer/Backends/Vulkan/VK_ValidationLayers.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    bool CheckValidationLayerSupport() {
        uint32_t layerCount = 0;
        vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

        std::vector<VkLayerProperties> availableLayers(layerCount);
        vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

        for (const char* layerName : s_ValidationLayers) {
            bool found = false;
            for (const auto& layerProps : availableLayers) {
                if (std::strcmp(layerName, layerProps.layerName) == 0) {
                    found = true;
                    break;
                }
            }
            if (!found) {
                NV_LOG_WARN((std::string("Validation layer not found: ") + layerName).c_str());
                return false;
            }
        }

        NV_LOG_INFO("Validation layer supported.");
        return true;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    ) {
        (void)messageType;
        (void)pUserData;

        const char* prefix = "[VULKAN] ";

        if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
            NV_LOG_ERROR((std::string(prefix) + pCallbackData->pMessage + "\n").c_str());
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
            NV_LOG_WARN((std::string(prefix) + pCallbackData->pMessage + "\n").c_str());
        }
        else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) {
            NV_LOG_INFO((std::string(prefix) + pCallbackData->pMessage + "\n").c_str());
        }
        else {
            NV_LOG_DEBUG((std::string(prefix) + pCallbackData->pMessage + "\n").c_str());
        }

        return VK_FALSE;
    }

    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo) {
        createInfo = {};
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        createInfo.pfnUserCallback = DebugCallback;
        createInfo.pUserData = nullptr;
    }

    bool SetupDebugMessenger(VkInstance instance) {
        if (!s_EnableValidationLayers)
            return true;

        // volkLoadInstance a déjà résolu vkCreateDebugUtilsMessengerEXT
        if (!vkCreateDebugUtilsMessengerEXT) {
            NV_LOG_ERROR("vkCreateDebugUtilsMessengerEXT not loaded by volk.");
            return false;
        }

        VkDebugUtilsMessengerCreateInfoEXT createInfo{};
        PopulateDebugMessengerCreateInfo(createInfo);

        VkResult res = vkCreateDebugUtilsMessengerEXT(instance, &createInfo, nullptr, &s_DebugMessenger);
        if (res != VK_SUCCESS) {
            CheckVkResult(res);
            NV_LOG_ERROR("Failed to create Vulkan debug messenger.");
            return false;
        }

        NV_LOG_INFO("Vulkan debug messenger created.");
        return true;
    }

    void DestroyDebugMessenger(VkInstance instance) {
        if (s_DebugMessenger == VK_NULL_HANDLE)
            return;

        if (vkDestroyDebugUtilsMessengerEXT)
            vkDestroyDebugUtilsMessengerEXT(instance, s_DebugMessenger, nullptr);

        s_DebugMessenger = VK_NULL_HANDLE;
    }

    bool IsValidationLayersEnabled() {
        return s_EnableValidationLayers;
    }

    void SetValidationLayersEnabled(bool enabled) {
        s_EnableValidationLayers = enabled;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan