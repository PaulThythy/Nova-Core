#ifndef RHI_RENDERGRAPH_H
#define RHI_RENDERGRAPH_H

#include <cstdint>
#include <memory>
#include <vector>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_RenderGraphTypes.h"
#include "Renderer/RHI/RHI_RenderGraphBuilder.h"
#include "Renderer/RHI/RHI_RenderGraphCompiler.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

namespace Nova::Core::Renderer::RHI {

    /**
     * Abstract render graph: holds a compiled declaration and runs backend execution.
     * Declaration/compilation live in RHI_RenderGraphBuilder / RHI_RenderGraphCompiler.
     */
    class NV_API IRenderGraph {
    public:
        virtual ~IRenderGraph() = default;

        static std::unique_ptr<IRenderGraph> Create(Core::GraphicsAPI api, RHI_CompiledRenderGraph compiled);

        virtual void OnBeginFrame() = 0;

        /** Execute passes that do not target the swapchain (scene rendering). */
        virtual void ExecuteScenePasses() = 0;

        /** Execute passes that write to the imported swapchain texture (presentation). */
        virtual void ExecutePresentPasses() = 0;

        virtual void OnEndFrame() = 0;
        virtual bool ReloadChangedShaders() = 0;

        /** Resolve a declared shader to its backend pipeline, building it on first use. */
        virtual IShaders* GetShader(RHI_ShaderHandle shader) = 0;

        virtual void* GetTextureImGuiID(RHI_TextureHandle handle) const = 0;
        virtual bool Resize(uint32_t width, uint32_t height) = 0;

        /** Engine `ParameterBlock<NovaEngine>` buffers (scene/model/material/lights). */
        virtual const RHI_EngineParameterBlock* GetEngineParameterBlock() const { return nullptr; }

        /**
         * Bind a depth Texture2DArray (+ comparison sampler) to `nova.shadowMaps` /
         * `nova.shadowSampler` on every built pipeline that reflects those names.
         */
        virtual bool BindEngineShadowMaps(RHI_TextureHandle shadowMaps) { (void)shadowMaps; return false; }

        /**
         * Resolve a graph texture to backend-native sampled image + sampler handles
         * for `IShaders::BindSampledTexture` (VkImageView / VkSampler as uint64_t).
         */
        virtual bool GetSampledTextureNativeHandles(
            RHI_TextureHandle texture,
            uint64_t& outImageView,
            uint64_t& outSampler) const
        {
            (void)texture; outImageView = 0; outSampler = 0;
            return false;
        }

        const std::vector<RHI_RenderGraphPassDesc>& GetPasses() const { return m_Passes; }
        const std::vector<size_t>& GetExecutionOrder() const { return m_ExecutionOrder; }
        /** True once the backend finished Create() successfully. */
        bool IsCompiled() const { return m_Compiled; }
        /** True if DAG compilation succeeded (no cycle / validation errors). */
        bool IsGraphValid() const { return m_GraphValid; }

    protected:
        explicit IRenderGraph(RHI_CompiledRenderGraph compiled);

        bool PassWritesSwapchain(const RHI_RenderGraphPassDesc& pass) const;

        RHI_RenderGraphData m_Data;
        std::vector<RHI_RenderGraphPassDesc> m_Passes;
        std::vector<size_t> m_ExecutionOrder;
        bool m_GraphValid = false;
        bool m_Compiled = false;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERGRAPH_H