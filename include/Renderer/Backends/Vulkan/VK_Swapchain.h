#ifndef VK_SWAPCHAIN_H
#define VK_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include <vector>
#include <thread>
#include <array>

#include "Core/Application.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	class VK_Swapchain {
	public:
		VK_Swapchain() = default;
		~VK_Swapchain() = default;

		static constexpr uint32_t FRAMES_IN_FLIGHT = 3;

		// Create() doit recevoir ce dont la swapchain a besoin
		bool Create(VkPhysicalDevice physicalDevice,
			VkDevice device,
			VkSurfaceKHR surface,
			VkQueue graphicsQueue,
			VkQueue presentQueue,
			uint32_t graphicsQueueFamily,
			uint32_t presentQueueFamily);

		void Destroy();

		struct VK_FrameSync {
			VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
			VkFence     m_InFlightFence = VK_NULL_HANDLE;
		};

		struct VK_Frame {
			VkImage       m_Image = VK_NULL_HANDLE;
			VkImageView   m_ImageView = VK_NULL_HANDLE;
			VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
		};

		struct VK_SwapchainSupportDetails {
			VkSurfaceCapabilitiesKHR        m_Capabilities{};
			std::vector<VkSurfaceFormatKHR> m_Formats;
			std::vector<VkPresentModeKHR>   m_PresentModes;
		};

		VkSwapchainKHR& GetSwapchain() { return m_Swapchain; }
		VkExtent2D& GetExtent() { return m_SwapchainExtent; }

		VkRenderPass& GetBackBufferRenderPass() { return m_BackBufferRenderPass; }

		VkDescriptorPool& GetImGuiDescriptorPool() { return m_ImGuiDescriptorPool; }

		VkCommandPool GetCommandPool() { return m_CommandPool; }

		std::vector<VK_Frame>& GetFrames() { return m_Frames; }
		std::vector<VkFence>& GetImagesInFlight() { return m_ImagesInFlight; }
		std::array<VK_FrameSync, FRAMES_IN_FLIGHT>& GetFrameSync() { return m_FrameSync; }

		void SetCurrentFrame(uint32_t frameIndex) { m_CurrentFrame = frameIndex; }
		uint32_t GetCurrentFrame() { return m_CurrentFrame; }
		void AdvanceFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % FRAMES_IN_FLIGHT; }

		// swapchain image aquired
		void SetAcquiredImageIndex(uint32_t idx) { m_AquiredImage = idx; }
		uint32_t GetAcquiredImageIndex() const { return m_AquiredImage; }

		VkPipeline& GetModelPipeline() { return m_ModelPipeline; }
		VkPipelineLayout& GetModelPipelineLayout() { return m_ModelPipelineLayout; }

		std::vector<VkCommandBuffer>& GetCommandBuffers() { return m_CommandBuffers; }

		VkSemaphore GetRenderFinishedSemaphore(uint32_t imageIndex) const {
			if (imageIndex < m_RenderFinishedSemaphores.size())
				return m_RenderFinishedSemaphores[imageIndex];
			return VK_NULL_HANDLE;
		}

		bool RecreateSwapchain();

	private:

		bool CreateSwapchain();
		void DestroySwapchain();

		bool CreateImageViews();
		bool CreateBackBufferRenderPass();
		bool CreateFramebuffers();

		// Commands & sync
		bool CreateCommandPoolAndBuffers();
		bool RecreateCommandBuffers();

		bool CreateSyncObjects();
		void DestroySyncObjects();

		// Minimal pipeline
		void CreateModelPipeline();
		void DestroyModelPipeline();
		bool CreateDepthResources();
		void DestroyDepthResources();

		// ImGui
		bool CreateImGuiDescriptorPool();
		void DestroyImGuiDescriptorPool();

		VK_SwapchainSupportDetails QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

		// Vulkan handles n�cessaires
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice         m_Device = VK_NULL_HANDLE;
		VkSurfaceKHR     m_Surface = VK_NULL_HANDLE;

		VkQueue          m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue          m_PresentQueue = VK_NULL_HANDLE;

		uint32_t         m_GraphicsQueueFamily = UINT32_MAX;
		uint32_t         m_PresentQueueFamily = UINT32_MAX;

		VkExtent2D       m_WindowExtent{ 0, 0 };

		// Swapchain state
		VkSwapchainKHR   m_Swapchain = VK_NULL_HANDLE;
		VkFormat         m_SwapchainImageFormat{};
		VkExtent2D       m_SwapchainExtent{};
		uint32_t         m_MinImageCount = 0;

		std::vector<VK_Frame> m_Frames;

		// Render pass
		VkRenderPass m_BackBufferRenderPass = VK_NULL_HANDLE;

		// Pipeline (triangle)
		VkPipeline       m_ModelPipeline = VK_NULL_HANDLE;
		VkPipelineLayout m_ModelPipelineLayout = VK_NULL_HANDLE;

		// Depth buffer
		VkImage        m_DepthImage = VK_NULL_HANDLE;
		VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;
		VkImageView    m_DepthImageView = VK_NULL_HANDLE;
		VkFormat       m_DepthFormat = VK_FORMAT_D32_SFLOAT;

		// Commands (single primary command buffer)
		VkCommandPool   m_CommandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer>  m_CommandBuffers;

		// Frames in flight : 3
		std::array<VK_FrameSync, FRAMES_IN_FLIGHT> m_FrameSync{};

		// Render finished semaphore per swapchain image (indexed by acquired image)
		std::vector<VkSemaphore> m_RenderFinishedSemaphores;

		std::vector<VkFence> m_ImagesInFlight;

		// ImGui resources
		VkDescriptorPool m_ImGuiDescriptorPool = VK_NULL_HANDLE;

		// Per-frame state
		uint32_t m_CurrentFrame = 0;
		uint32_t m_AquiredImage = 0;
		bool     m_FramebufferResized = false;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_SWAPCHAIN_H