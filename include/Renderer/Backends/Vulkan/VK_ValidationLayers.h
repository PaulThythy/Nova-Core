#ifndef VK_VALIDATION_LAYERS_H
#define VK_VALIDATION_LAYERS_H

#include <volk.h>
#include <string>
#include <vector>
#include <cstring>

#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static inline const std::vector<const char*> s_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    static inline bool s_EnableValidationLayers =
#ifdef NOVA_DEBUG
        true;
#else
        false;
#endif

    inline VkDebugUtilsMessengerEXT s_DebugMessenger = VK_NULL_HANDLE;

    bool CheckValidationLayerSupport();

    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );

    bool SetupDebugMessenger(VkInstance instance);
    void DestroyDebugMessenger(VkInstance instance);

    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    bool IsValidationLayersEnabled();
    void SetValidationLayersEnabled(bool enabled);

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_VALIDATION_LAYERS_H