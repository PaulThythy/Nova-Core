#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

#include <memory>
#include <cstdint>
#include <unordered_map>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_Mesh.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/RHI/RHI_RenderGraph.h"
#include "Renderer/RHI/RHI_GpuBuffer.h"
#include "Renderer/RHI/RHI_ShaderResourceSet.h"

namespace Nova::Core::Renderer::RHI {

    enum class RHI_PresentMode {
        Default,
        LowLatency,
        Immediate
    };

    struct NV_API RHI_SwapchainDesc {
        uint32_t m_FramesInFlight = 3;
        bool m_CreateSurface = true;
        bool m_EnableSwapchain = true;
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        RHI_PresentMode m_PreferredPresentMode = RHI_PresentMode::LowLatency;
    };

    enum class RHI_PrimitiveTopology {
        Triangles,
        Lines,
        Points
    };

    enum class RHI_IndexType {
        UInt16,
        UInt32
    };

    struct NV_API RHI_DrawCommand {
        std::shared_ptr<Renderer::RHI::RHI_Mesh> m_Mesh;
        RHI_PrimitiveTopology m_Topology = RHI_PrimitiveTopology::Triangles;
        uint32_t m_VertexCount = 0;
        uint32_t m_FirstVertex = 0;
        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
    };

    struct NV_API RHI_DrawIndexedCommand {
        std::shared_ptr<Renderer::RHI::RHI_Mesh> m_Mesh;
        RHI_PrimitiveTopology m_Topology = RHI_PrimitiveTopology::Triangles;
        RHI_IndexType m_IndexType = RHI_IndexType::UInt32;
        uint32_t m_IndexCount = 0;
        uint32_t m_FirstIndex = 0;
        int32_t  m_VertexOffset = 0;
        uint32_t m_InstanceCount = 1;
        uint32_t m_FirstInstance = 0;
    };

    class NV_API IRenderer {
    public:
        virtual ~IRenderer() = default;

        static std::unique_ptr<IRenderer> Create(
            Core::GraphicsAPI api,
            const RHI_SwapchainDesc& desc = RHI_SwapchainDesc{});

        virtual bool Create(const RHI_SwapchainDesc& desc) = 0;
        virtual void Destroy() = 0;
        virtual bool Resize(int w, int h) = 0;
        virtual void Update(float dt) = 0;

        /** Begin the frame. Call during OnBegin. */
        virtual void BeginFrame() = 0;
        /** Render the frame. Call during OnRender. */
        virtual void RenderFrame() = 0;
        /** End the frame. Call during OnEnd. */
        virtual void EndFrame() = 0;

        virtual void SetRenderGraph(std::unique_ptr<IRenderGraph> graph) = 0;
        virtual IRenderGraph* GetRenderGraph() const = 0;

        virtual void Draw(const RHI_DrawCommand& cmd) = 0;
        virtual void DrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;

        /** Upload a CPU mesh to GPU (or return a cached GPU mesh). Backend-specific. */
        virtual std::shared_ptr<RHI_Mesh> GetOrUploadMesh(const std::shared_ptr<RHI_Mesh>& cpuMesh) = 0;

        /** Returns an ImGui texture identifier for a sampled render graph texture. */
        virtual void* GetTextureImGuiID(RHI_TextureHandle handle) const = 0;

        // -------------------------------------------------------------------
        // GPU buffers — mirrors Slang's ConstantBuffer<T> / StructuredBuffer<T> / RWStructuredBuffer<T>.
        //
        // These are the buffers actually uploaded to the GPU (unlike RHI_RenderGraphBuilder::CreateBuffer,
        // which only *declares* transient render-graph resources for barrier tracking). The engine uses
        // them to create its own ParameterBlock<NovaEngine> buffers (see VK_PipelineCache::CreateEngineBuffers),
        // and any App code can call the same functions to send custom data to a shader as a ConstantBuffer,
        // StructuredBuffer or RWStructuredBuffer.
        // -------------------------------------------------------------------

        /** Create a buffer meant to back a Slang `ConstantBuffer<T>`. */
        virtual RHI_GpuBufferHandle CreateConstantBuffer(const RHI_GpuBufferDesc& desc) = 0;
        /** Create a buffer meant to back a Slang `StructuredBuffer<T>` (read-only array). */
        virtual RHI_GpuBufferHandle CreateStructuredBuffer(const RHI_GpuBufferDesc& desc) = 0;
        /** Create a buffer meant to back a Slang `RWStructuredBuffer<T>` (read-write array). */
        virtual RHI_GpuBufferHandle CreateRWStructuredBuffer(const RHI_GpuBufferDesc& desc) = 0;
        /** Destroy a buffer created by one of the `Create*Buffer` functions above. */
        virtual void DestroyGpuBuffer(RHI_GpuBufferHandle handle) = 0;

        /** Write `size` bytes of `data` into `elementIndex`, in the current frame-in-flight's region. */
        virtual void UpdateGpuBuffer(RHI_GpuBufferHandle handle, const void* data, size_t size, uint32_t elementIndex = 0) = 0;

        /**
         * Resolve a created buffer to a bindable (buffer handle/offset/range) for the current
         * frame-in-flight, to be fed into `RHI_ShaderResourceSet::SetBuffer`.
         */
        virtual RHI_BufferBinding ResolveGpuBufferBinding(RHI_GpuBufferHandle handle, uint32_t elementIndex = 0) const = 0;

    protected:
        std::unordered_map<const RHI_Mesh*, std::shared_ptr<RHI_Mesh>> m_MeshCache;

        void ClearMeshCache();
    };

    // -------------------------------------------------------------------------
    // Typed convenience helpers — explicit, Slang-vocabulary GPU buffer creation.
    //
    // These are the functions app/engine code should reach for: `RHI::CreateConstantBuffer<T>(...)`
    // reads exactly like declaring `ConstantBuffer<T>` in a shader. They forward to the untyped
    // `IRenderer::Create*Buffer` virtuals above, computed from `sizeof(T)`.
    // -------------------------------------------------------------------------

    template<typename T>
    RHI_GpuBufferHandle CreateConstantBuffer(IRenderer& renderer, uint32_t elementCount = 1, const char* debugName = nullptr) {
        return renderer.CreateConstantBuffer({ sizeof(T), elementCount, true, debugName });
    }

    template<typename T>
    RHI_GpuBufferHandle CreateStructuredBuffer(IRenderer& renderer, uint32_t elementCount, const char* debugName = nullptr) {
        return renderer.CreateStructuredBuffer({ sizeof(T), elementCount, true, debugName });
    }

    template<typename T>
    RHI_GpuBufferHandle CreateRWStructuredBuffer(IRenderer& renderer, uint32_t elementCount, const char* debugName = nullptr) {
        return renderer.CreateRWStructuredBuffer({ sizeof(T), elementCount, true, debugName });
    }

    /** Update a single element of a buffer created by one of the `Create*Buffer` helpers above. */
    template<typename T>
    void UpdateConstantBuffer(IRenderer& renderer, RHI_GpuBufferHandle handle, const T& value, uint32_t elementIndex = 0) {
        renderer.UpdateGpuBuffer(handle, &value, sizeof(T), elementIndex);
    }

    template<typename T>
    void UpdateStructuredBuffer(IRenderer& renderer, RHI_GpuBufferHandle handle, const T& value, uint32_t elementIndex = 0) {
        renderer.UpdateGpuBuffer(handle, &value, sizeof(T), elementIndex);
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H