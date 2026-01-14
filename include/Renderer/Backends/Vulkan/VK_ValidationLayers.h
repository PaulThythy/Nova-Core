#ifndef VK_VALIDATION_LAYERS_H
#define VK_VALIDATION_LAYERS_H

#include <vulkan/vulkan.h>
#include <string>
#include <vector>
#include <cstring>


namespace Nova::Core::Renderer::Backends::Vulkan {

    static inline const std::vector<const char*> s_ValidationLayers = {
        "VK_LAYER_KHRONOS_validation"
    };

    static inline bool g_EnableValidationLayers =
#ifndef NOVA_DEBUG
        true;
#else
        false;
#endif

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

    // Setup and populate the debug create info structure
    void PopulateDebugMessengerCreateInfo(VkDebugUtilsMessengerCreateInfoEXT& createInfo);

    // Get/Set validation layers enabled state
    bool IsValidationLayersEnabled();
    void SetValidationLayersEnabled(bool enabled);

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_VALIDATION_LAYERS_H
