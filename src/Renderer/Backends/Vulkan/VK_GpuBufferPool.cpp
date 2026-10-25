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

    bool VK_GpuBufferPool::CreateEntry(RHI::RHI_ResourceKind kind, size_t elementSize, uint32_t elementCount,
                                       bool perFrameInFlight, std::string debugName, Entry& outEntry) const {
        const bool isConstantBuffer = (kind == RHI::RHI_ResourceKind::ConstantBuffer);
        const VkDeviceSize align = isConstantBuffer ? m_UboAlign : m_SsboAlign;

        Entry entry{};
        entry.m_Kind = kind;
        entry.m_ElementSize = elementSize;
        entry.m_ElementStride = isConstantBuffer
            ? AlignUp(static_cast<VkDeviceSize>(elementSize), align)
            : static_cast<VkDeviceSize>(elementSize);
        entry.m_ElementCount = elementCount;
        entry.m_PerFrameInFlight = perFrameInFlight;
        // Frame regions start at dynamic offsets, which must be multiples of the offset alignment.
        entry.m_FrameRegionStride = AlignUp(entry.m_ElementStride * static_cast<VkDeviceSize>(elementCount), align);
        entry.m_DebugName = std::move(debugName);

        const uint32_t frameMultiplier = entry.m_PerFrameInFlight ? m_FramesInFlight : 1u;
        const VkDeviceSize totalSize = entry.m_FrameRegionStride * static_cast<VkDeviceSize>(frameMultiplier);

        VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        usage |= isConstantBuffer ? VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT : VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;

        if (!m_Allocator->CreateBuffer(totalSize, usage, VK_MemoryLocation::CpuReadWrite, entry.m_Buffer)) {
            NV_LOG_ERROR(std::string("VK_GpuBufferPool - failed to allocate buffer '") + entry.m_DebugName + "'");
            return false;
        }

        entry.m_DynamicCursor.assign(m_FramesInFlight, 0);
        outEntry = std::move(entry);
        return true;
    }

    RHI::RHI_GpuBufferHandle VK_GpuBufferPool::Create(RHI::RHI_ResourceKind kind, const RHI::RHI_GpuBufferDesc& desc) {
        if (!m_Allocator || desc.m_ElementSize == 0 || desc.m_ElementCount == 0)
            return RHI::RHI_GpuBufferHandle::Invalid();

        Entry entry{};
        if (!CreateEntry(kind, desc.m_ElementSize, desc.m_ElementCount, desc.m_PerFrameInFlight,
                         desc.m_DebugName ? desc.m_DebugName : "", entry))
            return RHI::RHI_GpuBufferHandle::Invalid();

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

    bool VK_GpuBufferPool::Resize(RHI::RHI_GpuBufferHandle handle, uint32_t elementCount) {
        Entry* entry = FindEntry(handle);
        if (!entry || elementCount == 0)
            return false;
        if (elementCount == entry->m_ElementCount)
            return true;

        Entry resized{};
        if (!CreateEntry(entry->m_Kind, entry->m_ElementSize, elementCount, entry->m_PerFrameInFlight, entry->m_DebugName, resized))
            return false;

        m_Allocator->DestroyBuffer(entry->m_Buffer);
        *entry = std::move(resized);
        return true;
    }

    uint32_t VK_GpuBufferPool::GetElementCount(RHI::RHI_GpuBufferHandle handle) const {
        const Entry* entry = FindEntry(handle);
        return entry ? entry->m_ElementCount : 0;
    }

    void VK_GpuBufferPool::Update(RHI::RHI_GpuBufferHandle handle, const void* data, size_t size, uint32_t elementIndex, uint32_t currentFrame) {
        Entry* entry = FindEntry(handle);
        if (!entry || !data || size == 0 || elementIndex >= entry->m_ElementCount)
            return;

        const VkDeviceSize offsetInRegion = static_cast<VkDeviceSize>(elementIndex) * entry->m_ElementStride;
        VkDeviceSize writeSize = static_cast<VkDeviceSize>(size);
        if (offsetInRegion + writeSize > entry->m_FrameRegionStride) {
            if (!entry->m_WarnedOverflow) {
                NV_LOG_WARN(std::string("VK_GpuBufferPool::Update - write past the end of '") + entry->m_DebugName
                    + "' clamped (grow the buffer first)");
                entry->m_WarnedOverflow = true;
            }
            writeSize = entry->m_FrameRegionStride - offsetInRegion;
        }

        m_Allocator->WriteToBuffer(entry->m_Buffer, FrameBase(*entry, currentFrame) + offsetInRegion, writeSize, data);
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