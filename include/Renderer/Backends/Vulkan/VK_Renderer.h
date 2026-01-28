#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/Vulkan/VK_Extensions.h"
#include "Renderer/Backends/Vulkan/VK_ValidationLayers.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Backends/Vulkan/VK_Instance.h"
#include "Renderer/Backends/Vulkan/VK_Device.h"
#include "Renderer/Backends/Vulkan/VK_Swapchain.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	class VK_Renderer final : public RHI::IRenderer {
    public:
        VK_Renderer() = default;
        ~VK_Renderer() = default;

        bool Create();
        void Destroy();

        bool Resize(int w, int h);
        void Update(float dt);

        void BeginFrame();
        void Render();
        void EndFrame();

    private:
        // ---------------------------------------------------------------------
        // Swapchain lifecycle
        // ---------------------------------------------------------------------
        bool RecreateSwapchain();
        void CleanupSwapchain();

        bool CreateSwapchain();
        bool CreateImageViews();
        bool CreateRenderPass();
        bool CreateFramebuffers();

        // ---------------------------------------------------------------------
        // Commands & sync (single frame in flight)
        // ---------------------------------------------------------------------
        bool CreateCommandPoolAndBuffers();
        bool CreateSyncObjects();
        void DestroySyncObjects();

        // ------------------------------------------------------------
        // Commands (one pool + one primary cmd buffer per swapchain image)
        // ------------------------------------------------------------
        bool RecreateCommandBuffers();

        // ---------------------------------------------------------------------
        // Minimal graphics pipeline (triangle)
        // ---------------------------------------------------------------------
        void CreateTrianglePipeline();
        void DestroyTrianglePipeline();

        // ---------------------------------------------------------------------
        // ImGui (optional, but kept for your engine integration)
        // ---------------------------------------------------------------------
        bool CreateImGuiDescriptorPool();
        void DestroyImGuiDescriptorPool();

    private:
        // Core Vulkan objects (wrappers)
        VK_Instance m_VKInstance;
        VK_Device   m_VKDevice;

        // Swapchain
        VkSwapchainKHR            m_Swapchain = VK_NULL_HANDLE;
        VkFormat                  m_SwapchainImageFormat = VK_FORMAT_UNDEFINED;
        VkExtent2D                m_SwapchainExtent{ 0, 0 };
        std::vector<VkImage>      m_SwapchainImages;
        std::vector<VkImageView>  m_SwapchainImageViews;
        std::vector<VkFramebuffer> m_Framebuffers;

        // Render pass
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;

        // Pipeline (triangle)
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;
        VkPipeline       m_GraphicsPipeline = VK_NULL_HANDLE;

        // Commands (single primary command buffer)
        VkCommandPool   m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer>  m_CommandBuffers;

        // Synchronization (single frame in flight)
        VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        std::vector<VkSemaphore> m_RenderFinishedSemaphores;
        VkFence     m_InFlightFence = VK_NULL_HANDLE;

        // ImGui resources
        VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;

        // Per-frame state
        uint32_t m_CurrentImageIndex = 0;
        bool     m_FrameActive = false;
        bool     m_FramebufferResized = false;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H