#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

#include <memory>
#include <cstdint>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_Mesh.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/RHI/RHI_RenderGraph.h"

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

        virtual void BeginFrame() = 0;
        virtual void EndFrame() = 0;

        virtual void SetRenderGraph(std::unique_ptr<IRenderGraph> graph) = 0;
        virtual IRenderGraph* GetRenderGraph() const = 0;

        /** Execute scene passes (non-swapchain targets). Call during OnRender. */
        virtual void ExecuteScenePasses() = 0;

        virtual void Draw(const RHI_DrawCommand& cmd) = 0;
        virtual void DrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;

        /** Returns an ImGui texture identifier for a sampled render graph texture. */
        virtual void* GetTextureImGuiID(RHI_TextureHandle handle) const = 0;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H