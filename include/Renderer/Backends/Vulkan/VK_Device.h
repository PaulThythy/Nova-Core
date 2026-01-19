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
        uint32_t GetPresentQueueFamily() const { return m_PresentQueueFamily; }
        uint32_t GetComputeQueueFamily() const { return m_ComputeQueueFamily; }
        uint32_t GetTransferQueueFamily() const { return m_TransferQueueFamily; }

        VkQueue GetGraphicsQueue() const { return m_GraphicsQueue; }
        VkQueue GetPresentQueue()  const { return m_PresentQueue; }
        VkQueue GetComputeQueue()  const { return m_ComputeQueue; }
        VkQueue GetTransferQueue() const { return m_TransferQueue; }

        bool HasDedicatedComputeQueue() const { return m_ComputeQueueFamily != m_GraphicsQueueFamily; }
        bool HasDedicatedTransferQueue() const { return m_TransferQueueFamily != m_GraphicsQueueFamily && m_TransferQueueFamily != m_ComputeQueueFamily; }

        const VkPhysicalDeviceProperties&       GetProperties() const { return m_Properties; }
        const VkPhysicalDeviceFeatures&         GetFeatures() const { return m_Features; }
        const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_MemoryProperties; }

    private:
        struct VK_QueueFamilyIndices {
            std::optional<uint32_t> m_Graphics;
            std::optional<uint32_t> m_Present;
            std::optional<uint32_t> m_Compute;
            std::optional<uint32_t> m_Transfer;

            bool IsComplete() const {
                return m_Graphics.has_value() && m_Present.has_value();
            }
        };

        VK_QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;
        bool HasSwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;
        bool PickPhysicalDevice(VkInstance instance, VkSurfaceKHR surface, const std::vector<const char*>& requiredDeviceExtensions);
        bool CreateLogicalDevice(const std::vector<const char*>& requiredDeviceExtensions);

        VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice         m_Device = VK_NULL_HANDLE;

        uint32_t m_GraphicsQueueFamily = UINT32_MAX;
        uint32_t m_PresentQueueFamily  = UINT32_MAX;
        uint32_t m_ComputeQueueFamily  = UINT32_MAX;
        uint32_t m_TransferQueueFamily = UINT32_MAX;

        VkQueue  m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue  m_PresentQueue  = VK_NULL_HANDLE;
        VkQueue  m_ComputeQueue  = VK_NULL_HANDLE;
        VkQueue  m_TransferQueue = VK_NULL_HANDLE;

        VkPhysicalDeviceProperties       m_Properties{};
        VkPhysicalDeviceFeatures         m_Features{};
        VkPhysicalDeviceMemoryProperties m_MemoryProperties{};
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_DEVICE_H