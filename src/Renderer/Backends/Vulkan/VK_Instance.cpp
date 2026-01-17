#include "Renderer/Backends/Vulkan/VK_Instance.h"

#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include "Renderer/Backends/Vulkan/VK_ValidationLayers.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"

#include <string>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    bool VK_Instance::Create() {
        return CreateInstance() && CreateSurface();
    }

    void VK_Instance::Destroy() {
        DestroyDebugUtilsMessengerEXT(m_Instance, s_DebugMessenger, nullptr);
        
        DestroySurface();
        DestroyInstance();
    }

    bool VK_Instance::CreateInstance() {
        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (!window) {
            NV_LOG_ERROR("CreateInstance failed: SDL window is null.");
            return false;
        }

        // Instance extensions from SDL
        Uint32 extCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
        if (!sdlExts || extCount == 0) {
            NV_LOG_ERROR((std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError()).c_str());
            return false;
        }

        std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

        if (IsValidationLayersEnabled()) {
            // Debug utils for validation layer messages
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        NV_LOG_INFO((std::string("SDL requires ") + std::to_string(extCount) + " instance extensions:").c_str());
        for (const char* e : extensions) {
            NV_LOG_INFO((std::string("  - ") + e).c_str());
        }

        if (IsValidationLayersEnabled()) {
            if (!CheckValidationLayerSupport()) {
                NV_LOG_WARN("Validation layers requested but not available. Disabling them.");
                SetValidationLayersEnabled(false);
            }
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Nova";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "Nova Core";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &appInfo;
        ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();

        VkDebugUtilsMessengerCreateInfoEXT dbgCreateInfo{};
        if (IsValidationLayersEnabled()) {
            ci.enabledLayerCount = static_cast<uint32_t>(s_ValidationLayers.size());
            ci.ppEnabledLayerNames = s_ValidationLayers.data();

            dbgCreateInfo = {};
            dbgCreateInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
            dbgCreateInfo.messageSeverity =
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            dbgCreateInfo.messageType =
                VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
                | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            dbgCreateInfo.pfnUserCallback = DebugCallback;
            dbgCreateInfo.pUserData = nullptr;

            ci.pNext = &dbgCreateInfo;
        }
        else {
            ci.enabledLayerCount = 0;
            ci.ppEnabledLayerNames = nullptr;
            ci.pNext = nullptr;
        }

        VkResult res = vkCreateInstance(&ci, nullptr, &m_Instance);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            NV_LOG_ERROR("Failed to create Vulkan instance.");
            return false;
        }

        if (IsValidationLayersEnabled()) {
            if (!SetupDebugMessenger(m_Instance)) {
                NV_LOG_WARN("Failed to setup Vulkan debug messenger.");
            }
        }

        NV_LOG_INFO("Vulkan instance created.");
        return true;
    }

    void VK_Instance::DestroyInstance() {
        if (m_Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }
    }

    bool VK_Instance::CreateSurface() {
        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (!window) {
            NV_LOG_ERROR("CreateSurface failed: SDL window is null.");
            return false;
        }

        if (m_Instance == VK_NULL_HANDLE) {
            NV_LOG_ERROR("CreateSurface failed: Vulkan instance not initialized.");
            return false;
        }

        if (!SDL_Vulkan_CreateSurface(window, m_Instance, nullptr, &m_Surface)) {
            NV_LOG_ERROR((std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError()).c_str());
            return false;
        }

        NV_LOG_INFO("Vulkan surface created.");
        return true;
    }

    void VK_Instance::DestroySurface() {
        if (m_Surface != VK_NULL_HANDLE && m_Instance != VK_NULL_HANDLE) {
            SDL_Vulkan_DestroySurface(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
        }
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan
