#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/Vulkan/VK_Extensions.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	class VK_Renderer final : public RHI::IRenderer {
	public:
		VK_Renderer() = default;
		~VK_Renderer() override = default;

		bool Create();
		void Destroy();

		bool Resize(int w, int h) override;

		void Update(float dt) override;

		void BeginFrame() override;
		void Render() override;
		void EndFrame() override;

	private:
        // --- Helpers ---
        bool CreateInstance();
        bool CreateSurface();
        bool PickPhysicalDevice();
        bool CreateDevice();
        bool CreateSwapchain();
        bool CreateImageViews();
        bool CreateRenderPass();
        bool CreateFramebuffers();
        bool CreateCommandPool();
        bool AllocateCommandBuffers();
        bool CreateSyncObjects();
        bool CreateImGuiDescriptorPool();
        void CleanupSwapchain();
        bool RecreateSwapchain();

        // Validation layers
        bool SetupDebugMessenger();

        bool m_EnableValidationLayers =
#ifndef NOVA_DEBUG
            true;
#else
            false;
#endif
        VkDebugUtilsMessengerEXT m_DebugMessenger = VK_NULL_HANDLE;


        // Vulkan objects
        VkInstance        m_Instance = VK_NULL_HANDLE;
        VkSurfaceKHR      m_Surface = VK_NULL_HANDLE;
        VkPhysicalDevice  m_PhysicalDevice = VK_NULL_HANDLE;
        VkDevice          m_Device = VK_NULL_HANDLE;

        uint32_t          m_GraphicsQueueFamily = 0;
        uint32_t          m_PresentQueueFamily = 0;
        VkQueue           m_GraphicsQueue = VK_NULL_HANDLE;
        VkQueue           m_PresentQueue = VK_NULL_HANDLE;

        VkSwapchainKHR                m_Swapchain = VK_NULL_HANDLE;
        VkFormat                      m_SwapchainImageFormat{};
        VkExtent2D                    m_SwapchainExtent{};
        uint32_t                      m_MinImageCount = 0;
        std::vector<VkImage>          m_SwapchainImages;
        std::vector<VkImageView>      m_SwapchainImageViews;
        std::vector<VkFramebuffer>    m_SwapchainFramebuffers;

        VkRenderPass       m_RenderPass = VK_NULL_HANDLE;

        VkCommandPool                   m_CommandPool = VK_NULL_HANDLE;
        std::vector<VkCommandBuffer>    m_CommandBuffers;

        VkSemaphore       m_ImageAvailableSemaphore = VK_NULL_HANDLE;
        VkSemaphore       m_RenderFinishedSemaphore = VK_NULL_HANDLE;
        VkFence           m_InFlightFence = VK_NULL_HANDLE;

        VkDescriptorPool  m_ImGuiDescriptorPool = VK_NULL_HANDLE;

        uint32_t          m_CurrentImageIndex = 0;
        bool              m_FramebufferResized = false;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H