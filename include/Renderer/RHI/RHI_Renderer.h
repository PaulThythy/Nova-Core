#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

#include <memory>
#include <vector>
#include <cstdint>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_Mesh.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/RHI/RHI_RenderGraph.h"

namespace Nova::Core::Renderer::RHI {

    // Cross-backend present mode preference (mapped per API in each backend).
    enum class RHI_PresentMode {
        Default,     // VSync / FIFO-style behaviour when available
        LowLatency,  // Prefer mailbox / low-latency modes (triple buffering when possible)
        Immediate    // Prefer immediate presentation (may tear)
    };

    /**
     * Swapchain and surface creation parameters shared across graphics backends.
     * Backends map these fields to API-specific swapchain/surface setup.
     */
    struct NV_API RHI_SwapchainDesc {
        // Number of CPU-side frames in flight (1 = single, 2 = double, 3 = triple buffering).
        uint32_t m_FramesInFlight = 3;

        // When true, create a platform surface from the application window (SDL on desktop).
        // When false, skip surface creation (e.g. headless or custom surface supplied later).
        bool m_CreateSurface = true;

        // When true and a valid surface exists, create a swapchain for presentation.
        bool m_EnableSwapchain = true;

        // Optional swapchain extent override. When either dimension is 0, use the window size.
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
        int32_t  m_VertexOffset = 0;   // base vertex

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

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        /**
         * Bind a render graph that defines the ordered render passes for this renderer.
         * The backend compiles pipelines and creates API-specific resources.
         */
        virtual void SetPipeline(std::unique_ptr<IRenderGraph> graph) = 0;
        virtual IRenderGraph* GetPipeline() const = 0;

        // Scene state is configured separately from raw draw commands.
        virtual void BeginScene(const glm::mat4& view, const glm::mat4& proj) = 0;
        virtual void SetModelMatrix(const glm::mat4& model) = 0;

        virtual void Draw(const RHI_DrawCommand& cmd) = 0;
        virtual void DrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;

        // Returns an API-specific ImGui texture identifier for the current viewport
        // render target, or nullptr if the renderer does not expose one.
        // Vulkan: typically a VkDescriptorSet cast to ImTextureID.
        virtual void* GetViewportTextureID() const = 0;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H