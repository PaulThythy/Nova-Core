#ifndef VK_SWAPCHAIN_H
#define VK_SWAPCHAIN_H

#include <vulkan/vulkan.h>

#include <vector>
#include <thread>
#include <array>

#include "Api.h"
#include "Core/Application.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	class NV_API VK_Swapchain {
	public:
		VK_Swapchain() = default;
		~VK_Swapchain() = default;

		static constexpr uint32_t FRAMES_IN_FLIGHT = 3;
		static constexpr uint32_t MAX_MODEL_INSTANCES = 1024;

		// Create() should receive every dependency required by the swapchain.
		bool Create(VkPhysicalDevice physicalDevice,
			VkDevice device,
			VkSurfaceKHR surface,
			VkQueue graphicsQueue,
			VkQueue presentQueue,
			uint32_t graphicsQueueFamily,
			uint32_t presentQueueFamily);

		void Destroy();

		struct NV_API VK_FrameSync {
			VkSemaphore m_ImageAvailableSemaphore = VK_NULL_HANDLE;
			VkFence     m_InFlightFence = VK_NULL_HANDLE;
		};

		struct NV_API VK_Frame {
			VkImage       m_Image = VK_NULL_HANDLE;
			VkImageView   m_ImageView = VK_NULL_HANDLE;
			VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
		};

		struct NV_API VK_SwapchainSupportDetails {
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

		// Currently acquired swapchain image
		void SetAcquiredImageIndex(uint32_t idx) { m_AquiredImage = idx; }
		uint32_t GetAcquiredImageIndex() const { return m_AquiredImage; }

		VkPipeline& GetModelPipeline() { return m_ModelPipeline; }
		VkPipelineLayout& GetModelPipelineLayout() { return m_ModelPipelineLayout; }

		// UBOs/SSBO et descriptor set (binding 0 = Globals, 1 = MVP, 2 = Instances, 3 = Material)
		VkBuffer GetGlobalsUBOBuffer() const { return m_GlobalsUBOBuffer; }
		VkDeviceMemory GetGlobalsUBOMemory() const { return m_GlobalsUBOMemory; }
		VkBuffer GetMVPUBOBuffer() const { return m_MVPUBOBuffer; }
		VkDeviceMemory GetMVPUBOMemory() const { return m_MVPUBOMemory; }
		VkBuffer GetMaterialUBOBuffer() const { return m_MaterialUBOBuffer; }
		VkDeviceMemory GetMaterialUBOMemory() const { return m_MaterialUBOMemory; }
		VkBuffer GetInstanceBuffer() const { return m_InstanceBuffer; }
		VkDeviceMemory GetInstanceBufferMemory() const { return m_InstanceBufferMemory; }
		VkDeviceSize GetInstanceBufferSize() const { return m_InstanceBufferSize; }
		VkDescriptorSet GetSceneDescriptorSet() const { return m_SceneDescriptorSet; }

		// Viewport offscreen: render pass only (same pipeline as main window = model pipeline)
		VkRenderPass GetViewportRenderPass() const { return m_ViewportRenderPass; }
		VkFormat GetSwapchainImageFormat() const { return m_SwapchainImageFormat; }
		VkFormat GetDepthFormat() const { return m_DepthFormat; }

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
		bool CreateViewportRenderPass();
		void DestroyViewportRenderPass();
		bool CreateDepthResources();
		void DestroyDepthResources();

		// ImGui
		bool CreateImGuiDescriptorPool();
		void DestroyImGuiDescriptorPool();

		VK_SwapchainSupportDetails QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface);
		VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats);
		VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes);
		VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities);

		// Required Vulkan handles
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
		VkDescriptorSetLayout m_SceneSetLayout = VK_NULL_HANDLE;
		VkBuffer         m_GlobalsUBOBuffer = VK_NULL_HANDLE;
		VkDeviceMemory   m_GlobalsUBOMemory = VK_NULL_HANDLE;
		VkBuffer         m_MVPUBOBuffer = VK_NULL_HANDLE;
		VkDeviceMemory   m_MVPUBOMemory = VK_NULL_HANDLE;
		VkBuffer         m_MaterialUBOBuffer = VK_NULL_HANDLE;
		VkDeviceMemory   m_MaterialUBOMemory = VK_NULL_HANDLE;
		VkBuffer         m_InstanceBuffer = VK_NULL_HANDLE;
		VkDeviceMemory   m_InstanceBufferMemory = VK_NULL_HANDLE;
		VkDeviceSize     m_InstanceBufferSize = 0;
		VkDescriptorSet  m_SceneDescriptorSet = VK_NULL_HANDLE;

		// Viewport offscreen render pass only (color finalLayout = SHADER_READ_ONLY for ImGui)
		VkRenderPass m_ViewportRenderPass = VK_NULL_HANDLE;

		// Depth buffer
		VkImage        m_DepthImage = VK_NULL_HANDLE;
		VkDeviceMemory m_DepthImageMemory = VK_NULL_HANDLE;
		VkImageView    m_DepthImageView = VK_NULL_HANDLE;
		VkFormat       m_DepthFormat = VK_FORMAT_D32_SFLOAT;

		// Commands (single primary command buffer)
		VkCommandPool   m_CommandPool = VK_NULL_HANDLE;
		std::vector<VkCommandBuffer>  m_CommandBuffers;

		// Frames in flight: 3
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