#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <utility>
#include <vector>

#include <vulkan/vulkan.h>
#include <unordered_map>

#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"

#include "Renderer/Backends/Vulkan/VK_Extensions.h"
#include "Renderer/Backends/Vulkan/VK_ValidationLayers.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Backends/Vulkan/VK_Instance.h"
#include "Renderer/Backends/Vulkan/VK_Device.h"
#include "Renderer/Backends/Vulkan/VK_Swapchain.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_Mesh.h"

#include "Api.h"
#include <memory>

namespace Nova::Core::Renderer::Backends::Vulkan {

	class NV_API VK_Renderer final : public RHI::IRenderer {
    public:
        VK_Renderer() = default;
        ~VK_Renderer() = default;

        bool Create(const RHI::RHI_SwapchainDesc& desc) override;
        void Destroy() override;

        bool Resize(int w, int h) override;
        void Update(float dt) override;

        void BeginFrame() override;
        void EndFrame() override;
        void PrepareForImGui() override;

        void BeginScene(const glm::mat4& view, const glm::mat4& proj) override;
        void SetModelMatrix(const glm::mat4& model) override;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        void* GetViewportTextureID() const override;

        RHI::RHI_Shaders* GetShader() override { return m_Shader.get(); }

        RHI::RHI_Shaders* CreateFullscreenShader(
            const RHI::RHI_ShaderCompileInput& vertIn,
            const RHI::RHI_ShaderCompileInput& fragIn) override;
        void DestroyFullscreenShader(RHI::RHI_Shaders* shader) override;
        void DrawFullscreen(RHI::RHI_Shaders* shader) override;

    private:
        static constexpr uint32_t MAX_MODEL_INSTANCES = 1024;
        static constexpr uint32_t MAX_MODEL_DRAWS = 4096;

        bool InitRenderResources();
        void DestroyRenderResources();
        bool RecreateSwapchainRenderTargets();

        bool CreateBackBufferRenderPass();
        void DestroyBackBufferRenderPass();
        bool CreateViewportRenderPass();
        void DestroyViewportRenderPass();
        bool CreateDepthResources();
        void DestroyDepthResources();
        bool CreateSwapchainFramebuffers();
        void DestroySwapchainFramebuffers();
        bool CreateImGuiDescriptorPool();
        void DestroyImGuiDescriptorPool();
        void CreateModelPipeline();
        void DestroyModelPipeline();

        void BeginImGuiRenderPass();
        bool TransitionViewportImageToShaderRead();
        void CreateViewportFramebuffer(int w, int h);
        void DestroyViewportFramebuffer();

        void CreateFullscreenQuadBuffer();
        void DestroyFullscreenQuadBuffer();

        std::shared_ptr<VK_Mesh> GetOrUploadMesh(const std::shared_ptr<Renderer::RHI::RHI_Mesh>& cpuMesh);

    private:
        VK_Instance  m_VKInstance;
        VK_Device    m_VKDevice;
        VK_Swapchain   m_VKSwapchain;
        RHI::RHI_SwapchainDesc m_SwapchainDesc{};
        bool m_RenderResourcesInitialized = false;

        VkRenderPass m_BackBufferRenderPass = VK_NULL_HANDLE;
        VkRenderPass m_ViewportRenderPass = VK_NULL_HANDLE;
        std::vector<VkFramebuffer> m_SwapchainFramebuffers;

        VkImage        m_DepthImage = VK_NULL_HANDLE;
        VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;
        VkImageView    m_DepthImageView = VK_NULL_HANDLE;
        VkFormat       m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        VkPipeline       m_ModelPipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_ModelPipelineLayout = VK_NULL_HANDLE;
        std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> m_SetLayouts;
        VkBuffer         m_BufGlobals = VK_NULL_HANDLE;
        VkDeviceMemory   m_BufGlobalsMemory = VK_NULL_HANDLE;
        VkBuffer         m_BufMvp = VK_NULL_HANDLE;
        VkDeviceMemory   m_BufMvpMemory = VK_NULL_HANDLE;
        VkDeviceSize     m_MvpDynamicStride = 0;
        VkBuffer         m_BufMaterials = VK_NULL_HANDLE;
        VkDeviceMemory   m_BufMaterialsMemory = VK_NULL_HANDLE;
        VkDeviceSize     m_MaterialDynamicStride = 0;
        VkBuffer         m_BufInstances = VK_NULL_HANDLE;
        VkDeviceMemory   m_BufInstancesMemory = VK_NULL_HANDLE;
        VkDeviceSize     m_BufInstancesSize = 0;
        std::vector<std::pair<uint32_t, VkDescriptorSet>> m_DescriptorSets;
        RHI::RHI_ProgramReflection m_ModelPipelineReflection{};
        VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;

        std::unique_ptr<VK_Shaders> m_Shader;
        std::vector<VkPipeline> m_FullscreenPipelines;
        struct FullscreenPipelineState {
            VkPipelineLayout layout = VK_NULL_HANDLE;
            std::vector<VkDescriptorSetLayout> setLayouts;
            std::vector<VkDescriptorSet> ownedSets;
        };
        std::unordered_map<VkPipeline, FullscreenPipelineState> m_FullscreenPipelineState;
        std::unordered_map<const Renderer::RHI::RHI_Mesh*, std::shared_ptr<VK_Mesh>> m_MeshCache;

        bool m_FramebufferResized = false;

        int m_ViewportWidth = 0;
        int m_ViewportHeight = 0;
        VkImage m_ViewportImage = VK_NULL_HANDLE;
        VkImageView m_ViewportImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_ViewportImageMemory = VK_NULL_HANDLE;
        VkImage m_ViewportDepthImage = VK_NULL_HANDLE;
        VkImageView m_ViewportDepthImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_ViewportDepthImageMemory = VK_NULL_HANDLE;
        VkFramebuffer m_ViewportFramebuffer = VK_NULL_HANDLE;
        VkSampler m_ViewportSampler = VK_NULL_HANDLE;
        VkDescriptorSet m_ViewportDescriptorSet = VK_NULL_HANDLE;
        bool m_RenderedToViewportThisFrame = false;
        bool m_ViewportImageFirstUse = true;
        bool m_FrameActive = false;
        bool m_ImGuiSwapchainPassBegun = false;

        VkBuffer       m_FullscreenQuadBuffer = VK_NULL_HANDLE;
        VkDeviceMemory m_FullscreenQuadMemory = VK_NULL_HANDLE;
	};
} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H