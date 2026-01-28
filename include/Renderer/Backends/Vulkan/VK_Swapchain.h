#ifndef VK_SWAPCHAIN_H
#define VK_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include <vector>
#include <thread>
#include <array>

namespace Nova::Core::Renderer::Backends::Vulkan {

	class VK_Swapchain {
	public:
		VK_Swapchain() = default;
		~VK_Swapchain() = default;

		static constexpr uint32_t FRAMES_IN_FLIGHT = 3;
		static constexpr uint32_t WORKER_THREAD_COUNT = 4;

		// Create() doit recevoir ce dont la swapchain a besoin
		bool Create(VkPhysicalDevice physicalDevice,
			VkDevice device,
			VkSurfaceKHR surface,
			VkQueue graphicsQueue,
			VkQueue presentQueue,
			uint32_t graphicsQueueFamily,
			uint32_t presentQueueFamily,
			VkExtent2D initialWindowExtent);

		void Destroy();

		bool CreateSwapchain();
		bool RecreateSwapchain(VkExtent2D newWindowExtent);
		void DestroySwapchain();

		// ----------------------------
		// Frame lifecycle helpers
		// ----------------------------
		bool AcquireNextImage(uint32_t& outImageIndex);
		bool Present(uint32_t imageIndex);

		// ----------------------------
		// Getters
		// ----------------------------
		VkSwapchainKHR GetSwapchain() const { return m_Swapchain; }
		VkFormat       GetImageFormat() const { return m_SwapchainImageFormat; }
		VkExtent2D     GetExtent() const { return m_SwapchainExtent; }
		uint32_t       GetMinImageCount() const { return m_MinImageCount; }

		VkRenderPass   GetRenderPass() const { return m_RenderPass; }

		uint32_t       GetCurrentFrame() const { return m_CurrentFrame; }
		uint32_t       GetCurrentImageIndex() const { return m_CurrentImageIndex; }

		uint32_t GetImageCount() const { return static_cast<uint32_t>(m_Frames.size()); }

		// Framebuffers
		VkFramebuffer GetFramebuffer(uint32_t imageIndex) const;

		// Sync objects for the current frame-in-flight
		VkSemaphore GetImageAvailableSemaphore() const;
		VkSemaphore GetRenderFinishedSemaphore() const;
		VkFence     GetInFlightFence() const;

		// Command buffers (par frame, par thread)
		VkCommandBuffer GetPrimaryCommandBuffer(uint32_t frameIndex) const;
		VkCommandBuffer GetSecondaryCommandBuffer(uint32_t frameIndex, uint32_t threadIndex) const;

		// Resize flag (si tu veux le set depuis ton window callback)
		void SetFramebufferResized(bool resized) { m_FramebufferResized = resized; }
		bool WasFramebufferResized() const { return m_FramebufferResized; }

	private:
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
			VkSurfaceCapabilitiesKHR        capabilities{};
			std::vector<VkSurfaceFormatKHR> formats;
			std::vector<VkPresentModeKHR>   presentModes;
		};

	private:
		// Query helpers
		SwapchainSupportDetails QuerySwapchainSupport(VkPhysicalDevice physicalDevice) const;
		VkSurfaceFormatKHR      ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const;
		VkPresentModeKHR        ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const;
		VkExtent2D              ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D windowExtent) const;

		bool CreateRenderPass();
		void DestroyRenderPass();

		bool CreateFramebuffers();
		void DestroyFramebuffers();

		bool CreateSyncObjects();
		void DestroySyncObjects();

		bool CreateCommandPoolsAndBuffers();
		void DestroyCommandPoolsAndBuffers();

	private:
		// Vulkan handles nécessaires
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

		// Sync per frame-in-flight
		std::array<VK_FrameSync, FRAMES_IN_FLIGHT> m_FrameSync{};
		uint32_t m_CurrentFrame = 0;
		uint32_t m_CurrentImageIndex = 0;

		// Track fence par image (pour éviter de réutiliser une image encore en vol)
		std::vector<VkFence> m_ImagesInFlight;

		bool m_FramebufferResized = false;

		// Render pass swapchain
		VkRenderPass m_RenderPass = VK_NULL_HANDLE;

		// Command pools + buffers
		// - 1 primary CB per frame
		// - 4 secondary CB per frame (1 per worker thread)
		std::array<VkCommandPool, FRAMES_IN_FLIGHT> m_PrimaryCommandPools{};
		std::array<VkCommandBuffer, FRAMES_IN_FLIGHT> m_PrimaryCommandBuffers{};

		std::array<std::array<VkCommandPool, WORKER_THREAD_COUNT>, FRAMES_IN_FLIGHT> m_SecondaryCommandPools{};
		std::array<std::array<VkCommandBuffer, WORKER_THREAD_COUNT>, FRAMES_IN_FLIGHT> m_SecondaryCommandBuffers{};
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_SWAPCHAIN_H