#ifndef VK_MEMORY_ALLOCATOR_H
#define VK_MEMORY_ALLOCATOR_H

#include <vulkan/vulkan.h>
#include <cstdint>

#include "Api.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    enum class VK_MemoryLocation : uint8_t {
        GpuOnly,
        CpuToGpu,
        CpuReadWrite
    };

    struct NV_API VK_BufferAllocation {
        VkBuffer buffer = VK_NULL_HANDLE;

        bool IsValid() const { return buffer != VK_NULL_HANDLE; }

    private:
        friend class VK_MemoryAllocator;
        void* m_Allocation = nullptr;
    };

    struct NV_API VK_ImageAllocation {
        VkImage image = VK_NULL_HANDLE;

        bool IsValid() const { return image != VK_NULL_HANDLE; }

    private:
        friend class VK_MemoryAllocator;
        void* m_Allocation = nullptr;
    };

    class NV_API VK_MemoryAllocator {
    public:
        VK_MemoryAllocator() = default;
        ~VK_MemoryAllocator() { Destroy(); }

        VK_MemoryAllocator(const VK_MemoryAllocator&) = delete;
        VK_MemoryAllocator& operator=(const VK_MemoryAllocator&) = delete;

        bool Create(VkInstance instance, VkPhysicalDevice physicalDevice, VkDevice device, uint32_t vulkanApiVersion = VK_API_VERSION_1_1);
        void Destroy();

        bool IsValid() const;

        VkDevice GetDevice() const { return m_Device; }

        bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VK_MemoryLocation location, VK_BufferAllocation& out);
        void DestroyBuffer(VK_BufferAllocation& allocation);

        bool CreateImage(const VkImageCreateInfo& imageInfo, VK_MemoryLocation location, VK_ImageAllocation& out);
        void DestroyImage(VK_ImageAllocation& allocation);

        void* Map(const VK_BufferAllocation& allocation, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);
        void Unmap(const VK_BufferAllocation& allocation);

        void WriteToBuffer(const VK_BufferAllocation& allocation, VkDeviceSize offset, VkDeviceSize size, const void* src);

    private:
        void* m_Allocator = nullptr;
        VkDevice m_Device = VK_NULL_HANDLE;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_MEMORY_ALLOCATOR_H