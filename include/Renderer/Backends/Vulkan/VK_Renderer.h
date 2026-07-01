#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <memory>
#include <unordered_map>

#include <vulkan/vulkan.h>

#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/RHI/RHI_RenderGraph.h"

#include "Renderer/Backends/Vulkan/VK_Instance.h"
#include "Renderer/Backends/Vulkan/VK_Device.h"
#include "Renderer/Backends/Vulkan/VK_Swapchain.h"
#include "Renderer/Backends/Vulkan/VK_Mesh.h"
#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"

#include "Api.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class NV_API VK_Renderer final : public RHI::IRenderer {
    public:
        VK_Renderer() = default;
        ~VK_Renderer() override = default;

        bool Create(const RHI::RHI_SwapchainDesc& desc) override;
        void Destroy() override;

        bool Resize(int w, int h) override;
        void Update(float dt) override;

        void BeginFrame() override;
        void EndFrame() override;

        void SetPipeline(std::unique_ptr<RHI::IRenderGraph> graph) override;
        RHI::IRenderGraph* GetPipeline() const override { return m_RenderGraph.get(); }

        VK_RenderGraph* GetVKRenderGraph() const;

        void BeginScene(const glm::mat4& view, const glm::mat4& proj) override;
        void SetModelMatrix(const glm::mat4& model) override;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        void* GetViewportTextureID() const override;

        // Used by VK_RenderGraph during frame execution.
        bool IsFrameActive() const { return m_FrameActive; }
        VkCommandBuffer GetCurrentCommandBuffer();
        std::shared_ptr<VK_Mesh> GetOrUploadMesh(const std::shared_ptr<Renderer::RHI::RHI_Mesh>& cpuMesh);

        // Accessors used by VK_RenderGraph
        VkInstance GetVkInstance() const { return m_VKInstance.GetInstance(); }
        VkDevice GetDevice() const { return m_VKDevice.GetDevice(); }
        VkPhysicalDevice GetPhysicalDevice() const { return m_VKDevice.GetPhysicalDevice(); }
        VkQueue GetGraphicsQueue() const { return m_VKDevice.GetGraphicsQueue(); }
        uint32_t GetGraphicsQueueFamily() const { return m_VKDevice.GetGraphicsQueueFamily(); }
        const VkPhysicalDeviceMemoryProperties& GetMemoryProperties() const { return m_VKDevice.GetMemoryProperties(); }

        uint32_t GetSwapchainWidth() const { return m_VKSwapchain.GetExtent().width; }
        uint32_t GetSwapchainHeight() const { return m_VKSwapchain.GetExtent().height; }
        uint32_t GetSwapchainImageCount() const { return m_VKSwapchain.GetImageCount(); }
        VkFormat GetSwapchainImageFormat() const { return m_VKSwapchain.GetImageFormat(); }
        const std::vector<VkImageView>& GetSwapchainImageViews() const;
        VkFramebuffer GetSwapchainFramebuffer(uint32_t imageIndex) const;
        uint32_t GetAcquiredImageIndex() const { return m_VKSwapchain.GetAcquiredImageIndex(); }

        bool HasViewportFramebuffer() const { return m_ViewportFramebuffer != VK_NULL_HANDLE; }
        VkImage GetViewportImage() const { return m_ViewportImage; }
        uint32_t GetViewportWidth() const { return static_cast<uint32_t>(m_ViewportWidth); }
        uint32_t GetViewportHeight() const { return static_cast<uint32_t>(m_ViewportHeight); }
        VkFramebuffer GetViewportFramebuffer() const { return m_ViewportFramebuffer; }

    private:
        void WarnIfNoPipeline(const char* operation) const;

        void CreateViewportFramebuffer(int w, int h);
        void DestroyViewportFramebuffer();

    private:
        VK_Instance    m_VKInstance;
        VK_Device      m_VKDevice;
        VK_Swapchain   m_VKSwapchain;
        RHI::RHI_SwapchainDesc m_SwapchainDesc{};

        std::unique_ptr<RHI::IRenderGraph> m_RenderGraph;

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
        bool m_FrameActive = false;
	};
} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H