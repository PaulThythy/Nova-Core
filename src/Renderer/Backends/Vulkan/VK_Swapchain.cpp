#include "Renderer/Backends/Vulkan/VK_Swapchain.h"

#include <algorithm>
#include <limits>
#include <fstream>
#include <glm/glm.hpp>

#include "Core/Assert.h"
#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Graphics/Vertex.h"

#include "Asset/AssetManager.h"
#include "Asset/Assets/ShaderAsset.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	VK_Swapchain::VK_SwapchainSupportDetails VK_Swapchain::QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {

		VK_SwapchainSupportDetails details{};
		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &details.m_Capabilities);

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
		if (formatCount) {
			details.m_Formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, details.m_Formats.data());
		}

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
		if (presentModeCount) {
			details.m_PresentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, details.m_PresentModes.data());
		}

		return details;
	}

	VkSurfaceFormatKHR VK_Swapchain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
		NV_ASSERT_MSG(!availableFormats.empty(), "No Vulkan surface formats are available for the swapchain.");
		if (availableFormats.empty()) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		// If the surface has no preferred format, choose one ourselves.
		if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
			VkSurfaceFormatKHR fmt{};
			fmt.format = VK_FORMAT_B8G8R8A8_UNORM;
			fmt.colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
			return fmt;
		}

		// Prefer SRGB if available (common for ImGui / standard rendering).
		for (const auto& fmt : availableFormats) {
			if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
				fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return fmt;
			}
		}

		// Fallback to UNORM if SRGB isn't available.
		for (const auto& fmt : availableFormats) {
			if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM &&
				fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return fmt;
			}
		}

		// Otherwise just use the first available format.
		return availableFormats[0];
	}

	VkPresentModeKHR VK_Swapchain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
		// Mailbox = low latency, no tearing (if available).
		for (const auto& pm : availablePresentModes) {
			if (pm == VK_PRESENT_MODE_MAILBOX_KHR)
				return pm;
		}

		// Immediate = tearing possible, but low latency.
		for (const auto& pm : availablePresentModes) {
			if (pm == VK_PRESENT_MODE_IMMEDIATE_KHR)
				return pm;
		}

		// FIFO is guaranteed by the spec.
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D VK_Swapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
		// If currentExtent is not UINT32_MAX, the surface size is defined and must be used.
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		}

		// Otherwise, we choose based on the actual window size.
		SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(window, &w, &h);

		VkExtent2D actualExtent{};
		actualExtent.width = static_cast<uint32_t>(std::max(1, w));
		actualExtent.height = static_cast<uint32_t>(std::max(1, h));

		actualExtent.width = std::clamp(
			actualExtent.width,
			capabilities.minImageExtent.width,
			capabilities.maxImageExtent.width
		);

		actualExtent.height = std::clamp(
			actualExtent.height,
			capabilities.minImageExtent.height,
			capabilities.maxImageExtent.height
		);

		return actualExtent;
	}

	// ---------------------------------------------
	// Public API
	// ---------------------------------------------
	bool VK_Swapchain::Create(VkPhysicalDevice physicalDevice,
		VkDevice device,
		VkSurfaceKHR surface,
		VkQueue graphicsQueue,
		VkQueue presentQueue,
		uint32_t graphicsQueueFamily,
		uint32_t presentQueueFamily)
	{
		if (physicalDevice == VK_NULL_HANDLE || device == VK_NULL_HANDLE || surface == VK_NULL_HANDLE) {
			NV_LOG_ERROR("VK_Swapchain::Create failed: invalid physicalDevice/device/surface");
			return false;
		}

		m_PhysicalDevice = physicalDevice;
		m_Device = device;
		m_Surface = surface;

		m_GraphicsQueue = graphicsQueue;
		m_PresentQueue = presentQueue;

		m_GraphicsQueueFamily = graphicsQueueFamily;
		m_PresentQueueFamily = presentQueueFamily;

		if (!CreateSwapchain())               return false;
		if (!CreateDepthResources())	      return false;
		if (!CreateImageViews())              return false;
		if (!CreateBackBufferRenderPass())              return false;
		if (!CreateFramebuffers())            return false;

		if (!CreateCommandPoolAndBuffers())   return false;
		if (!CreateSyncObjects())             return false;

		if (!CreateImGuiDescriptorPool())    return false;

		CreateModelPipeline(); // optional (allocates scene descriptor set from ImGui pool)

		if (!CreateViewportRenderPass())      return false;

		NV_LOG_INFO("VK_Swapchain created successfully.");
		return true;
	}

	void VK_Swapchain::Destroy() {
		if (m_Device == VK_NULL_HANDLE)
			return;

		vkDeviceWaitIdle(m_Device);

		DestroyModelPipeline();
		DestroyViewportRenderPass();

		DestroyDepthResources();

		DestroyImGuiDescriptorPool();

		DestroySwapchain();

		if (m_BackBufferRenderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(m_Device, m_BackBufferRenderPass, nullptr);
			m_BackBufferRenderPass = VK_NULL_HANDLE;
		}

		DestroySyncObjects();

		if (m_CommandPool != VK_NULL_HANDLE) {
			vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
			m_CommandPool = VK_NULL_HANDLE;
		}

		m_PhysicalDevice = VK_NULL_HANDLE;
		m_Device = VK_NULL_HANDLE;
		m_Surface = VK_NULL_HANDLE;

		m_GraphicsQueue = VK_NULL_HANDLE;
		m_PresentQueue = VK_NULL_HANDLE;

		m_GraphicsQueueFamily = UINT32_MAX;
		m_PresentQueueFamily = UINT32_MAX;

		m_WindowExtent = { 0, 0 };
		m_FramebufferResized = false;

		NV_LOG_INFO("VK_Swapchain destroyed.");
	}

	bool VK_Swapchain::CreateSwapchain() {
		auto swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice, m_Surface);

		VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.m_Formats);
		VkPresentModeKHR   presentMode = ChooseSwapPresentMode(swapChainSupport.m_PresentModes);
		VkExtent2D         extent = ChooseSwapExtent(swapChainSupport.m_Capabilities);

		m_SwapchainExtent = extent;

		uint32_t imageCount = std::max(
			swapChainSupport.m_Capabilities.minImageCount,
			FRAMES_IN_FLIGHT
		);
		if (swapChainSupport.m_Capabilities.maxImageCount > 0 &&
			imageCount > swapChainSupport.m_Capabilities.maxImageCount) {
			imageCount = swapChainSupport.m_Capabilities.maxImageCount;
		}

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = m_Surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

		uint32_t queueFamilyIndices[] = { m_GraphicsQueueFamily, m_PresentQueueFamily };
		if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		createInfo.preTransform = swapChainSupport.m_Capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VkResult res = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain);
		CheckVkResult(res);
		if (res != VK_SUCCESS) return false;

		uint32_t actualImageCount = 0;
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualImageCount, nullptr);

		std::vector<VkImage> images(actualImageCount);
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualImageCount, images.data());

		m_Frames.clear();
		m_Frames.resize(actualImageCount);
		for (uint32_t i = 0; i < actualImageCount; ++i) {
			m_Frames[i].m_Image = images[i];
		}

		m_SwapchainImageFormat = surfaceFormat.format;
		m_SwapchainExtent = extent;

		// fence-per-image tracking
		m_ImagesInFlight.assign(m_Frames.size(), VK_NULL_HANDLE);

		NV_LOG_INFO(("Swapchain created with " + std::to_string(actualImageCount) + " images.").c_str());
		return true;
	}

	bool VK_Swapchain::RecreateSwapchain() {
		SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
		int w = 0, h = 0;
		SDL_GetWindowSizeInPixels(window, &w, &h);

		if (w <= 0 || h <= 0)
			return false;

		vkDeviceWaitIdle(m_Device);
		DestroySwapchain();
		DestroyDepthResources();

		if (!CreateSwapchain())        return false;
		if (!CreateDepthResources())   return false;
		if (!CreateImageViews())       return false;
		if (!CreateFramebuffers())     return false;
		if (!RecreateCommandBuffers()) return false;

		// Recreate render finished semaphores for new image count
		m_RenderFinishedSemaphores.resize(m_Frames.size());
		VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i) {
			VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinishedSemaphores[i]);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}

		m_ImagesInFlight.assign(m_Frames.size(), VK_NULL_HANDLE);

		return true;
	}

	void VK_Swapchain::DestroySwapchain() {
		for (auto& f : m_Frames) {
			if (f.m_Framebuffer) vkDestroyFramebuffer(m_Device, f.m_Framebuffer, nullptr);
			if (f.m_ImageView)   vkDestroyImageView(m_Device, f.m_ImageView, nullptr);
			f.m_Framebuffer = VK_NULL_HANDLE;
			f.m_ImageView = VK_NULL_HANDLE;
			f.m_Image = VK_NULL_HANDLE;
		}
		m_Frames.clear();

		if (!m_CommandBuffers.empty() && m_CommandPool != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(m_Device, m_CommandPool,
				(uint32_t)m_CommandBuffers.size(), m_CommandBuffers.data());
			m_CommandBuffers.clear();
		}

		if (m_Swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}

		// Destroy render finished semaphores for each swapchain image
		for (auto& sem : m_RenderFinishedSemaphores) {
			if (sem) {
				vkDestroySemaphore(m_Device, sem, nullptr);
				sem = VK_NULL_HANDLE;
			}
		}
		m_RenderFinishedSemaphores.clear();

		m_ImagesInFlight.clear();
		m_CurrentFrame = 0;
	}

	bool VK_Swapchain::CreateBackBufferRenderPass() {
		if (m_BackBufferRenderPass != VK_NULL_HANDLE)
			return true;

		// Color attachment
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_SwapchainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		// Depth attachment
		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = m_DepthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkAttachmentReference depthRef{};
		depthRef.attachment = 1;
		depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
			| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VkResult res = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_BackBufferRenderPass);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	bool VK_Swapchain::CreateViewportRenderPass() {
		if (m_ViewportRenderPass != VK_NULL_HANDLE)
			return true;

		// Same as back buffer but color initial/final = SHADER_READ_ONLY (after first frame image stays in that layout)
		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_SwapchainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; // UNDEFINED on first use, then SHADER_READ from previous frame
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = m_DepthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{};
		colorRef.attachment = 0;
		colorRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
		VkAttachmentReference depthRef{};
		depthRef.attachment = 1;
		depthRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
			| VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
			| VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VkResult res = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_ViewportRenderPass);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Swapchain::DestroyViewportRenderPass() {
		if (m_ViewportRenderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(m_Device, m_ViewportRenderPass, nullptr);
			m_ViewportRenderPass = VK_NULL_HANDLE;
		}
	}

	bool VK_Swapchain::CreateFramebuffers() {
		for (auto& f : m_Frames) f.m_Framebuffer = VK_NULL_HANDLE;

		for (size_t i = 0; i < m_Frames.size(); ++i) {
			std::array<VkImageView, 2> attachments = {
				m_Frames[i].m_ImageView,
				m_DepthImageView            // <-- depth
			};

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_BackBufferRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = m_SwapchainExtent.width;
			framebufferInfo.height = m_SwapchainExtent.height;
			framebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &m_Frames[i].m_Framebuffer);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}
		return true;
	}

	bool VK_Swapchain::CreateImageViews() {
		for (auto& f : m_Frames) f.m_ImageView = VK_NULL_HANDLE;

		for (size_t i = 0; i < m_Frames.size(); i++) {
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = m_Frames[i].m_Image;
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = m_SwapchainImageFormat;
			createInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
									  VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			VkResult res = vkCreateImageView(m_Device, &createInfo, nullptr, &m_Frames[i].m_ImageView);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}
		return true;
	}

	bool VK_Swapchain::CreateSyncObjects() {
		VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		// Create image available semaphores and in-flight fences per frame
		for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
			VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_FrameSync[i].m_ImageAvailableSemaphore);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;

			res = vkCreateFence(m_Device, &fenceInfo, nullptr, &m_FrameSync[i].m_InFlightFence);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}

		// Create render finished semaphores per swapchain image
		m_RenderFinishedSemaphores.resize(m_Frames.size());
		for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i) {
			VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinishedSemaphores[i]);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}

		// fence-per-swapchain-image tracking
		m_ImagesInFlight.assign(m_Frames.size(), VK_NULL_HANDLE);

		m_CurrentFrame = 0;
		return true;
	}

	void VK_Swapchain::DestroySyncObjects() {
		for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; ++i) {
			if (m_FrameSync[i].m_ImageAvailableSemaphore) {
				vkDestroySemaphore(m_Device, m_FrameSync[i].m_ImageAvailableSemaphore, nullptr);
				m_FrameSync[i].m_ImageAvailableSemaphore = VK_NULL_HANDLE;
			}
			if (m_FrameSync[i].m_InFlightFence) {
				vkDestroyFence(m_Device, m_FrameSync[i].m_InFlightFence, nullptr);
				m_FrameSync[i].m_InFlightFence = VK_NULL_HANDLE;
			}
		}

		for (auto& sem : m_RenderFinishedSemaphores) {
			if (sem) {
				vkDestroySemaphore(m_Device, sem, nullptr);
				sem = VK_NULL_HANDLE;
			}
		}
		m_RenderFinishedSemaphores.clear();

		m_ImagesInFlight.clear();
	}

	bool VK_Swapchain::CreateImGuiDescriptorPool() {
		std::array<VkDescriptorPoolSize, 11> pool_sizes = {
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLER,                1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,          1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,          1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER,   1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER,   1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,         1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,         1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 1000 },
			VkDescriptorPoolSize{ VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT,       1000 }
		};

		VkDescriptorPoolCreateInfo pool_info{};
		pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		pool_info.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		pool_info.maxSets = 1000 * static_cast<uint32_t>(pool_sizes.size());
		pool_info.poolSizeCount = static_cast<uint32_t>(pool_sizes.size());
		pool_info.pPoolSizes = pool_sizes.data();

		VkResult res = vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_ImGuiDescriptorPool);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Swapchain::DestroyImGuiDescriptorPool() {
		if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(m_Device, m_ImGuiDescriptorPool, nullptr);
			m_ImGuiDescriptorPool = VK_NULL_HANDLE;
		}
	}

	bool VK_Swapchain::CreateCommandPoolAndBuffers() {
		VkCommandPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
		poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
		poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

		VkResult res = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool);
		CheckVkResult(res);
		if (res != VK_SUCCESS)
			return false;

		return RecreateCommandBuffers();
	}

	bool VK_Swapchain::RecreateCommandBuffers() {
		if (m_CommandPool == VK_NULL_HANDLE)
			return false;

		m_CommandBuffers.resize(m_Frames.size());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_CommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

		VkResult res = vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data());
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Swapchain::CreateModelPipeline() {
		if (m_ModelPipeline != VK_NULL_HANDLE) return;

		std::filesystem::path shaderDir = std::filesystem::current_path()
			/ "Nova-Core" / "Resources" / "Engine" / "Shaders";

		using Nova::Core::Asset::AssetManager;
		using Nova::Core::Asset::Assets::ShaderAsset;
		using Nova::Core::Renderer::RHI::RHI_ShaderStage;

		Nova::Core::Renderer::RHI::RHI_ShaderDesc vDesc{};
		vDesc.m_Stage = RHI_ShaderStage::Vertex;

		Nova::Core::Renderer::RHI::RHI_ShaderDesc fDesc{};
		fDesc.m_Stage = RHI_ShaderStage::Fragment;

		auto vertAsset = AssetManager::Get().Acquire<ShaderAsset>(shaderDir / "model.vert.slang", vDesc);
		auto fragAsset = AssetManager::Get().Acquire<ShaderAsset>(shaderDir / "model.frag.slang", fDesc);

		if (!vertAsset || !fragAsset) { NV_LOG_WARN("CreateModelPipeline: failed to acquire shaders"); return; }
		if (!vertAsset->Compile()) { NV_LOG_WARN(("VS compile failed:\n" + vertAsset->GetLastLog()).c_str()); return; }
		if (!fragAsset->Compile()) { NV_LOG_WARN(("FS compile failed:\n" + fragAsset->GetLastLog()).c_str()); return; }

		VK_ShaderModule vertModule, fragModule;
		if (!vertModule.Create(m_Device, vertAsset->GetSpirv()) ||
			!fragModule.Create(m_Device, fragAsset->GetSpirv()))
		{
			NV_LOG_WARN("CreateModelPipeline: failed to create shader modules");
			return;
		}

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertModule.GetModule();
		stages[0].pName = "main";

		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = fragModule.GetModule();
		stages[1].pName = "main";

		// Vertex buffer stride matches the full Vertex layout; only declare attributes
		// consumed by model.vert.slang (locations 0–1) to satisfy validation.
		VkVertexInputBindingDescription binding{};
		binding.binding = 0;
		binding.stride = sizeof(Renderer::Graphics::Vertex);
		binding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

		std::array<VkVertexInputAttributeDescription, 6> attrs{};
		attrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Position) };
		attrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Normal) };
		attrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Renderer::Graphics::Vertex, m_TexCoord) };
		attrs[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Color) };
		attrs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Tangent) };
		attrs[5] = { 5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Bitangent) };

		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
		vertexInput.vertexBindingDescriptionCount = 1;
		vertexInput.pVertexBindingDescriptions = &binding;
		vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
		vertexInput.pVertexAttributeDescriptions = attrs.data();

		VkPipelineInputAssemblyStateCreateInfo inputAsm{};
		inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
		inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewportState{};
		viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
		viewportState.viewportCount = 1;
		viewportState.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo raster{};
		raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
		raster.polygonMode = VK_POLYGON_MODE_FILL;
		raster.cullMode = VK_CULL_MODE_BACK_BIT;
		raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
		raster.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo msaa{};
		msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		// ---- Depth test ----
		VkPipelineDepthStencilStateCreateInfo depthStencil{};
		depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
		depthStencil.depthTestEnable = VK_TRUE;
		depthStencil.depthWriteEnable = VK_TRUE;
		depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;

		VkPipelineColorBlendAttachmentState blendAttachment{};
		blendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
			VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo blend{};
		blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAttachment;

		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic{};
		dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic.dynamicStateCount = 2;
		dynamic.pDynamicStates = dynamicStates;

		// Descriptor set layout: matches NovaEngine ParameterBlock field order (Slang bindings 0..Count-1).
		VkDescriptorSetLayoutBinding bindings[4]{};
		bindings[0].binding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Globals);
		bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[0].descriptorCount = 1;
		bindings[0].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		bindings[1].binding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Mvp);
		bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[1].descriptorCount = 1;
		bindings[1].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		bindings[2].binding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Instances);
		bindings[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		bindings[2].descriptorCount = 1;
		bindings[2].stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
		bindings[3].binding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Material);
		bindings[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		bindings[3].descriptorCount = 1;
		bindings[3].stageFlags = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;

		VkDescriptorSetLayoutCreateInfo setLayoutInfo{};
		setLayoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		setLayoutInfo.bindingCount = 4;
		setLayoutInfo.pBindings = bindings;

		VkResult res = vkCreateDescriptorSetLayout(m_Device, &setLayoutInfo, nullptr, &m_SceneSetLayout);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { NV_LOG_WARN("CreateModelPipeline: failed to create scene set layout"); return; }

		// ---- Globals buffer ----
		const VkDeviceSize globalsSize = sizeof(Renderer::RHI::Globals);
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = globalsSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		res = vkCreateBuffer(m_Device, &bufInfo, nullptr, &m_BufGlobals);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		VkMemoryRequirements memReq;
		vkGetBufferMemoryRequirements(m_Device, m_BufGlobals, &memReq);
		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
		uint32_t memTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) { NV_LOG_WARN("CreateModelPipeline: no host-visible memory for Globals buffer"); DestroyModelPipeline(); return; }
		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		res = vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_BufGlobalsMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_Device, m_BufGlobals, m_BufGlobalsMemory, 0);

		// ---- MVP buffer ----
		const VkDeviceSize mvpSize = sizeof(Renderer::RHI::MVP);
		bufInfo.size = mvpSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		res = vkCreateBuffer(m_Device, &bufInfo, nullptr, &m_BufMvp);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkGetBufferMemoryRequirements(m_Device, m_BufMvp, &memReq);
		memTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) { NV_LOG_WARN("CreateModelPipeline: no host-visible memory for MVP buffer"); DestroyModelPipeline(); return; }
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		res = vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_BufMvpMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_Device, m_BufMvp, m_BufMvpMemory, 0);

		// ---- Materials buffer ----
		const VkDeviceSize materialSize = sizeof(Renderer::RHI::Material);
		bufInfo.size = materialSize;
		res = vkCreateBuffer(m_Device, &bufInfo, nullptr, &m_BufMaterials);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkGetBufferMemoryRequirements(m_Device, m_BufMaterials, &memReq);
		memTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) { DestroyModelPipeline(); return; }
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		res = vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_BufMaterialsMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_Device, m_BufMaterials, m_BufMaterialsMemory, 0);

		// ---- Instances buffer ----
		const VkDeviceSize instanceSize = sizeof(Renderer::RHI::Instance) * MAX_MODEL_INSTANCES;
		bufInfo.size = instanceSize;
		bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		res = vkCreateBuffer(m_Device, &bufInfo, nullptr, &m_BufInstances);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkGetBufferMemoryRequirements(m_Device, m_BufInstances, &memReq);
		memTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) { DestroyModelPipeline(); return; }
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		res = vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_BufInstancesMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_Device, m_BufInstances, m_BufInstancesMemory, 0);
		m_BufInstancesSize = instanceSize;

		// ---- Allocate descriptor set (from ImGui pool) ----
		VkDescriptorSetAllocateInfo allocSetInfo{};
		allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
		allocSetInfo.descriptorPool = m_ImGuiDescriptorPool;
		allocSetInfo.descriptorSetCount = 1;
		allocSetInfo.pSetLayouts = &m_SceneSetLayout;
		res = vkAllocateDescriptorSets(m_Device, &allocSetInfo, &m_SceneDescriptorSet);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { NV_LOG_WARN("CreateModelPipeline: failed to allocate scene descriptor set"); DestroyModelPipeline(); return; }

		VkDescriptorBufferInfo globalsBufInfo{};
		globalsBufInfo.buffer = m_BufGlobals;
		globalsBufInfo.offset = 0;
		globalsBufInfo.range = globalsSize;
		VkDescriptorBufferInfo mvpBufInfo{};
		mvpBufInfo.buffer = m_BufMvp;
		mvpBufInfo.offset = 0;
		mvpBufInfo.range = mvpSize;
		VkDescriptorBufferInfo materialBufInfo{};
		materialBufInfo.buffer = m_BufMaterials;
		materialBufInfo.offset = 0;
		materialBufInfo.range = materialSize;
		VkDescriptorBufferInfo instanceBufInfo{};
		instanceBufInfo.buffer = m_BufInstances;
		instanceBufInfo.offset = 0;
		instanceBufInfo.range = instanceSize;
		VkWriteDescriptorSet writes[4]{};
		writes[0].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[0].dstSet = m_SceneDescriptorSet;
		writes[0].dstBinding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Globals);
		writes[0].dstArrayElement = 0;
		writes[0].descriptorCount = 1;
		writes[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[0].pBufferInfo = &globalsBufInfo;
		writes[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[1].dstSet = m_SceneDescriptorSet;
		writes[1].dstBinding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Mvp);
		writes[1].dstArrayElement = 0;
		writes[1].descriptorCount = 1;
		writes[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[1].pBufferInfo = &mvpBufInfo;
		writes[2].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[2].dstSet = m_SceneDescriptorSet;
		writes[2].dstBinding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Instances);
		writes[2].dstArrayElement = 0;
		writes[2].descriptorCount = 1;
		writes[2].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
		writes[2].pBufferInfo = &instanceBufInfo;
		writes[3].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		writes[3].dstSet = m_SceneDescriptorSet;
		writes[3].dstBinding = static_cast<uint32_t>(Renderer::RHI::EngineResourceSlot::Material);
		writes[3].dstArrayElement = 0;
		writes[3].descriptorCount = 1;
		writes[3].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		writes[3].pBufferInfo = &materialBufInfo;
		vkUpdateDescriptorSets(m_Device, 4, writes, 0, nullptr);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = 1;
		layoutInfo.pSetLayouts = &m_SceneSetLayout;
		layoutInfo.pushConstantRangeCount = 0;
		layoutInfo.pPushConstantRanges = nullptr;

		res = vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_ModelPipelineLayout);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { NV_LOG_WARN("CreateModelPipeline: failed to create pipeline layout"); DestroyModelPipeline(); return; }

		VkGraphicsPipelineCreateInfo pipe{};
		pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipe.stageCount = 2;
		pipe.pStages = stages;
		pipe.pVertexInputState = &vertexInput;
		pipe.pInputAssemblyState = &inputAsm;
		pipe.pViewportState = &viewportState;
		pipe.pRasterizationState = &raster;
		pipe.pMultisampleState = &msaa;
		pipe.pDepthStencilState = &depthStencil;
		pipe.pColorBlendState = &blend;
		pipe.pDynamicState = &dynamic;
		pipe.layout = m_ModelPipelineLayout;
		pipe.renderPass = m_BackBufferRenderPass;
		pipe.subpass = 0;

		res = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipe, nullptr, &m_ModelPipeline);
		CheckVkResult(res);

		if (res == VK_SUCCESS) NV_LOG_INFO("Model pipeline created.");
		else { NV_LOG_WARN("CreateModelPipeline: pipeline creation failed."); DestroyModelPipeline(); }
	}

	void VK_Swapchain::DestroyModelPipeline() {
		if (m_ModelPipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(m_Device, m_ModelPipeline, nullptr);
			m_ModelPipeline = VK_NULL_HANDLE;
		}
		if (m_ModelPipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(m_Device, m_ModelPipelineLayout, nullptr);
			m_ModelPipelineLayout = VK_NULL_HANDLE;
		}
		if (m_SceneDescriptorSet != VK_NULL_HANDLE && m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
			vkFreeDescriptorSets(m_Device, m_ImGuiDescriptorPool, 1, &m_SceneDescriptorSet);
			m_SceneDescriptorSet = VK_NULL_HANDLE;
		}
		if (m_BufInstances != VK_NULL_HANDLE) { vkDestroyBuffer(m_Device, m_BufInstances, nullptr); m_BufInstances = VK_NULL_HANDLE; }
		if (m_BufInstancesMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Device, m_BufInstancesMemory, nullptr); m_BufInstancesMemory = VK_NULL_HANDLE; }
		m_BufInstancesSize = 0;
		if (m_BufMaterials != VK_NULL_HANDLE) { vkDestroyBuffer(m_Device, m_BufMaterials, nullptr); m_BufMaterials = VK_NULL_HANDLE; }
		if (m_BufMaterialsMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Device, m_BufMaterialsMemory, nullptr); m_BufMaterialsMemory = VK_NULL_HANDLE; }
		if (m_BufMvp != VK_NULL_HANDLE) { vkDestroyBuffer(m_Device, m_BufMvp, nullptr); m_BufMvp = VK_NULL_HANDLE; }
		if (m_BufMvpMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Device, m_BufMvpMemory, nullptr); m_BufMvpMemory = VK_NULL_HANDLE; }
		if (m_BufGlobals != VK_NULL_HANDLE) { vkDestroyBuffer(m_Device, m_BufGlobals, nullptr); m_BufGlobals = VK_NULL_HANDLE; }
		if (m_BufGlobalsMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Device, m_BufGlobalsMemory, nullptr); m_BufGlobalsMemory = VK_NULL_HANDLE; }
		if (m_SceneSetLayout != VK_NULL_HANDLE) {
			vkDestroyDescriptorSetLayout(m_Device, m_SceneSetLayout, nullptr);
			m_SceneSetLayout = VK_NULL_HANDLE;
		}
	}

	bool VK_Swapchain::CreateDepthResources() {
		// Find supported depth format
		const std::vector<VkFormat> candidates = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT
		};
		m_DepthFormat = VK_FORMAT_UNDEFINED;
		for (VkFormat fmt : candidates) {
			VkFormatProperties props;
			vkGetPhysicalDeviceFormatProperties(m_PhysicalDevice, fmt, &props);
			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				m_DepthFormat = fmt;
				break;
			}
		}
		if (m_DepthFormat == VK_FORMAT_UNDEFINED) {
			NV_LOG_ERROR("VK_Swapchain: no supported depth format found");
			return false;
		}

		// Create depth image
		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = m_SwapchainExtent.width;
		imageInfo.extent.height = m_SwapchainExtent.height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = m_DepthFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		CheckVkResult(vkCreateImage(m_Device, &imageInfo, nullptr, &m_DepthImage));

		VkMemoryRequirements memReq;
		vkGetImageMemoryRequirements(m_Device, m_DepthImage, &memReq);

		// Find memory type
		VkPhysicalDeviceMemoryProperties memProps;
		vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);
		uint32_t memTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
			{
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) {
			NV_LOG_ERROR("VK_Swapchain: no suitable memory for depth image");
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		CheckVkResult(vkAllocateMemory(m_Device, &allocInfo, nullptr, &m_DepthImageMemory));
		vkBindImageMemory(m_Device, m_DepthImage, m_DepthImageMemory, 0);

		// Image view
		VkImageViewCreateInfo viewInfo{};
		viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		viewInfo.image = m_DepthImage;
		viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
		viewInfo.format = m_DepthFormat;
		viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
		viewInfo.subresourceRange.baseMipLevel = 0;
		viewInfo.subresourceRange.levelCount = 1;
		viewInfo.subresourceRange.baseArrayLayer = 0;
		viewInfo.subresourceRange.layerCount = 1;
		CheckVkResult(vkCreateImageView(m_Device, &viewInfo, nullptr, &m_DepthImageView));

		return true;
	}

	void VK_Swapchain::DestroyDepthResources() {
		if (m_DepthImageView != VK_NULL_HANDLE) { vkDestroyImageView(m_Device, m_DepthImageView, nullptr); m_DepthImageView = VK_NULL_HANDLE; }
		if (m_DepthImage != VK_NULL_HANDLE) { vkDestroyImage(m_Device, m_DepthImage, nullptr); m_DepthImage = VK_NULL_HANDLE; }
		if (m_DepthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Device, m_DepthImageMemory, nullptr); m_DepthImageMemory = VK_NULL_HANDLE; }
	}

} // namespace Nova::Core::Renderer::Backends::Vulkan

