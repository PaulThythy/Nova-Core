#include "Renderer/Backends/Vulkan/VK_GpuBufferPool.h"

#include "Core/Log.h"

#include <algorithm>

namespace Nova::Core::Renderer::Backends::Vulkan {

    VkDeviceSize VK_GpuBufferPool::AlignUp(VkDeviceSize value, VkDeviceSize alignment) {
        return (alignment > 0) ? ((value + alignment - 1) / alignment) * alignment : value;
    }

    VkDeviceSize VK_GpuBufferPool::FrameBase(const Entry& entry, uint32_t currentFrame) const {
        if (!entry.m_PerFrameInFlight) return 0;
        const uint32_t slot = (m_FramesInFlight > 0) ? (currentFrame % m_FramesInFlight) : 0;
        return static_cast<VkDeviceSize>(slot) * entry.m_FrameRegionStride;
    }

    const VK_GpuBufferPool::Entry* VK_GpuBufferPool::FindEntry(RHI::RHI_GpuBufferHandle handle) const {
        if (!handle.IsValid() || handle.m_Index >= m_Entries.size()) return nullptr;
        const Entry& e = m_Entries[handle.m_Index];
        return e.m_Buffer.IsValid() ? &e : nullptr;
    }

    VK_GpuBufferPool::Entry* VK_GpuBufferPool::FindEntry(RHI::RHI_GpuBufferHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Entries.size()) return nullptr;
        Entry& e = m_Entries[handle.m_Index];
        return e.m_Buffer.IsValid() ? &e : nullptr;
    }

    void VK_GpuBufferPool::Init(VkPhysicalDevice physicalDevice, VK_MemoryAllocator* allocator, uint32_t framesInFlight) {
        DestroyAll();
        m_Allocator = allocator;
        m_FramesInFlight = std::max(1u, framesInFlight);

        VkPhysicalDeviceProperties props{};
        if (physicalDevice != VK_NULL_HANDLE)
            vkGetPhysicalDeviceProperties(physicalDevice, &props);

        m_UboAlign = std::max<VkDeviceSize>(1, props.limits.minUniformBufferOffsetAlignment);
        m_SsboAlign = std::max<VkDeviceSize>(1, props.limits.minStorageBufferOffsetAlignment);
    }

    void VK_GpuBufferPool::DestroyAll() {
        if (m_Allocator) {
            for (auto& entry : m_Entries)
                m_Allocator->DestroyBuffer(entry.m_Buffer);
        }
        m_Entries.clear();
    }

    RHI::RHI_GpuBufferHandle VK_GpuBufferPool::Create(RHI::RHI_ResourceKind kind, const RHI::RHI_GpuBufferDesc& desc) {
        if (!m_Allocator || desc.m_ElementSize == 0 || desc.m_ElementCount == 0)
            return RHI::RHI_GpuBufferHandle::Invalid();

        const VkDeviceSize align = (kind == RHI::RHI_ResourceKind::ConstantBuffer) ? m_UboAlign : m_SsboAlign;

        Entry entry{};
        entry.m_Kind = kind;
        entry.m_ElementSize = desc.m_ElementSize;
        entry.m_ElementStride = AlignUp(static_cast<VkDeviceSize>(desc.m_ElementSize), align);
        entry.m_ElementCount = desc.m_ElementCount;
        entry.m_PerFrameInFlight = desc.m_PerFrameInFlight;
        entry.m_FrameRegionStride = entry.m_ElementStride * static_cast<VkDeviceSize>(entry.m_ElementCount);
        entry.m_DebugName = desc.m_DebugName ? desc.m_DebugName : "";

        const uint32_t frameMultiplier = entry.m_PerFrameInFlight ? m_FramesInFlight : 1u;
        const VkDeviceSize totalSize = entry.m_FrameRegionStride * static_cast<VkDeviceSize>(frameMultiplier);

        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        usage |= (kind == RHI::RHI_ResourceKind::ConstantBuffer)
            ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
            : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        if (!m_Allocator->CreateBuffer(totalSize, usage, VK_MemoryLocation::CpuReadWrite, entry.m_Buffer)) {
            NV_LOG_ERROR(std::string("VK_GpuBufferPool::Create - failed to allocate buffer '") + entry.m_DebugName + "'");
            return RHI::RHI_GpuBufferHandle::Invalid();
        }

        entry.m_DynamicCursor.assign(m_FramesInFlight, 0);

        m_Entries.push_back(std::move(entry));

        RHI::RHI_GpuBufferHandle handle{};
        handle.m_Index = static_cast<uint32_t>(m_Entries.size() - 1);
        return handle;
    }

    void VK_GpuBufferPool::Destroy(RHI::RHI_GpuBufferHandle handle) {
        if (!m_Allocator || !handle.IsValid() || handle.m_Index >= m_Entries.size())
            return;
        m_Allocator->DestroyBuffer(m_Entries[handle.m_Index].m_Buffer);
    }

    void VK_GpuBufferPool::Update(RHI::RHI_GpuBufferHandle handle, const void* data, size_t size, uint32_t elementIndex, uint32_t currentFrame) {
        Entry* entry = FindEntry(handle);
        if (!entry || !data || size == 0 || elementIndex >= entry->m_ElementCount)
            return;

        const VkDeviceSize offset = FrameBase(*entry, currentFrame) + static_cast<VkDeviceSize>(elementIndex) * entry->m_ElementStride;
        m_Allocator->WriteToBuffer(entry->m_Buffer, offset, static_cast<VkDeviceSize>(size), data);
    }

    RHI::RHI_BufferBinding VK_GpuBufferPool::ResolveBinding(RHI::RHI_GpuBufferHandle handle, uint32_t elementIndex, uint32_t currentFrame) const {
        RHI::RHI_BufferBinding binding{};
        const Entry* entry = FindEntry(handle);
        if (!entry || elementIndex >= entry->m_ElementCount)
            return binding;

        binding.m_Handle = reinterpret_cast<uint64_t>(entry->m_Buffer.buffer);
        binding.m_Offset = static_cast<uint64_t>(FrameBase(*entry, currentFrame) + static_cast<VkDeviceSize>(elementIndex) * entry->m_ElementStride);
        binding.m_Range = static_cast<uint64_t>(entry->m_ElementSize);
        return binding;
    }

    VkDeviceSize VK_GpuBufferPool::WriteNextDynamicElement(RHI::RHI_GpuBufferHandle handle, const void* data, size_t size, uint32_t currentFrame) {
        Entry* entry = FindEntry(handle);
        if (!entry || !data || size == 0)
            return 0;

        const uint32_t slot = (m_FramesInFlight > 0) ? (currentFrame % m_FramesInFlight) : 0;
        if (slot >= entry->m_DynamicCursor.size())
            return FrameBase(*entry, currentFrame);

        VkDeviceSize& cursor = entry->m_DynamicCursor[slot];
        if (cursor + static_cast<VkDeviceSize>(size) > entry->m_FrameRegionStride) {
            NV_LOG_WARN(std::string("VK_GpuBufferPool::WriteNextDynamicElement - ring buffer overflow ('") + entry->m_DebugName + "')");
            return FrameBase(*entry, currentFrame);
        }

        const VkDeviceSize absoluteOffset = FrameBase(*entry, currentFrame) + cursor;
        m_Allocator->WriteToBuffer(entry->m_Buffer, absoluteOffset, static_cast<VkDeviceSize>(size), data);
        cursor += entry->m_ElementStride;
        return absoluteOffset;
    }

    void VK_GpuBufferPool::ResetDynamicCursors(uint32_t currentFrame) {
        const uint32_t slot = (m_FramesInFlight > 0) ? (currentFrame % m_FramesInFlight) : 0;
        for (auto& entry : m_Entries) {
            if (slot < entry.m_DynamicCursor.size())
                entry.m_DynamicCursor[slot] = 0;
        }
    }

    bool VK_GpuBufferPool::GetDescriptorInfo(RHI::RHI_GpuBufferHandle handle, VkBuffer& outBuffer, VkDeviceSize& outRange) const {
        const Entry* entry = FindEntry(handle);
        if (!entry) return false;
        outBuffer = entry->m_Buffer.buffer;
        // StructuredBuffer: bind the whole per-frame array so shaders can index [0..N).
        // ConstantBuffer: one element; dynamic offset selects the frame/draw region.
        if (entry->m_Kind == RHI::RHI_ResourceKind::StructuredBuffer
            || entry->m_Kind == RHI::RHI_ResourceKind::RWStructuredBuffer) {
            outRange = entry->m_FrameRegionStride;
        } else {
            outRange = static_cast<VkDeviceSize>(entry->m_ElementSize);
        }
        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan