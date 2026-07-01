#ifndef VK_RENDERGRAPH_H
#define VK_RENDERGRAPH_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <vector>

#include <vulkan/vulkan.h>

#include "Api.h"
#include "Renderer/RHI/RHI_RenderGraph.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Renderer;

    class NV_API VK_RenderGraph final : public RHI::IRenderGraph {
    public:
        static constexpr uint32_t MAX_MODEL_INSTANCES = 1024;
        static constexpr uint32_t MAX_MODEL_DRAWS = 4096;

        explicit VK_RenderGraph(std::vector<RHI::RHI_RenderPassDesc> passes);
        ~VK_RenderGraph() override { Destroy(); }

        bool Create(VK_Renderer& renderer);
        void Destroy();

        void OnBeginFrame() override;
        void OnEndFrame() override;
        void OnBeginScene(const glm::mat4& view, const glm::mat4& proj) override;
        void OnSetModelMatrix(const glm::mat4& model) override;
        void OnDraw(const RHI::RHI_DrawCommand& cmd) override;
        void OnDrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;
        void OnTransitionToImGuiPass() override;
        bool ReloadChangedShaders() override;

        bool IsGeometryPassActive() const { return m_GeometryPassActive; }
        bool IsInsideRenderPass() const { return m_InsideRenderPass; }
        void EndGeometryPass(VkCommandBuffer cmd);
        void BeginImGuiPass(VkCommandBuffer cmd, uint32_t swapchainImageIndex);
        void EndActivePass(VkCommandBuffer cmd);

        int GetGeometryPassIndex() const { return m_GeometryPassIndex; }
        int GetImGuiPassIndex() const { return m_ImGuiPassIndex; }

        bool InitSwapchainResources();
        void DestroySwapchainResources();
        bool RecreateSwapchainRenderTargets();
        void ResetFrameDynamicUBOs();

        VkRenderPass GetBackBufferRenderPass() const { return m_BackBufferRenderPass; }
        VkRenderPass GetViewportRenderPass() const { return m_ViewportColorDepthClearPass; }
        VkDescriptorPool GetImGuiDescriptorPool() const { return m_ImGuiDescriptorPool; }
        const std::vector<VkFramebuffer>& GetSwapchainFramebuffers() const { return m_SwapchainFramebuffers; }
        VkFormat GetDepthFormat() const { return m_DepthFormat; }

    private:
        struct PassPipeline {
            RHI::RHI_RenderPassType type = RHI::RHI_RenderPassType::Fullscreen;
            VkRenderPass renderPassViewport = VK_NULL_HANDLE;
            VkRenderPass renderPassBackBuffer = VK_NULL_HANDLE;
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> setLayouts;
            std::vector<std::pair<uint32_t, VkDescriptorSet>> descriptorSets;
            std::unique_ptr<VK_Shaders> shader;
            RHI::RHI_GraphicsShaderDesc shaderDesc{};
            std::filesystem::file_time_type vertWriteTime{};
            std::filesystem::file_time_type fragWriteTime{};
            bool hasShaderDesc = false;
            bool alphaBlend = false;
            bool depthTest = true;
            bool depthWrite = true;
            RHI::RHI_LoadOp colorLoadOp = RHI::RHI_LoadOp::Clear;
            bool clearDepth = true;
            RHI::RHI_RenderTarget target = RHI::RHI_RenderTarget::Viewport;
        };

        bool CreateImGuiDescriptorPool();
        void DestroyImGuiDescriptorPool();
        bool CreateDepthResources();
        void DestroyDepthResources();
        bool CreateBackBufferRenderPass();
        void DestroyBackBufferRenderPass();
        bool CreateSwapchainFramebuffers();
        void DestroySwapchainFramebuffers();

        VkRenderPass CreateColorDepthRenderPass(
            VkAttachmentLoadOp colorLoad,
            VkAttachmentLoadOp depthLoad,
            VkImageLayout finalColorLayout,
            VkImageLayout colorInitialLayout = VK_IMAGE_LAYOUT_UNDEFINED) const;

        bool CreatePassPipeline(size_t passIndex, const RHI::RHI_RenderPassDesc& desc);
        bool RebuildPassPipeline(size_t passIndex);
        bool RebuildPassPipeline(size_t passIndex, const RHI::RHI_RenderPassDesc& desc);
        void ApplyPassDesc(PassPipeline& pass, const RHI::RHI_RenderPassDesc& desc);
        void AssignPassRenderPasses();
        void DestroyViewportRenderPasses();
        void TransitionViewportImageForSampling(VkCommandBuffer cmd);
        void DestroyPassPipeline(PassPipeline& pass);

        void CreateFullscreenQuadBuffer();
        void DestroyFullscreenQuadBuffer();
        void DrawFullscreenQuad(VkCommandBuffer cmd, VK_Shaders& shader);

        VkRenderPass SelectRenderPass(const PassPipeline& pass, bool useViewport) const;
        bool BeginPassRenderPass(VkCommandBuffer cmd, const PassPipeline& pass, bool useViewport,
            VkFramebuffer framebuffer, uint32_t width, uint32_t height,
            VkRenderPass renderPassOverride = VK_NULL_HANDLE);
        void SetViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height);

        void BeginRenderPasses(VkCommandBuffer cmd);
        void EnsureRenderPassesBegun();
        RHI::RHI_Shaders* GetGeometryPassShader() const;
        VkCommandBuffer GetCurrentCommandBuffer();

        VK_Renderer* m_Renderer = nullptr;

        bool m_ResourcesInitialized = false;
        int m_GeometryPassIndex = -1;
        int m_ImGuiPassIndex = -1;
        int m_ActivePassIndex = -1;
        bool m_GeometryPassActive = false;
        bool m_InsideRenderPass = false;
        bool m_RenderPassesBegun = false;
        bool m_ImGuiPassBegun = false;
        // True once any pass has rendered into the current swapchain image this frame.
        // The ImGui pass uses this to decide whether to LOAD (preserve) or CLEAR the backbuffer,
        // so its render pass initial layout always matches the image's real layout.
        bool m_SwapchainColorWritten = false;

        VkRenderPass m_BackBufferRenderPass = VK_NULL_HANDLE;
        VkRenderPass m_BackBufferLoadRenderPass = VK_NULL_HANDLE;
        VkRenderPass m_ViewportColorDepthClearPass = VK_NULL_HANDLE;
        VkRenderPass m_ViewportColorLoadDepthClearPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;

        VkImage        m_DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;
        VkImageView    m_DepthImageView = VK_NULL_HANDLE;
        VkFormat       m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;

        VkBuffer       m_BufGlobals = VK_NULL_HANDLE;
        VkDeviceMemory m_BufGlobalsMemory = VK_NULL_HANDLE;
        VkBuffer       m_BufMvp = VK_NULL_HANDLE;
        VkDeviceMemory m_BufMvpMemory = VK_NULL_HANDLE;
        VkDeviceSize   m_MvpDynamicStride = 0;
        VkDeviceSize   m_MvpDynamicOffset = 0;
        VkBuffer       m_BufMaterials = VK_NULL_HANDLE;
        VkDeviceMemory m_BufMaterialsMemory = VK_NULL_HANDLE;
        VkDeviceSize   m_MaterialDynamicStride = 0;
        VkDeviceSize   m_MaterialDynamicOffset = 0;
        VkBuffer       m_BufInstances = VK_NULL_HANDLE;
        VkDeviceMemory m_BufInstancesMemory = VK_NULL_HANDLE;
        VkDeviceSize   m_BufInstancesSize = 0;

        std::vector<PassPipeline> m_PassPipelines;

        VkBuffer       m_FullscreenQuadBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_FullscreenQuadMemory = VK_NULL_HANDLE;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERGRAPH_H