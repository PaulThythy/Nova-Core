#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Core/Log.h"

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

namespace Nova::Core::Renderer::Backends::Vulkan {

    VmaAllocator ToVmaAllocator(void* allocator) {
        return static_cast<VmaAllocator>(allocator);
    }

    VmaAllocation ToVmaAllocation(void* allocation) {
        return static_cast<VmaAllocation>(allocation);
    }

    void FillAllocationCreateInfo(VK_MemoryLocation location, VmaAllocationCreateInfo& out) {
        out = {};
        switch (location) {
            case VK_MemoryLocation::GpuOnly:
                out.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
                break;
            case VK_MemoryLocation::CpuToGpu:
                out.usage = VMA_MEMORY_USAGE_AUTO;
                out.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
                break;
            case VK_MemoryLocation::CpuReadWrite:
                out.usage = VMA_MEMORY_USAGE_AUTO;
                out.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT;
                out.requiredFlags = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                out.preferredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
                break;
        }
    }

    bool VK_MemoryAllocator::Create(const VkInstance instance, const VkPhysicalDevice physicalDevice, const VkDevice device, const uint32_t vulkanApiVersion) {
        Destroy();

        if (instance == VK_NULL_HANDLE || physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE)
            return false;

        VmaAllocatorCreateInfo allocatorInfo{};
        allocatorInfo.vulkanApiVersion = vulkanApiVersion;
        allocatorInfo.physicalDevice = physicalDevice;
        allocatorInfo.device = device;
        allocatorInfo.instance = instance;

        VmaAllocator allocator = VK_NULL_HANDLE;
        const VkResult res = vmaCreateAllocator(&allocatorInfo, &allocator);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            NV_LOG_ERROR("VK_MemoryAllocator::Create - vmaCreateAllocator failed");
            return false;
        }

        m_Allocator = allocator;
        m_Device = device;
        return true;
    }

    void VK_MemoryAllocator::Destroy() {
        if (m_Allocator != nullptr) {
            vmaDestroyAllocator(ToVmaAllocator(m_Allocator));
            m_Allocator = nullptr;
        }
        m_Device = VK_NULL_HANDLE;
    }

    bool VK_MemoryAllocator::IsValid() const {
        return m_Allocator != nullptr && m_Device != VK_NULL_HANDLE;
    }

    bool VK_MemoryAllocator::CreateBuffer(const VkDeviceSize size, const VkBufferUsageFlags usage, const VK_MemoryLocation location, VK_BufferAllocation& out) {
        if (!IsValid() || size == 0)
            return false;

        DestroyBuffer(out);

        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VmaAllocationCreateInfo allocInfo{};
        FillAllocationCreateInfo(location, allocInfo);

        VmaAllocation allocation = VK_NULL_HANDLE;
        VkBuffer buffer = VK_NULL_HANDLE;
        const VkResult res = vmaCreateBuffer(ToVmaAllocator(m_Allocator), &bufferInfo, &allocInfo, &buffer,
                                             &allocation, nullptr);
        CheckVkResult(res);
        if (res != VK_SUCCESS)
            return false;

        out.buffer = buffer;
        out.m_Allocation = allocation;
        return true;
    }

    void VK_MemoryAllocator::DestroyBuffer(VK_BufferAllocation& allocation) {
        if (!IsValid() || !allocation.IsValid())
            return;

        vmaDestroyBuffer(ToVmaAllocator(m_Allocator), allocation.buffer, ToVmaAllocation(allocation.m_Allocation));
        allocation.buffer = VK_NULL_HANDLE;
        allocation.m_Allocation = nullptr;
    }

    bool VK_MemoryAllocator::CreateImage(const VkImageCreateInfo& imageInfo, const VK_MemoryLocation location, VK_ImageAllocation& out) {
        if (!IsValid())
            return false;

        DestroyImage(out);

        VmaAllocationCreateInfo allocInfo{};
        FillAllocationCreateInfo(location, allocInfo);

        VmaAllocation allocation = VK_NULL_HANDLE;
        VkImage image = VK_NULL_HANDLE;
        const VkResult res = vmaCreateImage(ToVmaAllocator(m_Allocator), &imageInfo, &allocInfo, &image, &allocation, nullptr);
        CheckVkResult(res);
        if (res != VK_SUCCESS)
            return false;

        out.image = image;
        out.m_Allocation = allocation;
        return true;
    }

    void VK_MemoryAllocator::DestroyImage(VK_ImageAllocation& allocation) {
        if (!IsValid() || !allocation.IsValid())
            return;

        vmaDestroyImage(ToVmaAllocator(m_Allocator), allocation.image, ToVmaAllocation(allocation.m_Allocation));
        allocation.image = VK_NULL_HANDLE;
        allocation.m_Allocation = nullptr;
    }

    void* VK_MemoryAllocator::Map(const VK_BufferAllocation& allocation, const VkDeviceSize offset, const VkDeviceSize size) {
        (void)size;
        if (!IsValid() || !allocation.IsValid())
            return nullptr;

        void* mapped = nullptr;
        const VkResult res = vmaMapMemory(ToVmaAllocator(m_Allocator), ToVmaAllocation(allocation.m_Allocation), &mapped);
        CheckVkResult(res);
        if (res != VK_SUCCESS)
            return nullptr;

        return static_cast<char*>(mapped) + offset;
    }

    void VK_MemoryAllocator::Unmap(const VK_BufferAllocation& allocation) {
        if (!IsValid() || !allocation.IsValid())
            return;

        vmaUnmapMemory(ToVmaAllocator(m_Allocator), ToVmaAllocation(allocation.m_Allocation));
    }

    void VK_MemoryAllocator::WriteToBuffer(const VK_BufferAllocation& allocation, const VkDeviceSize offset, const VkDeviceSize size, const void* src) {
        if (!src || size == 0 || !IsValid() || !allocation.IsValid())
            return;

        const VkResult res = vmaCopyMemoryToAllocation(
            ToVmaAllocator(m_Allocator),
            src,
            ToVmaAllocation(allocation.m_Allocation),
            offset,
            size);
        CheckVkResult(res);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan