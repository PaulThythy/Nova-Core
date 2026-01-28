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

        bool Resize(int w, int h) override;
        void Update(float dt) override;

        void BeginFrame() override;
        void Render() override;
        void EndFrame() override;

        static constexpr uint32_t MAX_FRAMES_IN_FLIGHT = 3;

        struct VK_FrameSync {
            VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
            VkSemaphore m_RenderFinishedSemaphore = VK_NULL_HANDLE;
            VkFence     m_InFlightFence = VK_NULL_HANDLE;
        };

        struct VK_Frame {
            VkImage       m_VKImage = VK_NULL_HANDLE;
            VkImageView   m_VKImageView = VK_NULL_HANDLE;
            VkFramebuffer m_VKFramebuffer = VK_NULL_HANDLE;
        };

        struct SwapchainSupportDetails {
            VkSurfaceCapabilitiesKHR        m_Capabilities{};
            std::vector<VkSurfaceFormatKHR> m_Formats;
            std::vector<VkPresentModeKHR>   m_PresentModes;
        };

    private:
        // Swapchain lifecycle
        bool RecreateSwapchain();
        void CleanupSwapchain();

        bool CreateSwapchain();
        bool CreateImageViews();
        bool CreateRenderPass();
        bool CreateFramebuffers();

        // Commands & sync
        bool CreateCommandPoolAndBuffers();
        bool RecreateCommandBuffers();

        bool CreateSyncObjects();
        void DestroySyncObjects();

        // Minimal pipeline
        void CreateTrianglePipeline();
        void DestroyTrianglePipeline();

        // ImGui
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

        std::vector<VK_Frame> m_Frames;

        // Render pass
        VkRenderPass m_RenderPass = VK_NULL_HANDLE;

        // Pipeline (triangle)
        VkPipeline       m_TrianglePipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_TrianglePipelineLayout = VK_NULL_HANDLE;

        // Commands (single primary command buffer)
        VkCommandPool   m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer>  m_CommandBuffers;

        // Frames in flight : 3
        std::array<VK_FrameSync, MAX_FRAMES_IN_FLIGHT> m_FrameSync{};
        uint32_t m_CurrentFrame = 0;

        std::vector<VkFence> m_ImagesInFlight;

        // ImGui resources
        VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;

        // Per-frame state
        uint32_t m_CurrentImageIndex = 0;
        bool     m_FramebufferResized = false;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H