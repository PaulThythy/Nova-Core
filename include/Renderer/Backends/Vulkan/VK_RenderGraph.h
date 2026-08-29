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
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"
#include "Renderer/Backends/Vulkan/VK_Texture.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Renderer;

    struct NV_API VK_RenderPassAttachmentDesc {
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkAttachmentLoadOp loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        VkAttachmentStoreOp storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        VkImageLayout initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        VkImageLayout finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    };

    /** Vulkan pipeline cache with hot-reload support, indexed by RHI_ShaderHandle. */
    class NV_API VK_PipelineCache {
    public:
        static constexpr uint32_t MAX_MODEL_DRAWS = 4096;

        VK_PipelineCache() = default;
        ~VK_PipelineCache() { Destroy(); }

        bool Create(VK_Renderer& renderer, const std::vector<RHI::RHI_ShaderDesc>& shaders, VkFormat colorFormat, VkFormat depthFormat);
        void Destroy();

        /** Resolve a declared shader, building its pipeline on first use. */
        RHI::IShaders* Get(RHI::RHI_ShaderHandle handle);
        bool ReloadChangedShaders();

        void ResetFrameDynamicUBOs();

        VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        const RHI::RHI_EngineParameterBlock& GetEngine() const { return m_Engine; }
        bool BindEngineShadowMaps(VkImageView arrayView, VkSampler comparisonSampler);

        friend class VK_RenderGraph;

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
        bool CreateCompatibleRenderPasses(VkFormat colorFormat, VkFormat depthFormat);
        void DestroyEntry(PipelineEntry& entry);
        bool CreateEngineBuffers();
        void DestroyEngineBuffers();
        bool CreateDescriptorPool();
        void WriteEngineBuffersToEntry(PipelineEntry& entry);
        void WriteShadowMapsToEntry(PipelineEntry& entry);

        VK_Renderer* m_Renderer = nullptr;
        VkPipelineCache m_VkPipelineCache = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        /** Compatible RP for color+depth pipelines (Grid/Scene). */
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;
        /** Compatible RP for depth-only shadow pipelines. */
        VkRenderPass m_DepthOnlyRenderPass = VK_NULL_HANDLE;
        VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        RHI::RHI_EngineParameterBlock m_Engine{};
        VkImageView m_ShadowMapsView = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;

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

        RHI::IShaders* GetShader(RHI::RHI_ShaderHandle handle) override { return m_PipelineCache.Get(handle); }
        void* GetTextureImGuiID(RHI::RHI_TextureHandle handle) const override;
        bool Resize(uint32_t width, uint32_t height) override;
        const RHI::RHI_EngineParameterBlock* GetEngineParameterBlock() const override { return &m_PipelineCache.GetEngine(); }
        bool BindEngineShadowMaps(RHI::RHI_TextureHandle shadowMaps) override;
        bool GetSampledTextureNativeHandles(RHI::RHI_TextureHandle texture, uint64_t& outImageView, uint64_t& outSampler) const override;

        bool InitSwapchainResources();
        void DestroySwapchainResources();
        bool RecreateSwapchainRenderTargets();

        VkDescriptorPool GetImGuiDescriptorPool() const { return m_PipelineCache.GetDescriptorPool(); }
        const std::vector<VkFramebuffer>& GetSwapchainFramebuffers() const { return m_SwapchainFramebuffers; }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }

    private:
        struct PassRenderTarget {
            VkRenderPass renderPassClear = VK_NULL_HANDLE;
            VkRenderPass renderPassLoad = VK_NULL_HANDLE;
            std::vector<RHI::RHI_TextureHandle> colorAttachments;
            RHI::RHI_TextureHandle depthAttachment{};
            bool depthOnly = false;
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

        VkRenderPass CreateRenderPass(
            const VK_RenderPassAttachmentDesc* colors, uint32_t colorCount,
            const VK_RenderPassAttachmentDesc* depth) const;

        VkRenderPass CreateColorDepthRenderPass(
            VkFormat colorFormat,
            VkAttachmentLoadOp colorLoad,
            VkAttachmentLoadOp depthLoad,
            VkImageLayout finalColorLayout,
            VkImageLayout colorInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED) const;

        VkRenderPass CreateDepthOnlyRenderPass(VkAttachmentLoadOp depthLoad) const;

        bool EnsurePassRenderTarget(PassRenderTarget& rt, const RHI::RHI_RenderGraphPassDesc& pass);
        bool ExecutePass(size_t passIndex, bool presentPhase, bool leaveRenderPassOpen);
        void TransitionTextureForSampling(VkCommandBuffer cmd, VK_Texture& texture);
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

        std::vector<VK_Texture> m_Textures;
        std::vector<PassRenderTarget> m_PassRenderTargets;

        bool m_ResourcesInitialized = false;
        bool m_ShadersReloaded = false;
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