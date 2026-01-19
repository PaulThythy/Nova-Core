#ifndef VK_VALIDATION_LAYERS_H
#define VK_VALIDATION_LAYERS_H

#include <vulkan/vulkan.h>
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

    // Check if validation layers are supported on this system
    bool CheckValidationLayerSupport();

    // Debug callback function for validation layer messages
    VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
        VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
        VkDebugUtilsMessageTypeFlagsEXT messageType,
        const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
        void* pUserData
    );

    // Create a debug utils messenger for validation layer messages
    VkResult CreateDebugUtilsMessengerEXT(
        VkInstance instance,
        const VkDebugUtilsMessengerCreateInfoEXT* pCreateInfo,
        const VkAllocationCallbacks* pAllocator,
        VkDebugUtilsMessengerEXT* pMessenger
    );

    // Destroy a debug utils messenger
    void DestroyDebugUtilsMessengerEXT(
        VkInstance instance,
        VkDebugUtilsMessengerEXT messenger,
        const VkAllocationCallbacks* pAllocator
    );

    bool SetupDebugMessenger(VkInstance instance);

    // Setup and populate the debug create info structure
    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // Get/Set validation layers enabled state
    bool IsValidationLayersEnabled();
    void SetValidationLayersEnabled(bool enabled);

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_VALIDATION_LAYERS_H
