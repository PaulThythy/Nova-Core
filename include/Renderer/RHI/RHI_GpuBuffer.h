#ifndef RHI_GPU_BUFFER_H
#define RHI_GPU_BUFFER_H

#include <cstddef>
#include <cstdint>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    /**
     * Backend-agnostic description of a GPU-visible buffer created through
     * `IRenderer::CreateConstantBuffer` / `CreateStructuredBuffer` / `CreateRWStructuredBuffer`.
     *
     * These map 1:1 onto Slang's own resource generics declared in shaders (see
     * NovaUniforms.slang): `ConstantBuffer<T>`, `StructuredBuffer<T>` and `RWStructuredBuffer<T>`.
     * Which Slang type a given buffer is meant to back is determined by which `Create*` function
     * was used, not by this descriptor (kept identical across the three for simplicity).
     */
    struct NV_API RHI_GpuBufferDesc {
        /** Size in bytes of a single element (sizeof(T) for a Slang `ConstantBuffer<T>` etc.). */
        size_t m_ElementSize = 0;
        /** Number of elements. 1 for a plain `ConstantBuffer<T>`; >1 for an array (e.g. one region per draw call). */
        uint32_t m_ElementCount = 1;
        /** When true, the buffer is internally duplicated per frame-in-flight so CPU writes for
         * frame N+1 never race with GPU reads of frame N still in flight. Almost always desired
         * for buffers written every frame; set to false only for data uploaded once. */
        bool m_PerFrameInFlight = true;
        /** Optional label used for logging/debugging. */
        const char* m_DebugName = nullptr;
    };

    /** Opaque handle to a buffer created via `IRenderer::CreateConstantBuffer` and friends. */
    struct NV_API RHI_GpuBufferHandle {
        uint32_t m_Index = UINT32_MAX;
        bool IsValid() const { return m_Index != UINT32_MAX; }
        static RHI_GpuBufferHandle Invalid() { return {}; }
        friend bool operator==(const RHI_GpuBufferHandle& a, const RHI_GpuBufferHandle& b) { return a.m_Index == b.m_Index; }
        friend bool operator!=(const RHI_GpuBufferHandle& a, const RHI_GpuBufferHandle& b) { return !(a == b); }
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_GPU_BUFFER_H