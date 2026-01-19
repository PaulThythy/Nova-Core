#include "Renderer/Backends/Vulkan/VK_Device.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    bool VK_Device::HasSwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const {
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

        return formatCount > 0 && presentModeCount > 0;
    }

    bool VK_Device::Create(const VkInstance instance, const VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions) {
        if(instance == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_Device::Create failed: VK_Instance is not initialized (instance/surface null)");
            return false;
        }

        if (!PickPhysicalDevice(instance, surface, requiredDeviceExtensions)) {
            return false;
        }

        if (!CreateLogicalDevice(requiredDeviceExtensions)) {
            return false;
        }

        NV_LOG_INFO("VK_Device created successfully.");
        return true;
    }

    void VK_Device::Destroy() {
        if (m_Device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
        }

        m_PhysicalDevice = VK_NULL_HANDLE;

        m_GraphicsQueueFamily = UINT32_MAX;
        m_PresentQueueFamily  = UINT32_MAX;
        m_ComputeQueueFamily  = UINT32_MAX;
        m_TransferQueueFamily = UINT32_MAX;

        m_GraphicsQueue = VK_NULL_HANDLE;
        m_PresentQueue  = VK_NULL_HANDLE;
        m_ComputeQueue  = VK_NULL_HANDLE;
        m_TransferQueue = VK_NULL_HANDLE;

        m_Properties = {};
        m_Features = {};
        m_MemoryProperties = {};

        NV_LOG_INFO("VK_Device destroyed.");
    }

    VK_Device::VK_QueueFamilyIndices VK_Device::FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const {
        VK_QueueFamilyIndices indices;

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
        if (qCount == 0) {
            return indices;
        }

        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, qProps.data());

        // Track the "best" compute/transfer queues.
        // Prefer dedicated queues (compute without graphics, transfer without graphics/compute)
        for (uint32_t i = 0; i < qCount; ++i) {
            const VkQueueFamilyProperties& props = qProps[i];
            if (props.queueCount == 0) {
                continue;
            }

            const bool supportsGraphics = (props.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
            const bool supportsCompute  = (props.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
            const bool supportsTransfer = (props.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;

            // Graphics
            if (supportsGraphics && !indices.m_Graphics.has_value()) {
                indices.m_Graphics = i;
            }

            // Present
            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
            if (presentSupport && !indices.m_Present.has_value()) {
                indices.m_Present = i;
            }

            // Compute: prefer compute-only (no graphics)
            if (supportsCompute) {
                if (!indices.m_Compute.has_value()) {
                    indices.m_Compute = i;
                } else {
                    // If current compute also supports graphics, prefer a compute-only family
                    const uint32_t current = *indices.m_Compute;
                    const bool currentIsGraphics = (qProps[current].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                    if (currentIsGraphics && !supportsGraphics) {
                        indices.m_Compute = i;
                    }
                }
            }

            // Transfer: prefer transfer-only (no graphics, no compute)
            if (supportsTransfer) {
                if (!indices.m_Transfer.has_value()) {
                    indices.m_Transfer = i;
                } else {
                    const uint32_t current = *indices.m_Transfer;
                    const VkQueueFamilyProperties& curProps = qProps[current];
                    const bool curHasGraphics = (curProps.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
                    const bool curHasCompute  = (curProps.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
                    const bool newHasGraphics = supportsGraphics;
                    const bool newHasCompute  = supportsCompute;

                    const bool currentDedicated = (!curHasGraphics && !curHasCompute);
                    const bool newDedicated     = (!newHasGraphics && !newHasCompute);

                    if (!currentDedicated && newDedicated) {
                        indices.m_Transfer = i;
                    }
                }
            }
        }

        // Fallbacks: compute/transfer can be the same family as graphics
        if (!indices.m_Compute.has_value() && indices.m_Graphics.has_value()) {
            indices.m_Compute = indices.m_Graphics;
        }
        if (!indices.m_Transfer.has_value() && indices.m_Graphics.has_value()) {
            indices.m_Transfer = indices.m_Graphics;
        }

        return indices;
    }

    bool VK_Device::PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions) {
        uint32_t deviceCount = 0;
        VkResult res = vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);
        CheckVkResult(res);

        if (res != VK_SUCCESS || deviceCount == 0) {
            NV_LOG_ERROR("No Vulkan physical devices found.");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        res = vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            NV_LOG_ERROR("vkEnumeratePhysicalDevices failed (stage 2)");
            return false;
        }

        // Choose best device by scoring
        int bestScore = -1;
        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        VK_QueueFamilyIndices bestQueues;

        for (VkPhysicalDevice dev : devices) {
            VK_QueueFamilyIndices indices = FindQueueFamilies(dev, surface);
            if (!indices.IsComplete()) {
                continue;
            }

            if (!HasDeviceExtensions(dev, requiredDeviceExtensions)) {
                continue;
            }

            // If swapchain extension is required, ensure we have at least one format + present mode
            if (std::find(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end(), VK_KHR_SWAPCHAIN_EXTENSION_NAME)
                != requiredDeviceExtensions.end())
            {
                if (!HasSwapChainSupport(dev, surface)) {
                    continue;
                }
            }

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);

            int score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
            score += static_cast<int>(props.limits.maxImageDimension2D);

            if (score > bestScore) {
                bestScore = score;
                bestDevice = dev;
                bestQueues = indices;
            }
        }

        if (bestDevice == VK_NULL_HANDLE) {
            NV_LOG_ERROR("No suitable Vulkan physical device found (graphics + present + required extensions).");
            return false;
        }

        // Commit selection
        m_PhysicalDevice = bestDevice;

        m_GraphicsQueueFamily = bestQueues.m_Graphics.value();
        m_PresentQueueFamily  = bestQueues.m_Present.value();
        m_ComputeQueueFamily  = bestQueues.m_Compute.value();
        m_TransferQueueFamily = bestQueues.m_Transfer.value();

        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_Properties);
        vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_Features);
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);

        NV_LOG_INFO((std::string("Selected GPU: ") + m_Properties.deviceName).c_str());

        NV_LOG_INFO((std::string("Queue families:") +
            " graphics=" + std::to_string(m_GraphicsQueueFamily) +
            " present=" + std::to_string(m_PresentQueueFamily) +
            " compute=" + std::to_string(m_ComputeQueueFamily) +
            " transfer=" + std::to_string(m_TransferQueueFamily)).c_str());

        LogDeviceExtensions(m_PhysicalDevice);

        return true;
    }

    bool VK_Device::CreateLogicalDevice(const std::vector<const char*>& requiredDeviceExtensions) {
        if (m_PhysicalDevice == VK_NULL_HANDLE) {
            NV_LOG_ERROR("CreateLogicalDevice failed: physical device is null");
            return false;
        }

        const float priority = 1.0f;

        // Use unique queue family indices (a family may serve multiple roles).
        std::set<uint32_t> uniqueFamilies = {
            m_GraphicsQueueFamily,
            m_PresentQueueFamily,
            m_ComputeQueueFamily,
            m_TransferQueueFamily
        };

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueFamilies.size());

        for (uint32_t family : uniqueFamilies) {
            if (family == UINT32_MAX) continue;

            VkDeviceQueueCreateInfo qci{};
            qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = family;
            qci.queueCount = 1;
            qci.pQueuePriorities = &priority;
            queueCreateInfos.push_back(qci);
        }

        // Enable only what you need here. Keep it empty for now.
        VkPhysicalDeviceFeatures enabledFeatures{};

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = static_cast<uint32_t>(queueCreateInfos.size());
        dci.pQueueCreateInfos = queueCreateInfos.data();
        dci.pEnabledFeatures = &enabledFeatures;
        dci.enabledExtensionCount = static_cast<uint32_t>(requiredDeviceExtensions.size());
        dci.ppEnabledExtensionNames = requiredDeviceExtensions.data();

        VkResult res = vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            NV_LOG_ERROR("Failed to create Vulkan logical device");
            return false;
        }

        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_PresentQueueFamily, 0, &m_PresentQueue);
        vkGetDeviceQueue(m_Device, m_ComputeQueueFamily, 0, &m_ComputeQueue);
        vkGetDeviceQueue(m_Device, m_TransferQueueFamily, 0, &m_TransferQueue);

        NV_LOG_INFO("Vulkan logical device created.");
        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan