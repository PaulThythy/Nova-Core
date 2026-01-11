#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include "Core/Application.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    static void CheckVkResult(VkResult err) {
        if (err == VK_SUCCESS) return;
        NV_LOG_ERROR((std::string("Vulkan error: VkResult=") + std::to_string((int)err)).c_str());
    }

    static void LogDeviceExtensions(VkPhysicalDevice device) {
        uint32_t count = 0;
        VkResult res = vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
        CheckVkResult(res);

        std::vector<VkExtensionProperties> props(count);
        res = vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data());
        CheckVkResult(res);

        NV_LOG_INFO((std::string("Device supports ") + std::to_string(count) + " extensions:").c_str());
        for (const auto& p : props) {
            NV_LOG_INFO((std::string("  - ") + p.extensionName + " (spec " + std::to_string(p.specVersion) + ")").c_str());
        }
    }

    static bool HasDeviceExtension(VkPhysicalDevice device, const char* extName) {
        uint32_t count = 0;
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr) != VK_SUCCESS) {
            return false;
        }
        std::vector<VkExtensionProperties> props(count);
        if (vkEnumerateDeviceExtensionProperties(device, nullptr, &count, props.data()) != VK_SUCCESS) {
            return false;
        }

        return std::any_of(props.begin(), props.end(), [&](const VkExtensionProperties& p) {
            return std::strcmp(p.extensionName, extName) == 0;
        });
    }

    static bool FindQueueFamilies(VkPhysicalDevice device, VkSurfaceKHR surface, uint32_t& outGraphics, uint32_t& outPresent) {
        outGraphics = UINT32_MAX;
        outPresent  = UINT32_MAX;

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qCount, nullptr);
        if (qCount == 0) return false;

        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &qCount, qProps.data());

        for (uint32_t i = 0; i < qCount; ++i) {
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && outGraphics == UINT32_MAX) {
                outGraphics = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport);
            if (presentSupport && outPresent == UINT32_MAX) {
                outPresent = i;
            }
        }

        return outGraphics != UINT32_MAX && outPresent != UINT32_MAX;
    }

    bool VK_Renderer::Create() {
        NV_LOG_INFO("Creating Vulkan Renderer...");

        if (!CreateInstance()) return false;
        if (!CreateSurface()) return false;
        if (!PickPhysicalDevice()) return false;
        if (!CreateDevice()) return false;

        NV_LOG_INFO("Vulkan renderer created successfully.");
        return true;
    }

    void VK_Renderer::Destroy() {
        // English comment: Always wait for device idle before destroying resources.
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
        }

        if (m_Surface != VK_NULL_HANDLE) {
            // SDL3 provides a helper for surface destruction.
            SDL_Vulkan_DestroySurface(m_Instance, m_Surface, nullptr);
            m_Surface = VK_NULL_HANDLE;
        }

        if (m_Instance != VK_NULL_HANDLE) {
            vkDestroyInstance(m_Instance, nullptr);
            m_Instance = VK_NULL_HANDLE;
        }
    }

    bool VK_Renderer::Resize(int /*w*/, int /*h*/) {
        // Stub for now
        return true;
    }

    void VK_Renderer::Update(float /*dt*/) {
        // Stub for now
    }

    void VK_Renderer::BeginFrame() {
        // Stub for now
    }

    void VK_Renderer::Render() {
        // Stub for now
    }

    void VK_Renderer::EndFrame() {
        // Stub for now
    }

    bool VK_Renderer::CreateInstance() {
        SDL_Window* window = ::Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (!window) {
            NV_LOG_ERROR("CreateInstance failed: SDL window is null.");
            return false;
        }

        // SDL3: SDL_Vulkan_GetInstanceExtensions returns an array (owned by SDL), not a bool. :contentReference[oaicite:1]{index=1}
        Uint32 extCount = 0;
        const char* const* sdlExts = SDL_Vulkan_GetInstanceExtensions(&extCount);
        if (!sdlExts || extCount == 0) {
            NV_LOG_ERROR((std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError()).c_str());
            return false;
        }

        std::vector<const char*> extensions(sdlExts, sdlExts + extCount);

        NV_LOG_INFO((std::string("SDL requires ") + std::to_string(extCount) + " instance extensions:").c_str());
        for (const char* e : extensions) {
            NV_LOG_INFO((std::string("  - ") + e).c_str());
        }

        VkApplicationInfo appInfo{};
        appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        appInfo.pApplicationName = "Nova";
        appInfo.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.pEngineName = "Nova Engine";
        appInfo.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        appInfo.apiVersion = VK_API_VERSION_1_0;

        VkInstanceCreateInfo ci{};
        ci.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        ci.pApplicationInfo = &appInfo;
        ci.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
        ci.ppEnabledExtensionNames = extensions.data();
        ci.enabledLayerCount = 0;
        ci.ppEnabledLayerNames = nullptr;

        VkResult res = vkCreateInstance(&ci, nullptr, &m_Instance);
        CheckVkResult(res);

        NV_LOG_INFO("Vulkan instance created.");
        return true;
    }

    bool VK_Renderer::CreateSurface() {
        SDL_Window* window = ::Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (!window) {
            NV_LOG_ERROR("CreateSurface failed: SDL window is null.");
            return false;
        }

        // SDL3: window must be created with SDL_WINDOW_VULKAN and instance must enable SDL_Vulkan_GetInstanceExtensions(). :contentReference[oaicite:2]{index=2}
        if (!SDL_Vulkan_CreateSurface(window, m_Instance, nullptr, &m_Surface)) {
            NV_LOG_ERROR((std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError()).c_str());
            return false;
        }

        NV_LOG_INFO("Vulkan surface created.");
        return true;
    }

    bool VK_Renderer::PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        VkResult res = vkEnumeratePhysicalDevices(m_Instance, &deviceCount, nullptr);
        CheckVkResult(res);

        std::vector<VkPhysicalDevice> devices(deviceCount);
        res = vkEnumeratePhysicalDevices(m_Instance, &deviceCount, devices.data());
        CheckVkResult(res);

        for (VkPhysicalDevice dev : devices) {
            uint32_t g = UINT32_MAX, p = UINT32_MAX;
            if (!FindQueueFamilies(dev, m_Surface, g, p)) {
                continue;
            }

            // We require swapchain extension for real rendering later; for now we still prefer devices that have it.
            // (You can relax this if you really want minimal device creation.)
            if (!HasDeviceExtension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                continue;
            }

            m_PhysicalDevice = dev;

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);

            NV_LOG_INFO((std::string("Selected GPU: ") + props.deviceName).c_str());
            LogDeviceExtensions(m_PhysicalDevice); // <-- "extensions compatibles" (device)
            return true;
        }

        NV_LOG_ERROR("No suitable Vulkan physical device found (graphics+present+swapchain).");
        return false;
    }

    bool VK_Renderer::CreateDevice() {
        uint32_t graphicsIndex = UINT32_MAX;
        uint32_t presentIndex  = UINT32_MAX;
        if (!FindQueueFamilies(m_PhysicalDevice, m_Surface, graphicsIndex, presentIndex)) {
            NV_LOG_ERROR("CreateDevice failed: could not find required queue families.");
            return false;
        }

        const float priority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> qcis;
        qcis.reserve(2);

        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = graphicsIndex;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);

        if (presentIndex != graphicsIndex) {
            VkDeviceQueueCreateInfo pqci{};
            pqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            pqci.queueFamilyIndex = presentIndex;
            pqci.queueCount = 1;
            pqci.pQueuePriorities = &priority;
            qcis.push_back(pqci);
        }

        std::vector<const char*> enabledDeviceExts;
        enabledDeviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        VkPhysicalDeviceFeatures features{}; // keep default (no special features for now)

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
        dci.pQueueCreateInfos = qcis.data();
        dci.pEnabledFeatures = &features;
        dci.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExts.size());
        dci.ppEnabledExtensionNames = enabledDeviceExts.data();

        VkResult res = vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device);
        CheckVkResult(res);

        NV_LOG_INFO("Vulkan logical device created.");
        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan