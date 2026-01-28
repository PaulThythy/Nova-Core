#include "Renderer/Backends/Vulkan/VK_Swapchain.h"

#include <algorithm>
#include <limits>

#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	// ---------------------------------------------
	// Public API
	// ---------------------------------------------
	bool VK_Swapchain::Create(VkPhysicalDevice physicalDevice,
		VkDevice device,
		VkSurfaceKHR surface,
		VkQueue graphicsQueue,
		VkQueue presentQueue,
		uint32_t graphicsQueueFamily,
		uint32_t presentQueueFamily,
		VkExtent2D initialWindowExtent)
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

		m_WindowExtent = initialWindowExtent;

		if (!CreateSwapchain())
			return false;

		if (!CreateSyncObjects())
			return false;

		if (!CreateCommandPoolsAndBuffers())
			return false;

		NV_LOG_INFO("VK_Swapchain created successfully.");
		return true;
	}

	void VK_Swapchain::Destroy() {
		if (m_Device == VK_NULL_HANDLE)
			return;

		vkDeviceWaitIdle(m_Device);

		DestroyCommandPoolsAndBuffers();
		DestroySyncObjects();

		DestroySwapchain();

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
		if (m_Device == VK_NULL_HANDLE || m_Surface == VK_NULL_HANDLE) {
			NV_LOG_ERROR("VK_Swapchain::CreateSwapchain failed: device/surface invalid");
			return false;
		}

		SwapchainSupportDetails support = QuerySwapchainSupport(m_PhysicalDevice);
		if (support.formats.empty() || support.presentModes.empty()) {
			NV_LOG_ERROR("VK_Swapchain::CreateSwapchain failed: no formats or present modes");
			return false;
		}

		VkSurfaceFormatKHR surfaceFormat = ChooseSurfaceFormat(support.formats);
		VkPresentModeKHR presentMode = ChoosePresentMode(support.presentModes);
		VkExtent2D extent = ChooseExtent(support.capabilities, m_WindowExtent);

		// Image count (triple buffering conseillé, mais dépend de la surface)
		uint32_t imageCount = support.capabilities.minImageCount + 1;
		if (support.capabilities.maxImageCount > 0 && imageCount > support.capabilities.maxImageCount) {
			imageCount = support.capabilities.maxImageCount;
		}

		m_MinImageCount = imageCount;
		m_SwapchainExtent = extent;
		m_SwapchainImageFormat = surfaceFormat.format;

		VkSwapchainCreateInfoKHR createInfo{};
		createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
		createInfo.surface = m_Surface;
		createInfo.minImageCount = imageCount;
		createInfo.imageFormat = surfaceFormat.format;
		createInfo.imageColorSpace = surfaceFormat.colorSpace;
		createInfo.imageExtent = extent;
		createInfo.imageArrayLayers = 1;
		createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;

		uint32_t queueFamilyIndices[] = { m_GraphicsQueueFamily, m_PresentQueueFamily };

		if (m_GraphicsQueueFamily != m_PresentQueueFamily) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		}
		else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		createInfo.preTransform = support.capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = presentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VkResult res = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain);
		CheckVkResult(res);
		if (res != VK_SUCCESS) {
			NV_LOG_ERROR("vkCreateSwapchainKHR failed");
			return false;
		}

		// Récupération des images
		uint32_t swapImageCount = 0;
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &swapImageCount, nullptr);
		if (swapImageCount == 0) {
			NV_LOG_ERROR("Swapchain created but returned 0 images");
			return false;
		}

		std::vector<VkImage> images(swapImageCount);
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &swapImageCount, images.data());

		m_Frames.resize(swapImageCount);
		for (uint32_t i = 0; i < swapImageCount; i++) {
			m_Frames[i].m_VKImage = images[i];
		}

		// Track fence per image (init null => image not in-flight)
		m_ImagesInFlight.assign(swapImageCount, VK_NULL_HANDLE);

		// Image views
		for (uint32_t i = 0; i < swapImageCount; i++) {
			VkImageViewCreateInfo viewInfo{};
			viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			viewInfo.image = m_Frames[i].m_VKImage;
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			viewInfo.format = m_SwapchainImageFormat;
			viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
			viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			viewInfo.subresourceRange.baseMipLevel = 0;
			viewInfo.subresourceRange.levelCount = 1;
			viewInfo.subresourceRange.baseArrayLayer = 0;
			viewInfo.subresourceRange.layerCount = 1;

			VkResult vres = vkCreateImageView(m_Device, &viewInfo, nullptr, &m_Frames[i].m_VKImageView);
			CheckVkResult(vres);
			if (vres != VK_SUCCESS) {
				NV_LOG_ERROR("vkCreateImageView failed for swapchain image");
				return false;
			}
		}

		// RenderPass + Framebuffers
		if (!CreateRenderPass())
			return false;

		if (!CreateFramebuffers())
			return false;

		NV_LOG_INFO("Swapchain created.");
		return true;
	}

	bool VK_Swapchain::RecreateSwapchain(VkExtent2D newWindowExtent) {
		if (m_Device == VK_NULL_HANDLE)
			return false;

		// évite les extent invalides (fenêtre minimisée)
		if (newWindowExtent.width == 0 || newWindowExtent.height == 0) {
			NV_LOG_INFO("RecreateSwapchain skipped: window extent is 0 (minimized).");
			return false;
		}

		m_WindowExtent = newWindowExtent;
		m_FramebufferResized = false;

		vkDeviceWaitIdle(m_Device);

		DestroySwapchain();

		return CreateSwapchain();
	}

	void VK_Swapchain::DestroySwapchain() {
		if (m_Device == VK_NULL_HANDLE)
			return;

		DestroyFramebuffers();
		DestroyRenderPass();

		for (auto& f : m_Frames) {
			if (f.m_VKImageView != VK_NULL_HANDLE) {
				vkDestroyImageView(m_Device, f.m_VKImageView, nullptr);
				f.m_VKImageView = VK_NULL_HANDLE;
			}
			f.m_VKImage = VK_NULL_HANDLE;
		}

		m_Frames.clear();
		m_ImagesInFlight.clear();

		if (m_Swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}

		m_SwapchainImageFormat = {};
		m_SwapchainExtent = {};
		m_MinImageCount = 0;

		NV_LOG_INFO("Swapchain destroyed.");
	}

	bool VK_Swapchain::AcquireNextImage(uint32_t& outImageIndex) {
		const VK_FrameSync& sync = m_FrameSync[m_CurrentFrame];

		// Wait que le frame-in-flight soit libre
		vkWaitForFences(m_Device, 1, &sync.m_InFlightFence, VK_TRUE, UINT64_MAX);

		VkResult res = vkAcquireNextImageKHR(
			m_Device,
			m_Swapchain,
			UINT64_MAX,
			sync.m_ImageAvailableSemaphore,
			VK_NULL_HANDLE,
			&outImageIndex
		);

		if (res == VK_ERROR_OUT_OF_DATE_KHR) {
			return false;
		}
		if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR) {
			CheckVkResult(res);
			NV_LOG_ERROR("vkAcquireNextImageKHR failed");
			return false;
		}

		// Si cette image est déjà associée à une fence, il faut attendre
		if (m_ImagesInFlight[outImageIndex] != VK_NULL_HANDLE) {
			vkWaitForFences(m_Device, 1, &m_ImagesInFlight[outImageIndex], VK_TRUE, UINT64_MAX);
		}

		// On marque que cette image est utilisée par ce frame fence
		m_ImagesInFlight[outImageIndex] = sync.m_InFlightFence;

		vkResetFences(m_Device, 1, &sync.m_InFlightFence);

		m_CurrentImageIndex = outImageIndex;
		return true;
	}

	bool VK_Swapchain::Present(uint32_t imageIndex) {
		VkPresentInfoKHR presentInfo{};
		presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

		VkSemaphore waitSemaphores[] = { GetRenderFinishedSemaphore() };
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = waitSemaphores;

		VkSwapchainKHR swapchains[] = { m_Swapchain };
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = swapchains;
		presentInfo.pImageIndices = &imageIndex;

		VkResult res = vkQueuePresentKHR(m_PresentQueue, &presentInfo);

		// Advance frame
		m_CurrentFrame = (m_CurrentFrame + 1) % FRAMES_IN_FLIGHT;

		if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || m_FramebufferResized) {
			return false;
		}

		CheckVkResult(res);
		return res == VK_SUCCESS;
	}

	// ---------------------------------------------
	// Getters helpers
	// ---------------------------------------------
	VkFramebuffer VK_Swapchain::GetFramebuffer(uint32_t imageIndex) const {
		if (imageIndex >= m_Frames.size()) return VK_NULL_HANDLE;
		return m_Frames[imageIndex].m_VKFramebuffer;
	}

	VkSemaphore VK_Swapchain::GetImageAvailableSemaphore() const {
		return m_FrameSync[m_CurrentFrame].m_ImageAvailableSemaphore;
	}

	VkSemaphore VK_Swapchain::GetRenderFinishedSemaphore() const {
		return m_FrameSync[m_CurrentFrame].m_RenderFinishedSemaphore;
	}

	VkFence VK_Swapchain::GetInFlightFence() const {
		return m_FrameSync[m_CurrentFrame].m_InFlightFence;
	}

	VkCommandBuffer VK_Swapchain::GetPrimaryCommandBuffer(uint32_t frameIndex) const {
		if (frameIndex >= FRAMES_IN_FLIGHT) return VK_NULL_HANDLE;
		return m_PrimaryCommandBuffers[frameIndex];
	}

	VkCommandBuffer VK_Swapchain::GetSecondaryCommandBuffer(uint32_t frameIndex, uint32_t threadIndex) const {
		if (frameIndex >= FRAMES_IN_FLIGHT) return VK_NULL_HANDLE;
		if (threadIndex >= WORKER_THREAD_COUNT) return VK_NULL_HANDLE;
		return m_SecondaryCommandBuffers[frameIndex][threadIndex];
	}

	// ---------------------------------------------
	// Swapchain support + selection
	// ---------------------------------------------
	VK_Swapchain::SwapchainSupportDetails VK_Swapchain::QuerySwapchainSupport(VkPhysicalDevice physicalDevice) const {
		SwapchainSupportDetails details;

		vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, m_Surface, &details.capabilities);

		uint32_t formatCount = 0;
		vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, nullptr);
		if (formatCount != 0) {
			details.formats.resize(formatCount);
			vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, m_Surface, &formatCount, details.formats.data());
		}

		uint32_t presentModeCount = 0;
		vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, nullptr);
		if (presentModeCount != 0) {
			details.presentModes.resize(presentModeCount);
			vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, m_Surface, &presentModeCount, details.presentModes.data());
		}

		return details;
	}

	VkSurfaceFormatKHR VK_Swapchain::ChooseSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& formats) const {
		// Standard : SRGB BGRA8 si possible
		for (const auto& f : formats) {
			if (f.format == VK_FORMAT_B8G8R8A8_SRGB && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return f;
			}
		}
		return formats[0];
	}

	VkPresentModeKHR VK_Swapchain::ChoosePresentMode(const std::vector<VkPresentModeKHR>& presentModes) const {
		// MAILBOX = low latency + pas de tearing (si dispo)
		for (auto mode : presentModes) {
			if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
				return mode;
			}
		}
		// FIFO = garanti par Vulkan (vsync)
		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D VK_Swapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& caps, VkExtent2D windowExtent) const {
		if (caps.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return caps.currentExtent;
		}

		VkExtent2D actualExtent = windowExtent;
		actualExtent.width = std::clamp(actualExtent.width, caps.minImageExtent.width, caps.maxImageExtent.width);
		actualExtent.height = std::clamp(actualExtent.height, caps.minImageExtent.height, caps.maxImageExtent.height);
		return actualExtent;
	}

	// ---------------------------------------------
	// RenderPass + Framebuffers
	// ---------------------------------------------
	bool VK_Swapchain::CreateRenderPass() {
		if (m_RenderPass != VK_NULL_HANDLE)
			return true;

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_SwapchainImageFormat;
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentReference colorAttachmentRef{};
		colorAttachmentRef.attachment = 0;
		colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorAttachmentRef;

		// Dépendance externe -> subpass
		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = 1;
		renderPassInfo.pAttachments = &colorAttachment;
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VkResult res = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_RenderPass);
		CheckVkResult(res);
		return res == VK_SUCCESS;
	}

	void VK_Swapchain::DestroyRenderPass() {
		if (m_RenderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
			m_RenderPass = VK_NULL_HANDLE;
		}
	}

	bool VK_Swapchain::CreateFramebuffers() {
		for (auto& f : m_Frames) {
			VkImageView attachments[] = { f.m_VKImageView };

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_RenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
			framebufferInfo.width = m_SwapchainExtent.width;
			framebufferInfo.height = m_SwapchainExtent.height;
			framebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_Device, &framebufferInfo, nullptr, &f.m_VKFramebuffer);
			CheckVkResult(res);
			if (res != VK_SUCCESS) {
				NV_LOG_ERROR("vkCreateFramebuffer failed");
				return false;
			}
		}
		return true;
	}

	void VK_Swapchain::DestroyFramebuffers() {
		for (auto& f : m_Frames) {
			if (f.m_VKFramebuffer != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(m_Device, f.m_VKFramebuffer, nullptr);
				f.m_VKFramebuffer = VK_NULL_HANDLE;
			}
		}
	}

	// ---------------------------------------------
	// Sync objects
	// ---------------------------------------------
	bool VK_Swapchain::CreateSyncObjects() {
		VkSemaphoreCreateInfo semInfo{};
		semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

		VkFenceCreateInfo fenceInfo{};
		fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // au début : frame dispo

		for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
			VkResult r1 = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_FrameSync[i].m_ImageAvailableSemaphore);
			VkResult r2 = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_FrameSync[i].m_RenderFinishedSemaphore);
			VkResult r3 = vkCreateFence(m_Device, &fenceInfo, nullptr, &m_FrameSync[i].m_InFlightFence);

			CheckVkResult(r1);
			CheckVkResult(r2);
			CheckVkResult(r3);

			if (r1 != VK_SUCCESS || r2 != VK_SUCCESS || r3 != VK_SUCCESS) {
				NV_LOG_ERROR("Failed to create sync objects for swapchain");
				return false;
			}
		}
		return true;
	}

	void VK_Swapchain::DestroySyncObjects() {
		for (uint32_t i = 0; i < FRAMES_IN_FLIGHT; i++) {
			if (m_FrameSync[i].m_ImageAvailableSemaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(m_Device, m_FrameSync[i].m_ImageAvailableSemaphore, nullptr);
				m_FrameSync[i].m_ImageAvailableSemaphore = VK_NULL_HANDLE;
			}
			if (m_FrameSync[i].m_RenderFinishedSemaphore != VK_NULL_HANDLE) {
				vkDestroySemaphore(m_Device, m_FrameSync[i].m_RenderFinishedSemaphore, nullptr);
				m_FrameSync[i].m_RenderFinishedSemaphore = VK_NULL_HANDLE;
			}
			if (m_FrameSync[i].m_InFlightFence != VK_NULL_HANDLE) {
				vkDestroyFence(m_Device, m_FrameSync[i].m_InFlightFence, nullptr);
				m_FrameSync[i].m_InFlightFence = VK_NULL_HANDLE;
			}
		}
	}

	// ---------------------------------------------
	// Command pools + buffers (parallel recording)
	// ---------------------------------------------
	bool VK_Swapchain::CreateCommandPoolsAndBuffers() {
		// Primary pools + buffers
		for (uint32_t frame = 0; frame < FRAMES_IN_FLIGHT; frame++) {
			VkCommandPoolCreateInfo poolInfo{};
			poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
			poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
			poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

			VkResult res = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_PrimaryCommandPools[frame]);
			CheckVkResult(res);
			if (res != VK_SUCCESS) {
				NV_LOG_ERROR("Failed to create primary command pool");
				return false;
			}

			VkCommandBufferAllocateInfo allocInfo{};
			allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
			allocInfo.commandPool = m_PrimaryCommandPools[frame];
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;

			res = vkAllocateCommandBuffers(m_Device, &allocInfo, &m_PrimaryCommandBuffers[frame]);
			CheckVkResult(res);
			if (res != VK_SUCCESS) {
				NV_LOG_ERROR("Failed to allocate primary command buffer");
				return false;
			}
		}

		// Secondary pools + buffers (4 threads)
		for (uint32_t frame = 0; frame < FRAMES_IN_FLIGHT; frame++) {
			for (uint32_t t = 0; t < WORKER_THREAD_COUNT; t++) {
				VkCommandPoolCreateInfo poolInfo{};
				poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
				poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;
				poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT | VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;

				VkResult res = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_SecondaryCommandPools[frame][t]);
				CheckVkResult(res);
				if (res != VK_SUCCESS) {
					NV_LOG_ERROR("Failed to create secondary command pool");
					return false;
				}

				VkCommandBufferAllocateInfo allocInfo{};
				allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
				allocInfo.commandPool = m_SecondaryCommandPools[frame][t];
				allocInfo.level = VK_COMMAND_BUFFER_LEVEL_SECONDARY;
				allocInfo.commandBufferCount = 1;

				res = vkAllocateCommandBuffers(m_Device, &allocInfo, &m_SecondaryCommandBuffers[frame][t]);
				CheckVkResult(res);
				if (res != VK_SUCCESS) {
					NV_LOG_ERROR("Failed to allocate secondary command buffer");
					return false;
				}
			}
		}

		return true;
	}

	void VK_Swapchain::DestroyCommandPoolsAndBuffers() {
		// Secondary
		for (uint32_t frame = 0; frame < FRAMES_IN_FLIGHT; frame++) {
			for (uint32_t t = 0; t < WORKER_THREAD_COUNT; t++) {
				if (m_SecondaryCommandPools[frame][t] != VK_NULL_HANDLE) {
					vkDestroyCommandPool(m_Device, m_SecondaryCommandPools[frame][t], nullptr);
					m_SecondaryCommandPools[frame][t] = VK_NULL_HANDLE;
				}
				m_SecondaryCommandBuffers[frame][t] = VK_NULL_HANDLE;
			}
		}

		// Primary
		for (uint32_t frame = 0; frame < FRAMES_IN_FLIGHT; frame++) {
			if (m_PrimaryCommandPools[frame] != VK_NULL_HANDLE) {
				vkDestroyCommandPool(m_Device, m_PrimaryCommandPools[frame], nullptr);
				m_PrimaryCommandPools[frame] = VK_NULL_HANDLE;
			}
			m_PrimaryCommandBuffers[frame] = VK_NULL_HANDLE;
		}
	}

} // namespace Nova::Core::Renderer::Backends::Vulkan

