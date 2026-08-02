#ifndef VK_SWAPCHAIN_H
#define VK_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include <vector>
#include <array>
#include <cstdint>

#include "Api.h"
#include "Renderer/RHI/RHI_Renderer.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	class NV_API VK_Swapchain {
	public:
		VK_Swapchain() = default;
		~VK_Swapchain() = default;

		// Creates the swapchain, image views, sync objects, and per-image command buffers.
		bool Create(VkPhysicalDevice physicalDevice,
			VkDevice device,
			VkSurfaceKHR surface,
			VkQueue graphicsQueue,
			VkQueue presentQueue,
			uint32_t graphicsQueueFamily,
			uint32_t presentQueueFamily,
			const RHI::RHI_SwapchainDesc& desc);

		void Destroy();

		struct NV_API VK_FrameSync {
			VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
			VkFence     m_InFlightFence = VK_NULL_HANDLE;
		};

		struct NV_API VK_SwapchainImage {
			VkImage     m_Image = VK_NULL_HANDLE;
			VkImageView m_ImageView = VK_NULL_HANDLE;
		};

		struct NV_API VK_SwapchainSupportDetails {
			VkSurfaceCapabilitiesKHR        m_Capabilities{};
			std::vector<VkSurfaceFormatKHR> m_Formats;
			std::vector<VkPresentModeKHR>   m_PresentModes;
		};

		VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
		const VkExtent2D& GetExtent() const { return m_SwapchainExtent; }
		VkFormat GetImageFormat() const { return m_SwapchainImageFormat; }
		VkPresentModeKHR GetPresentMode() const { return m_PresentMode; }

		uint32_t GetFramesInFlight() const { return m_FramesInFlight; }
		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Images.size()); }

		VkCommandPool GetCommandPool() const { return m_CommandPool; }
		const std::vector<VK_SwapchainImage>& GetImages() const { return m_Images; }
		std::vector<VK_SwapchainImage>& GetImages() { return m_Images; }
		const std::vector<VkFence>& GetImagesInFlight() const { return m_ImagesInFlight; }
		std::vector<VkFence>& GetImagesInFlight() { return m_ImagesInFlight; }
		const std::vector<VK_FrameSync>& GetFrameSync() const { return m_FrameSync; }
		std::vector<VK_FrameSync>& GetFrameSync() { return m_FrameSync; }

		void SetCurrentFrame(uint32_t frameIndex) { m_CurrentFrame = frameIndex; }
		uint32_t GetCurrentFrame() const { return m_CurrentFrame; }
		void AdvanceFrame() { m_CurrentFrame = (m_CurrentFrame + 1) % m_FramesInFlight; }

		void SetAcquiredImageIndex(uint32_t idx) { m_AcquiredImage = idx; }
		uint32_t GetAcquiredImageIndex() const { return m_AcquiredImage; }

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
		bool CreateCommandPoolAndBuffers();
		bool RecreateCommandBuffers();

		bool CreateSyncObjects();
		void DestroySyncObjects();

		void LogSwapchainConfiguration(uint32_t swapchainImageCount) const;

		VK_SwapchainSupportDetails QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const;
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const;
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const;
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;

		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice         m_Device = VK_NULL_HANDLE;
		VkSurfaceKHR     m_Surface = VK_NULL_HANDLE;

		VkQueue          m_GraphicsQueue = VK_NULL_HANDLE;
		VkQueue          m_PresentQueue = VK_NULL_HANDLE;

		uint32_t         m_GraphicsQueueFamily = UINT32_MAX;
		uint32_t         m_PresentQueueFamily = UINT32_MAX;

		RHI::RHI_SwapchainDesc m_Desc{};

		VkSwapchainKHR   m_Swapchain = VK_NULL_HANDLE;
		VkFormat         m_SwapchainImageFormat{};
		VkExtent2D       m_SwapchainExtent{};
		VkPresentModeKHR m_PresentMode = VK_PRESENT_MODE_FIFO_KHR;
		uint32_t         m_FramesInFlight = 3;

		std::vector<VK_SwapchainImage> m_Images;

		VkCommandPool   m_CommandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer> m_CommandBuffers;

		std::vector<VK_FrameSync> m_FrameSync;
		std::vector<VkSemaphore>  m_RenderFinishedSemaphores;
		std::vector<VkFence>      m_ImagesInFlight;

		uint32_t m_CurrentFrame = 0;
		uint32_t m_AcquiredImage = 0;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_SWAPCHAIN_H