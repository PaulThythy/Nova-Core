#include "Renderer/Backends/Vulkan/VK_Device.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static std::string QueueFlagsToString(VkQueueFlags flags) {
        std::string s;
        auto Add = [&](const char* name) {
            if (!s.empty()) s += ", ";
            s += name;
            };

        if (flags & VK_QUEUE_GRAPHICS_BIT) Add("GRAPHICS");
        if (flags & VK_QUEUE_COMPUTE_BIT)  Add("COMPUTE");
        if (flags & VK_QUEUE_TRANSFER_BIT) Add("TRANSFER");
        if (flags & VK_QUEUE_SPARSE_BINDING_BIT) Add("SPARSE_BINDING_BIT");
        if (flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) Add("VIDEO_DECODE_BIT_KHR");
        if (flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) Add("VIDEO_ENCODE_BIT_KHR");
        if (flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) Add("OPTICAL_FLOW_BIT_NV");

        if (s.empty()) s = "NONE";
        return s;
    }

    void VK_Device::LogQueueFamilies(const std::vector<VK_QueueFamily>& families) const {
        NV_LOG_INFO("---- Queue Families ----");
        for (const auto& f : families) {
            std::string line =
                "Family " + std::to_string(f.index) +
                " | flags=" + QueueFlagsToString(f.flags) +
                " | queueCount=" + std::to_string(f.queueCount) +
                " | present=" + std::string(f.supportsPresentation ? "true" : "false") +
                " | timestampValidBits=" + std::to_string(f.timestampValidBits) +
                " | granularity=(" +
                std::to_string(f.minImageTransferGranularity.width) + "," +
                std::to_string(f.minImageTransferGranularity.height) + "," +
                std::to_string(f.minImageTransferGranularity.depth) + ")";

            NV_LOG_INFO(line.c_str());
        }
        NV_LOG_INFO("------------------------");
    }

    bool VK_Device::HasSwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const {
        uint32_t formatCount;
        vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);

        uint32_t presentModeCount;
        vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);

        return formatCount > 0 && presentModeCount > 0;
    }

    const VK_Device::VK_QueueFamily* VK_Device::GetQueueFamily(uint32_t familyIndex) const {
        for (const auto& f : m_QueueFamilies) {
            if (f.index == familyIndex)
                return &f;
        }
        return nullptr;
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

        m_Properties = {};
        m_Features = {};
        m_MemoryProperties = {};

        NV_LOG_INFO("VK_Device destroyed.");
    }

    std::vector<VK_Device::VK_QueueFamily> VK_Device::QueryQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const {
        std::vector<VK_QueueFamily> families;

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
        if (qCount == 0) {
            return families;
        }

        std::vector<VkQueueFamilyProperties> props(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, props.data());

        families.reserve(qCount);

        for (uint32_t i = 0; i < qCount; ++i) {
            VK_QueueFamily fam{};
            fam.index = i;
            fam.flags = props[i].queueFlags;
            fam.queueCount = props[i].queueCount;
            fam.timestampValidBits = props[i].timestampValidBits;
            fam.minImageTransferGranularity = props[i].minImageTransferGranularity;

            if (surface != VK_NULL_HANDLE) {
                VkBool32 presentSupport = VK_FALSE;
                vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
                fam.supportsPresentation = (presentSupport == VK_TRUE);
            }

            families.push_back(fam);
        }

        return families;
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

        // Petit helper local: choisir les queues “best effort”
        struct SelectedQueues {
            uint32_t graphics = UINT32_MAX;
            uint32_t present = UINT32_MAX;
            uint32_t compute = UINT32_MAX;
            uint32_t transfer = UINT32_MAX;

            bool HasGraphicsPresent() const {
                return graphics != UINT32_MAX && present != UINT32_MAX;
            }
        };

        auto SelectQueues = [](const std::vector<VK_QueueFamily>& families) -> SelectedQueues
            {
                SelectedQueues out{};

                // Graphics
                for (const auto& f : families) {
                    if (f.queueCount > 0 && f.SupportsGraphics()) {
                        out.graphics = f.index;
                        break;
                    }
                }

                // Present
                for (const auto& f : families) {
                    if (f.queueCount > 0 && f.supportsPresentation) {
                        out.present = f.index;
                        break;
                    }
                }

                // Compute: prefer compute-only (no graphics)
                for (const auto& f : families) {
                    if (f.queueCount > 0 && f.SupportsCompute() && !f.SupportsGraphics()) {
                        out.compute = f.index;
                        break;
                    }
                }
                // fallback: any compute
                if (out.compute == UINT32_MAX) {
                    for (const auto& f : families) {
                        if (f.queueCount > 0 && f.SupportsCompute()) {
                            out.compute = f.index;
                            break;
                        }
                    }
                }

                // Transfer: prefer transfer-only (no graphics, no compute)
                for (const auto& f : families) {
                    if (f.queueCount > 0 && f.SupportsTransfer() && !f.SupportsGraphics() && !f.SupportsCompute()) {
                        out.transfer = f.index;
                        break;
                    }
                }
                // fallback: transfer without graphics
                if (out.transfer == UINT32_MAX) {
                    for (const auto& f : families) {
                        if (f.queueCount > 0 && f.SupportsTransfer() && !f.SupportsGraphics()) {
                            out.transfer = f.index;
                            break;
                        }
                    }
                }
                // fallback: any transfer
                if (out.transfer == UINT32_MAX) {
                    for (const auto& f : families) {
                        if (f.queueCount > 0 && f.SupportsTransfer()) {
                            out.transfer = f.index;
                            break;
                        }
                    }
                }

                // ultimate fallback
                if (out.compute == UINT32_MAX)  out.compute = out.graphics;
                if (out.transfer == UINT32_MAX) out.transfer = out.graphics;

                return out;
            };

        const bool requiresSwapchain = std::find(requiredDeviceExtensions.begin(), requiredDeviceExtensions.end(), VK_KHR_SWAPCHAIN_EXTENSION_NAME) != requiredDeviceExtensions.end();

        int bestScore = -1;
        VkPhysicalDevice bestDevice = VK_NULL_HANDLE;
        std::vector<VK_QueueFamily> bestFamilies;
        SelectedQueues bestSelected{};

        for (VkPhysicalDevice dev : devices)
        {
            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(dev, &props);

            // Query families + log
            std::vector<VK_QueueFamily> families = QueryQueueFamilies(dev, surface);

            NV_LOG_INFO((std::string("==== GPU Candidate: ") + props.deviceName + " ====").c_str());
            LogQueueFamilies(families);

            // Must have required extensions
            if (!HasDeviceExtensions(dev, requiredDeviceExtensions)) {
                NV_LOG_WARN("Skipping GPU: missing required device extensions.");
                continue;
            }

            // If swapchain required, must have swapchain surface support (format + present modes)
            if (requiresSwapchain && !HasSwapChainSupport(dev, surface)) {
                NV_LOG_WARN("Skipping GPU: swapchain support incomplete (no formats/present modes).");
                continue;
            }

            // Select queue families
            SelectedQueues selected = SelectQueues(families);

            // If swapchain, we must have graphics+present
            if (requiresSwapchain && !selected.HasGraphicsPresent()) {
                NV_LOG_WARN("Skipping GPU: missing required graphics/present queues.");
                continue;
            }

            // Scoring
            int score = 0;
            if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) score += 1000;
            score += static_cast<int>(props.limits.maxImageDimension2D);

            if (score > bestScore) {
                bestScore = score;
                bestDevice = dev;
                bestFamilies = std::move(families);
                bestSelected = selected;
            }
        }

        if (bestDevice == VK_NULL_HANDLE) {
            NV_LOG_ERROR("No suitable Vulkan physical device found.");
            return false;
        }

        // Commit
        m_PhysicalDevice = bestDevice;
        m_QueueFamilies = std::move(bestFamilies);

        m_GraphicsQueueFamily = bestSelected.graphics;
        m_PresentQueueFamily = bestSelected.present;
        m_ComputeQueueFamily = bestSelected.compute;
        m_TransferQueueFamily = bestSelected.transfer;

        vkGetPhysicalDeviceProperties(m_PhysicalDevice, &m_Properties);
        vkGetPhysicalDeviceFeatures(m_PhysicalDevice, &m_Features);
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &m_MemoryProperties);

        NV_LOG_INFO((std::string("Selected GPU: ") + m_Properties.deviceName).c_str());
        NV_LOG_INFO((std::string("Selected queue families: graphics=") + std::to_string(m_GraphicsQueueFamily) +
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

        if (m_GraphicsQueueFamily == UINT32_MAX) {
            NV_LOG_ERROR("CreateLogicalDevice failed: no graphics queue family selected");
            return false;
        }

        const float priority = 1.0f;

        // Unique queue family indices (a family may serve multiple roles)
        std::set<uint32_t> uniqueFamilies;
        if (m_GraphicsQueueFamily != UINT32_MAX) uniqueFamilies.insert(m_GraphicsQueueFamily);
        if (m_PresentQueueFamily != UINT32_MAX) uniqueFamilies.insert(m_PresentQueueFamily);
        if (m_ComputeQueueFamily != UINT32_MAX) uniqueFamilies.insert(m_ComputeQueueFamily);
        if (m_TransferQueueFamily != UINT32_MAX) uniqueFamilies.insert(m_TransferQueueFamily);

        std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
        queueCreateInfos.reserve(uniqueFamilies.size());

        for (uint32_t family : uniqueFamilies)
        {
            VkDeviceQueueCreateInfo qci{};
            qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qci.queueFamilyIndex = family;
            qci.queueCount = 1;
            qci.pQueuePriorities = &priority;
            queueCreateInfos.push_back(qci);
        }

        // Enable only what you need here (keep empty for now)
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

        // Retrieve queues
        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);

        if (m_PresentQueueFamily != UINT32_MAX)
            vkGetDeviceQueue(m_Device, m_PresentQueueFamily, 0, &m_PresentQueue);

        if (m_ComputeQueueFamily != UINT32_MAX)
            vkGetDeviceQueue(m_Device, m_ComputeQueueFamily, 0, &m_ComputeQueue);

        if (m_TransferQueueFamily != UINT32_MAX)
            vkGetDeviceQueue(m_Device, m_TransferQueueFamily, 0, &m_TransferQueue);

        NV_LOG_INFO("Vulkan logical device created.");
        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan