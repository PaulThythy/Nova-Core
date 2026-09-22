#ifndef VK_RENDERGRAPH_H
#define VK_RENDERGRAPH_H

#include <cstdint>
#include <memory>
#include <vector>

#include <volk.h>

#include "Api.h"
#include "Renderer/RHI/RHI_RenderGraph.h"
#include "Renderer/Backends/Vulkan/VK_PipelineCache.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"
#include "Renderer/Backends/Vulkan/VK_Texture.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Renderer;

    class NV_API VK_RenderGraph final : public RHI::IRenderGraph {
    public:
        explicit VK_RenderGraph(RHI::RHI_CompiledRenderGraph compiled);
        ~VK_RenderGraph() override { Destroy(); }

        bool Create(VK_Renderer& renderer);
        void Destroy();

        /** Unregister ImGui texture IDs while the Vulkan ImGui backend is still alive. */
        void ReleaseImGuiTextures();

        void OnBeginFrame() override;
        void ExecuteScenePasses() override;
        void ExecutePresentPasses() override;
        void OnEndFrame() override;
        bool ReloadChangedShaders() override;

        RHI::IShaders* GetShader(RHI::RHI_ShaderHandle handle) override { return m_PipelineCache.Get(handle); }
        void* GetTextureImGuiID(RHI::RHI_TextureHandle handle) const override;
        bool Resize(uint32_t width, uint32_t height) override;
        const RHI::RHI_EngineParameterBlock* GetEngineParameterBlock() const override { return &m_PipelineCache.GetEngine(); }
        bool BindEngineShadowMaps(RHI::RHI_TextureHandle shadowMaps) override;
        bool GetSampledTextureNativeHandles(RHI::RHI_TextureHandle texture, uint64_t& outImageView, uint64_t& outSampler) const override;

        VkDescriptorPool GetImGuiDescriptorPool() const { return m_PipelineCache.GetDescriptorPool(); }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }

    private:
        struct PassRenderTarget {
            std::vector<RHI::RHI_TextureHandle> colorAttachments;
            RHI::RHI_TextureHandle depthAttachment{};
            bool depthOnly = false;
            bool loadColor = false;
            bool loadDepth = false;
        };

        class PassContext final : public RHI::IPassContext {
        public:
            PassContext(VK_RenderGraph& graph, uint32_t width, uint32_t height, PassRenderTarget* rt)
                : m_Graph(graph), m_Width(width), m_Height(height), m_Rt(rt) {}

            RHI::IShaders* GetShader(RHI::RHI_ShaderHandle shader) override { return m_Graph.m_PipelineCache.Get(shader); }
            void DrawFullscreen(RHI::RHI_ShaderHandle shader) override;
            void Draw(const RHI::RHI_DrawCommand& cmd) override;
            void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;
            void BindShader(RHI::RHI_ShaderHandle shader) override;
            uint32_t GetRenderWidth() const override { return m_Width; }
            uint32_t GetRenderHeight() const override { return m_Height; }
            void SetDepthBias(float constantFactor, float slopeFactor, float clamp = 0.0f) override;

            void BeginDepthLayer(RHI::RHI_TextureHandle depth, uint32_t layer, bool clear) override;
            void EndDepthLayer() override;

        private:
            VK_RenderGraph& m_Graph;
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
            PassRenderTarget* m_Rt = nullptr;
        };

        bool CreateTransientResources();
        void DestroyTransientResources();
        void DestroyPassRenderTargets();

        VkFormat ToVkFormat(RHI::RHI_TextureFormat format) const;
        bool IsDepthFormat(RHI::RHI_TextureFormat format) const;

        bool EnsurePassRenderTarget(PassRenderTarget& rt, const RHI::RHI_RenderGraphPassDesc& pass);
        bool ExecutePass(size_t passIndex, bool presentPhase, bool leaveRenderingOpen);

        void TransitionTextureToAttachment(VkCommandBuffer cmd, VK_Texture& texture, bool isDepth, bool discardContents);
        void TransitionTextureForSampling(VkCommandBuffer cmd, VK_Texture& texture);
        void TransitionSwapchainToColorAttachment(VkCommandBuffer cmd, uint32_t imageIndex, bool discardContents);
        void TransitionSwapchainToPresent(VkCommandBuffer cmd, uint32_t imageIndex);
        void TransitionSwapchainDepthToAttachment(VkCommandBuffer cmd, uint32_t imageIndex, bool discardContents);

        void BeginRendering(
            VkCommandBuffer cmd,
            uint32_t width,
            uint32_t height,
            const VkRenderingAttachmentInfo* color,
            uint32_t colorCount,
            const VkRenderingAttachmentInfo* depth);
        void EndRendering(VkCommandBuffer cmd);

        void SetViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height);
        void DrawFullscreenQuad(VkCommandBuffer cmd);

        bool InitPresentationResources();
        void DestroyPresentationResources();
        void CreateFullscreenQuadBuffer();
        void DestroyFullscreenQuadBuffer();

        VkCommandBuffer GetCurrentCommandBuffer() const;

        VK_Renderer* m_Renderer = nullptr;
        VK_PipelineCache m_PipelineCache;

        std::vector<VK_Texture> m_Textures;
        std::vector<PassRenderTarget> m_PassRenderTargets;

        bool m_ResourcesInitialized = false;
        bool m_ShadersReloaded = false;
        bool m_InsideRendering = false;
        bool m_SwapchainColorWritten = false;
        bool m_SwapchainInColorAttachment = false;
        bool m_SwapchainDepthInAttachment = false;

        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;
        /** Stable storage for ImGui PipelineRenderingCreateInfo color format pointer. */
        VkFormat m_ImGuiColorFormat = VK_FORMAT_UNDEFINED;

        uint32_t m_SceneWidth = 0;
        uint32_t m_SceneHeight = 0;

        VK_BufferAllocation m_FullscreenQuadBuffer{};
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERGRAPH_H