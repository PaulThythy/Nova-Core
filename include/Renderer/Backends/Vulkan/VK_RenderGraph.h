#ifndef VK_RENDERGRAPH_H
#define VK_RENDERGRAPH_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

#include <vulkan/vulkan.h>

#include "Api.h"
#include "Renderer/RHI/RHI_RenderGraph.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Renderer;

    /** Vulkan pipeline cache with hot-reload support, indexed by RHI_ShaderHandle. */
    class NV_API VK_PipelineCache {
    public:
        static constexpr uint32_t MAX_MODEL_DRAWS = 4096;

        VK_PipelineCache() = default;
        ~VK_PipelineCache() { Destroy(); }

        bool Create(VK_Renderer& renderer, const std::vector<RHI::RHI_ShaderDesc>& shaders, VkFormat colorFormat, VkFormat depthFormat);
        void Destroy();

        /** Resolve a declared shader, building its pipeline on first use. */
        RHI::RHI_Shaders* Get(RHI::RHI_ShaderHandle handle);
        bool ReloadChangedShaders();

        void ResetFrameDynamicUBOs();

        VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }

    private:
        struct PipelineEntry {
            RHI::RHI_ShaderDesc desc{};
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> setLayouts;
            std::vector<std::pair<uint32_t, VkDescriptorSet>> descriptorSets;
            std::unique_ptr<VK_Shaders> shader;
            std::filesystem::file_time_type vertWriteTime{};
            std::filesystem::file_time_type fragWriteTime{};
        };

        bool BuildPipeline(PipelineEntry& entry);
        bool CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat);
        void DestroyEntry(PipelineEntry& entry);
        bool CreateEngineBuffers();
        void DestroyEngineBuffers();
        bool CreateDescriptorPool();

        VK_Renderer* m_Renderer = nullptr;
        VkPipelineCache m_VkPipelineCache = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        VK_BufferAllocation m_BufGlobals{};
        VkDeviceSize m_FrameUniformStride = 0;
        VkDeviceSize m_FrameUniformOffset = 0;
        VK_BufferAllocation m_BufMvp{};
        VkDeviceSize m_MvpDynamicStride = 0;
        VkDeviceSize m_MvpFrameRegionStride = 0;
        VkDeviceSize m_MvpDynamicOffset = 0;
        VK_BufferAllocation m_BufMaterials{};
        VkDeviceSize m_MaterialDynamicStride = 0;
        VkDeviceSize m_MaterialFrameRegionStride = 0;
        VkDeviceSize m_MaterialDynamicOffset = 0;

        uint32_t m_FramesInFlight = 1;
        std::vector<PipelineEntry> m_Entries;
    };

    class NV_API VK_RenderGraph final : public RHI::IRenderGraph {
    public:
        explicit VK_RenderGraph(RHI::RHI_RenderGraphData data);
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

        RHI::RHI_Shaders* GetShader(RHI::RHI_ShaderHandle handle) override { return m_PipelineCache.Get(handle); }
        void* GetTextureImGuiID(RHI::RHI_TextureHandle handle) const override;
        bool Resize(uint32_t width, uint32_t height) override;

        bool InitSwapchainResources();
        void DestroySwapchainResources();
        bool RecreateSwapchainRenderTargets();

        VkDescriptorPool GetImGuiDescriptorPool() const { return m_PipelineCache.GetDescriptorPool(); }
        const std::vector<VkFramebuffer>& GetSwapchainFramebuffers() const { return m_SwapchainFramebuffers; }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }

    private:
        struct TextureResource {
            RHI::RHI_RenderGraphTextureResource desc;
            VK_ImageAllocation image{};
            /** Identity-swizzle view — required for framebuffer attachments. */
            VkImageView view = VK_NULL_HANDLE;
            /** Optional sampling view (e.g. depth R→RGB for ImGui). Equals view when unused. */
            VkImageView sampledView = VK_NULL_HANDLE;
            VkSampler sampler = VK_NULL_HANDLE;
            void* imguiTextureId = nullptr;
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            RHI::RHI_ResourceState state = RHI::RHI_ResourceState::Undefined;
        };

        struct PassRenderTarget {
            VkRenderPass renderPassClear = VK_NULL_HANDLE;
            VkRenderPass renderPassLoad = VK_NULL_HANDLE;
            std::vector<RHI::RHI_TextureHandle> colorAttachments;
            RHI::RHI_TextureHandle depthAttachment{};
        };

        class PassContext final : public RHI::RHI_PassContext {
        public:
            PassContext(VK_RenderGraph& graph, uint32_t width, uint32_t height)
                : m_Graph(graph), m_Width(width), m_Height(height) {}

            RHI::RHI_Shaders* GetShader(RHI::RHI_ShaderHandle shader) override { return m_Graph.m_PipelineCache.Get(shader); }
            void DrawFullscreen(RHI::RHI_ShaderHandle shader) override;
            void Draw(const RHI::RHI_DrawCommand& cmd) override;
            void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;
            void BindShader(RHI::RHI_ShaderHandle shader) override;
            uint32_t GetRenderWidth() const override { return m_Width; }
            uint32_t GetRenderHeight() const override { return m_Height; }

        private:
            VK_RenderGraph& m_Graph;
            uint32_t m_Width = 0;
            uint32_t m_Height = 0;
        };

        bool CreateTransientResources();
        void DestroyTransientResources();
        bool CreateTexture(TextureResource& texture, uint32_t width, uint32_t height);
        void DestroyTexture(TextureResource& texture);
        void DestroyPassRenderTargets();

        VkFormat ToVkFormat(RHI::RHI_TextureFormat format) const;
        bool IsDepthFormat(RHI::RHI_TextureFormat format) const;

        VkRenderPass CreateColorDepthRenderPass(
            VkFormat colorFormat,
            VkAttachmentLoadOp colorLoad,
            VkAttachmentLoadOp depthLoad,
            VkImageLayout finalColorLayout,
            VkImageLayout colorInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED) const;

        bool EnsurePassRenderTarget(PassRenderTarget& rt, const RHI::RHI_RenderGraphPassDesc& pass);
        bool ExecutePass(size_t passIndex, bool presentPhase, bool leaveRenderPassOpen);
        void TransitionTextureForSampling(VkCommandBuffer cmd, TextureResource& texture);
        void SetViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height);
        void DrawFullscreenQuad(VkCommandBuffer cmd);

        bool CreateDepthResources();
        void DestroyDepthResources();
        bool CreateBackBufferRenderPass();
        void DestroyBackBufferRenderPass();
        bool CreateSwapchainFramebuffers();
        void DestroySwapchainFramebuffers();
        void CreateFullscreenQuadBuffer();
        void DestroyFullscreenQuadBuffer();

        VkCommandBuffer GetCurrentCommandBuffer() const;

        VK_Renderer* m_Renderer = nullptr;
        VK_PipelineCache m_PipelineCache;

        std::vector<TextureResource> m_Textures;
        std::vector<PassRenderTarget> m_PassRenderTargets;

        bool m_ResourcesInitialized = false;
        bool m_InsideRenderPass = false;
        bool m_SwapchainColorWritten = false;

        VkRenderPass m_BackBufferRenderPass = VK_NULL_HANDLE;
        VkRenderPass m_BackBufferLoadRenderPass = VK_NULL_HANDLE;
        // Intermediate swapchain rendering (Grid/Scene) — keeps layout in COLOR_ATTACHMENT_OPTIMAL.
        VkRenderPass m_SwapchainSceneClearPass = VK_NULL_HANDLE;
        VkRenderPass m_SwapchainSceneLoadPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;

        struct SwapchainDepthImage {
            VK_ImageAllocation m_Image{};
            VkImageView m_View = VK_NULL_HANDLE;
        };
        std::vector<SwapchainDepthImage> m_SwapchainDepthImages;
        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        uint32_t m_SceneWidth = 0;
        uint32_t m_SceneHeight = 0;

        VK_BufferAllocation m_FullscreenQuadBuffer{};
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERGRAPH_H