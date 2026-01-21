#ifndef VK_DEVICE_H
#define VK_DEVICE_H

#include <vulkan/vulkan.h>
#include <vector>
#include <optional>
#include <set>

#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Backends/Vulkan/VK_Extensions.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Device {
    public:
        VK_Device() = default;
        ~VK_Device() = default;

        bool Create(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions);
        void Destroy();

        VkPhysicalDevice GetPhysicalDevice() const { return m_PhysicalDevice; }
        VkDevice GetDevice() const { return m_Device; }

        uint32_t GetGraphicsQueueFamily() const { return m_GraphicsQueueFamily; }
        uint32_t GetPresentQueueFamily()  const { return m_PresentQueueFamily; }
        uint32_t GetComputeQueueFamily()  const { return m_ComputeQueueFamily; }
        uint32_t GetTransferQueueFamily() const { return m_TransferQueueFamily; }

        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        VkQueue GetPresentQueue()  const { return m_PresentQueue; }
        VkQueue GetComputeQueue()  const { return m_ComputeQueue; }
        VkQueue GetTransferQueue() const { return m_TransferQueue; }

        const VkPhysicalDeviceProperties&       GetProperties() const { return m_Properties; }
        const VkPhysicalDeviceFeatures&         GetFeatures() const { return m_Features; }
        const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_MemoryProperties; }

        struct VK_QueueFamily {
            uint32_t   index = UINT32_MAX;
            VkQueueFlags flags = 0; // GRAPHICS/COMPUTE/TRANSFER/SPARSE + video/optical if available
            uint32_t   queueCount = 0;

            uint32_t   timestampValidBits = 0;
            VkExtent3D minImageTransferGranularity{ 0, 0, 0 };

            bool       supportsPresentation = false;

            // Helpers
            bool SupportsGraphics() const { return (flags & VK_QUEUE_GRAPHICS_BIT) != 0; }
            bool SupportsCompute()  const { return (flags & VK_QUEUE_COMPUTE_BIT) != 0; }
            bool SupportsTransfer() const { return (flags & VK_QUEUE_TRANSFER_BIT) != 0; }
            bool SupportsSparse()   const { return (flags & VK_QUEUE_SPARSE_BINDING_BIT) != 0; }

#if defined(VK_QUEUE_VIDEO_DECODE_BIT_KHR)
            bool SupportsVideoDecode() const { return (flags & VK_QUEUE_VIDEO_DECODE_BIT_KHR) != 0; }
#else
            bool SupportsVideoDecode() const { return false; }
#endif

#if defined(VK_QUEUE_VIDEO_ENCODE_BIT_KHR)
            bool SupportsVideoEncode() const { return (flags & VK_QUEUE_VIDEO_ENCODE_BIT_KHR) != 0; }
#else
            bool SupportsVideoEncode() const { return false; }
#endif

#if defined(VK_QUEUE_OPTICAL_FLOW_BIT_NV)
            bool SupportsOpticalFlow() const { return (flags & VK_QUEUE_OPTICAL_FLOW_BIT_NV) != 0; }
#else
            bool SupportsOpticalFlow() const { return false; }
#endif
        };

        const std::vector<VK_QueueFamily>& GetQueueFamilies() const { return m_QueueFamilies; }
        const VK_QueueFamily* GetQueueFamily(uint32_t familyIndex) const;

    private:
        
        bool HasSwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;
        bool PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions);
        bool CreateLogicalDevice(const std::vector<const char*>& requiredDeviceExtensions);

        std::vector<VK_QueueFamily> QueryQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;

        void LogQueueFamilies(const std::vector<VK_QueueFamily>& families) const;

        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         m_Device = VK_NULL_HANDLE;

        std::vector<VK_QueueFamily> m_QueueFamilies;

        uint32_t m_GraphicsQueueFamily = UINT32_MAX;
        uint32_t m_PresentQueueFamily = UINT32_MAX;
        uint32_t m_ComputeQueueFamily = UINT32_MAX;
        uint32_t m_TransferQueueFamily = UINT32_MAX;

        VkQueue  m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue  m_PresentQueue = VK_NULL_HANDLE;
        VkQueue  m_ComputeQueue = VK_NULL_HANDLE;
        VkQueue  m_TransferQueue = VK_NULL_HANDLE;

        VkPhysicalDeviceProperties       m_Properties{};
        VkPhysicalDeviceFeatures         m_Features{};
        VkPhysicalDeviceMemoryProperties m_MemoryProperties{};
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_DEVICE_H