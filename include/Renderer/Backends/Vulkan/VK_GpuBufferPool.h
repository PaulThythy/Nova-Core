#ifndef VK_GPU_BUFFER_POOL_H
#define VK_GPU_BUFFER_POOL_H

#include <cstdint>
#include <string>
#include <vector>

#include <vulkan/vulkan.h>

#include "Api.h"
#include "Renderer/RHI/RHI_GpuBuffer.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"
#include "Renderer/RHI/RHI_ShaderResourceSet.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    /**
     * Owns every GPU buffer created through `IRenderer::CreateConstantBuffer` / `CreateStructuredBuffer` /
     * `CreateRWStructuredBuffer` (Vulkan backend). Backs Slang's `ConstantBuffer<T>` / `StructuredBuffer<T>` /
     * `RWStructuredBuffer<T>`. Handles per-frame-in-flight duplication so a buffer written this frame never
     * overwrites data a previous frame is still reading on the GPU.
     *
     * Two update paths are supported:
     * - `Update()`: caller picks the element index explicitly (plain buffers, e.g. a single
     *   `ConstantBuffer<T>` or a `StructuredBuffer<T>` indexed by meaningful data). This is what
     *   `IRenderer::UpdateGpuBuffer` uses, for both engine and App-created buffers.
     * - `WriteNextDynamicElement()`: engine-only per-draw ring allocation (used for `nova.mvp` /
     *   `nova.material`, which get one element per draw call within the current frame). Not exposed
     *   on the abstract `IRenderer`; only `VK_Shaders`/`VK_PipelineCache` use it directly.
     */
    class NV_API VK_GpuBufferPool {
    public:
        void Init(VkPhysicalDevice physicalDevice, VK_MemoryAllocator* allocator, uint32_t framesInFlight);
        void DestroyAll();

        RHI::RHI_GpuBufferHandle Create(RHI::RHI_ResourceKind kind, const RHI::RHI_GpuBufferDesc& desc);
        void Destroy(RHI::RHI_GpuBufferHandle handle);

        /** Write `size` bytes at `elementIndex`, into the region of the given frame-in-flight. */
        void Update(RHI::RHI_GpuBufferHandle handle, const void* data, size_t size, uint32_t elementIndex, uint32_t currentFrame);
        /** Resolve to a bindable (VkBuffer/offset/range) for `elementIndex` at the given frame-in-flight. */
        RHI::RHI_BufferBinding ResolveBinding(RHI::RHI_GpuBufferHandle handle, uint32_t elementIndex, uint32_t currentFrame) const;

        /**
         * Write `data` into the next free element of this frame's region (auto-incrementing ring
         * cursor) and return the absolute byte offset to use as a Vulkan dynamic UBO offset.
         */
        VkDeviceSize WriteNextDynamicElement(RHI::RHI_GpuBufferHandle handle, const void* data, size_t size, uint32_t currentFrame);
        /** Reset the per-draw ring cursor for the given frame-in-flight (call once per frame). */
        void ResetDynamicCursors(uint32_t currentFrame);

        /** One-time descriptor setup info; the actual per-frame/per-draw offset is supplied later as a dynamic offset. */
        bool GetDescriptorInfo(RHI::RHI_GpuBufferHandle handle, VkBuffer& outBuffer, VkDeviceSize& outRange) const;

    private:
        struct Entry {
            VK_BufferAllocation m_Buffer{};
            size_t m_ElementSize = 0;
            VkDeviceSize m_ElementStride = 0; // aligned to UBO/SSBO offset alignment
            uint32_t m_ElementCount = 1;
            bool m_PerFrameInFlight = true;
            VkDeviceSize m_FrameRegionStride = 0; // m_ElementStride * m_ElementCount
            std::vector<VkDeviceSize> m_DynamicCursor; // one per frame-in-flight
            std::string m_DebugName;
        };

        static VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment);
        VkDeviceSize FrameBase(const Entry& entry, uint32_t currentFrame) const;
        const Entry* FindEntry(RHI::RHI_GpuBufferHandle handle) const;
        Entry* FindEntry(RHI::RHI_GpuBufferHandle handle);

        VK_MemoryAllocator* m_Allocator = nullptr;
        uint32_t m_FramesInFlight = 1;
        VkDeviceSize m_UboAlign = 1;
        VkDeviceSize m_SsboAlign = 1;
        std::vector<Entry> m_Entries;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_GPU_BUFFER_POOL_H