#include "Renderer/RHI/RHI_RenderGraph.h"

#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"
#include "Core/Log.h"

namespace Nova::Core::Renderer::RHI {

    IRenderGraph::IRenderGraph(RHI_CompiledRenderGraph compiled)
        : m_Data(std::move(compiled.m_Data))
        , m_Passes(m_Data.m_Passes)
        , m_ExecutionOrder(std::move(compiled.m_ExecutionOrder))
        , m_GraphValid(compiled.m_Valid)
    {
    }

    bool IRenderGraph::PassWritesSwapchain(const RHI_RenderGraphPassDesc& pass) const {
        for (RHI_TextureHandle handle : pass.m_WriteTextures) {
            if (!handle.IsValid() || handle.m_Index >= m_Data.m_Textures.size())
                continue;
            if (m_Data.m_Textures[handle.m_Index].m_IsSwapchain)
                return true;
        }
        return false;
    }

    std::unique_ptr<IRenderGraph> IRenderGraph::Create(Core::GraphicsAPI api, RHI_CompiledRenderGraph compiled) {
        switch (api) {
            case Core::GraphicsAPI::Vulkan:
                return std::make_unique<Backends::Vulkan::VK_RenderGraph>(std::move(compiled));

            default:
                NV_LOG_ERROR("IRenderGraph::Create - unsupported graphics API");
                return nullptr;
        }
    }

} // namespace Nova::Core::Renderer::RHI