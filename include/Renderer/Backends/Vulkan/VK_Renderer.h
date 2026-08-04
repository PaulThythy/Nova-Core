#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <memory>

#include <vulkan/vulkan.h>

#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/RHI/RHI_RenderGraph.h"

#include "Renderer/Backends/Vulkan/VK_Instance.h"
#include "Renderer/Backends/Vulkan/VK_Device.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"
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
        void RenderFrame() override;
        void EndFrame() override;

        void SetRenderGraph(std::unique_ptr<RHI::IRenderGraph> graph) override;
        RHI::IRenderGraph* GetRenderGraph() const override { return m_RenderGraph.get(); }

        VK_RenderGraph* GetVKRenderGraph() const;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        void* GetTextureImGuiID(RHI::RHI_TextureHandle handle) const override;

        bool IsFrameActive() const { return m_FrameActive; }
        VkCommandBuffer GetCurrentCommandBuffer();
        
        std::shared_ptr<RHI::RHI_Mesh> GetOrUploadMesh(const std::shared_ptr<RHI::RHI_Mesh>& cpuMesh) override;

        VkInstance GetVkInstance() const { return m_VKInstance.GetInstance(); }
        VkDevice GetDevice() const { return m_VKDevice.GetDevice(); }
        VkPhysicalDevice GetPhysicalDevice() const { return m_VKDevice.GetPhysicalDevice(); }
        VkQueue GetGraphicsQueue() const { return m_VKDevice.GetGraphicsQueue(); }
        uint32_t GetGraphicsQueueFamily() const { return m_VKDevice.GetGraphicsQueueFamily(); }
        VK_MemoryAllocator& GetMemoryAllocator() { return m_MemoryAllocator; }
        const VK_MemoryAllocator& GetMemoryAllocator() const { return m_MemoryAllocator; }

        uint32_t GetSwapchainWidth() const { return m_VKSwapchain.GetExtent().width; }
        uint32_t GetSwapchainHeight() const { return m_VKSwapchain.GetExtent().height; }
        uint32_t GetSwapchainImageCount() const { return m_VKSwapchain.GetImageCount(); }
        uint32_t GetFramesInFlight() const { return m_VKSwapchain.GetFramesInFlight(); }
        uint32_t GetCurrentFrameInFlight() const { return m_VKSwapchain.GetCurrentFrame(); }
        VkFormat GetSwapchainImageFormat() const { return m_VKSwapchain.GetImageFormat(); }
        std::vector<VkImageView> GetSwapchainImageViews() const;
        VkFramebuffer GetSwapchainFramebuffer(uint32_t imageIndex) const;
        uint32_t GetAcquiredImageIndex() const { return m_VKSwapchain.GetAcquiredImageIndex(); }

    private:
        void WarnIfNoRenderGraph(const char* operation) const;

        VK_Instance          m_VKInstance;
        VK_Device            m_VKDevice;
        VK_MemoryAllocator   m_MemoryAllocator;
        VK_Swapchain         m_VKSwapchain;
        RHI::RHI_SwapchainDesc m_SwapchainDesc{};

        std::unique_ptr<RHI::IRenderGraph> m_RenderGraph;

        bool m_FramebufferResized = false;
        bool m_FrameActive = false;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H