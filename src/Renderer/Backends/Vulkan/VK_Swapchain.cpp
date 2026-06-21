#include "Renderer/Backends/Vulkan/VK_Swapchain.h"

#include <algorithm>
#include <limits>
#include <string>

#include <SDL3/SDL.h>

#include "Core/Application.h"
#include "Core/Assert.h"
#include "Core/Log.h"
#include "Core/Window.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	const char* PresentModeName(VkPresentModeKHR mode) {
		switch (mode) {
			case VK_PRESENT_MODE_IMMEDIATE_KHR: return "IMMEDIATE";
			case VK_PRESENT_MODE_MAILBOX_KHR:   return "MAILBOX";
			case VK_PRESENT_MODE_FIFO_KHR:      return "FIFO";
			case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO_RELAXED";
			default: return "UNKNOWN";
		}
	}

	const char* BufferingLabel(uint32_t framesInFlight) {
		switch (framesInFlight) {
			case 1: return "single";
			case 2: return "double";
			case 3: return "triple";
			default: return "custom";
		}
	}

	uint32_t ClampFramesInFlight(uint32_t value) {
		return std::clamp(value, 1u, 3u);
	}

	VK_Swapchain::VK_SwapchainSupportDetails VK_Swapchain::QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) const {
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

	VkSurfaceFormatKHR VK_Swapchain::ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) const {
		NV_ASSERT_MSG(!availableFormats.empty(), "No Vulkan surface formats are available for the swapchain.");
		if (availableFormats.empty()) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		if (availableFormats.size() == 1 && availableFormats[0].format == VK_FORMAT_UNDEFINED) {
			return { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
		}

		for (const auto& fmt : availableFormats) {
			if (fmt.format == VK_FORMAT_B8G8R8A8_SRGB &&
				fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return fmt;
			}
		}

		for (const auto& fmt : availableFormats) {
			if (fmt.format == VK_FORMAT_B8G8R8A8_UNORM &&
				fmt.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				return fmt;
			}
		}

		return availableFormats[0];
	}

	VkPresentModeKHR VK_Swapchain::ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) const {
		const auto hasMode = [&](VkPresentModeKHR mode) {
			return std::find(availablePresentModes.begin(), availablePresentModes.end(), mode)
				!= availablePresentModes.end();
		};

		switch (m_Desc.m_PreferredPresentMode) {
		case RHI::RHI_PresentMode::Immediate:
			if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR))
				return VK_PRESENT_MODE_IMMEDIATE_KHR;
			break;
		case RHI::RHI_PresentMode::Default:
			return VK_PRESENT_MODE_FIFO_KHR;
		case RHI::RHI_PresentMode::LowLatency:
		default:
			if (hasMode(VK_PRESENT_MODE_MAILBOX_KHR))
				return VK_PRESENT_MODE_MAILBOX_KHR;
			if (hasMode(VK_PRESENT_MODE_IMMEDIATE_KHR))
				return VK_PRESENT_MODE_IMMEDIATE_KHR;
			break;
		}

		return VK_PRESENT_MODE_FIFO_KHR;
	}

	VkExtent2D VK_Swapchain::ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const {
		if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return capabilities.currentExtent;
		}

		uint32_t width = m_Desc.m_Width;
		uint32_t height = m_Desc.m_Height;

		if (width == 0 || height == 0) {
			SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
			int w = 0, h = 0;
			SDL_GetWindowSizeInPixels(window, &w, &h);
			width = static_cast<uint32_t>(std::max(1, w));
			height = static_cast<uint32_t>(std::max(1, h));
		}

		VkExtent2D actualExtent{ width, height };
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

	void VK_Swapchain::LogSwapchainConfiguration(uint32_t swapchainImageCount) const {
		NV_LOG_INFO("---- Swapchain Configuration ----");
		NV_LOG_INFO((std::string("  Frames in flight: ") + std::to_string(m_FramesInFlight) +
			" (" + BufferingLabel(m_FramesInFlight) + " buffering)").c_str());
		NV_LOG_INFO((std::string("  Swapchain images: ") + std::to_string(swapchainImageCount)).c_str());
		NV_LOG_INFO((std::string("  Present mode: ") + PresentModeName(m_PresentMode) +
			" (preferred=" + std::to_string(static_cast<int>(m_Desc.m_PreferredPresentMode)) + ")").c_str());
		NV_LOG_INFO((std::string("  Image format: ") + std::to_string(static_cast<uint32_t>(m_SwapchainImageFormat))).c_str());
		NV_LOG_INFO((std::string("  Extent: ") + std::to_string(m_SwapchainExtent.width) +
			"x" + std::to_string(m_SwapchainExtent.height)).c_str());

		if (m_PresentMode == VK_PRESENT_MODE_MAILBOX_KHR && m_FramesInFlight >= 2) {
			NV_LOG_INFO("  Latency hint: MAILBOX + multi-frame flight typically reduces input latency vs FIFO-only.");
		} else if (m_PresentMode == VK_PRESENT_MODE_FIFO_KHR) {
			NV_LOG_INFO("  Latency hint: FIFO is VSync-limited; increase frames in flight to reduce CPU/GPU stalls, not display latency.");
		}

		NV_LOG_INFO("----------------------------------");
	}

	bool VK_Swapchain::Create(VkPhysicalDevice physicalDevice,
		VkDevice device,
		VkSurfaceKHR surface,
		VkQueue graphicsQueue,
		VkQueue presentQueue,
		uint32_t graphicsQueueFamily,
		uint32_t presentQueueFamily,
		const RHI::RHI_SwapchainDesc& desc)
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
		m_Desc = desc;
		m_FramesInFlight = ClampFramesInFlight(desc.m_FramesInFlight);

		if (!CreateSwapchain())               return false;
		if (!CreateImageViews())              return false;
		if (!CreateCommandPoolAndBuffers())   return false;
		if (!CreateSyncObjects())             return false;

		NV_LOG_INFO("VK_Swapchain created successfully.");
		return true;
	}

	void VK_Swapchain::Destroy() {
		if (m_Device == VK_NULL_HANDLE)
			return;

		vkDeviceWaitIdle(m_Device);

		DestroySwapchain();
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
		m_Desc = {};
		m_FramesInFlight = 3;
		m_CurrentFrame = 0;
		m_AcquiredImage = 0;

		NV_LOG_INFO("VK_Swapchain destroyed.");
	}

	bool VK_Swapchain::CreateSwapchain() {
		const auto swapChainSupport = QuerySwapChainSupport(m_PhysicalDevice, m_Surface);

		const VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.m_Formats);
		m_PresentMode = ChooseSwapPresentMode(swapChainSupport.m_PresentModes);
		const VkExtent2D extent = ChooseSwapExtent(swapChainSupport.m_Capabilities);

		m_SwapchainExtent = extent;
		m_SwapchainImageFormat = surfaceFormat.format;

		uint32_t imageCount = std::max(
			swapChainSupport.m_Capabilities.minImageCount,
			m_FramesInFlight
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

		const uint32_t queueFamilyIndices[] = { m_GraphicsQueueFamily, m_PresentQueueFamily };
		if (queueFamilyIndices[0] != queueFamilyIndices[1]) {
			createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
			createInfo.queueFamilyIndexCount = 2;
			createInfo.pQueueFamilyIndices = queueFamilyIndices;
		} else {
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
		}

		createInfo.preTransform = swapChainSupport.m_Capabilities.currentTransform;
		createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
		createInfo.presentMode = m_PresentMode;
		createInfo.clipped = VK_TRUE;
		createInfo.oldSwapchain = VK_NULL_HANDLE;

		VkResult res = vkCreateSwapchainKHR(m_Device, &createInfo, nullptr, &m_Swapchain);
		CheckVkResult(res);
		if (res != VK_SUCCESS) return false;

		uint32_t actualImageCount = 0;
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualImageCount, nullptr);

		std::vector<VkImage> images(actualImageCount);
		vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &actualImageCount, images.data());

		m_Images.clear();
		m_Images.resize(actualImageCount);
		for (uint32_t i = 0; i < actualImageCount; ++i) {
			m_Images[i].m_Image = images[i];
		}

		m_ImagesInFlight.assign(m_Images.size(), VK_NULL_HANDLE);

		//LogSwapchainConfiguration(actualImageCount);
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

		if (!CreateSwapchain())        return false;
		if (!CreateImageViews())       return false;
		if (!RecreateCommandBuffers()) return false;

		m_RenderFinishedSemaphores.resize(m_Images.size());
		VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i) {
			if (m_RenderFinishedSemaphores[i] == VK_NULL_HANDLE) {
				VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinishedSemaphores[i]);
				CheckVkResult(res);
				if (res != VK_SUCCESS) return false;
			}
		}

		m_ImagesInFlight.assign(m_Images.size(), VK_NULL_HANDLE);
		NV_LOG_INFO("VK_Swapchain recreated after resize.");
		return true;
	}

	void VK_Swapchain::DestroySwapchain() {
		for (auto& image : m_Images) {
			if (image.m_ImageView) {
				vkDestroyImageView(m_Device, image.m_ImageView, nullptr);
				image.m_ImageView = VK_NULL_HANDLE;
			}
			image.m_Image = VK_NULL_HANDLE;
		}
		m_Images.clear();

		if (!m_CommandBuffers.empty() && m_CommandPool != VK_NULL_HANDLE) {
			vkFreeCommandBuffers(m_Device, m_CommandPool,
				static_cast<uint32_t>(m_CommandBuffers.size()), m_CommandBuffers.data());
			m_CommandBuffers.clear();
		}

		for (auto& sem : m_RenderFinishedSemaphores) {
			if (sem) {
				vkDestroySemaphore(m_Device, sem, nullptr);
				sem = VK_NULL_HANDLE;
			}
		}
		m_RenderFinishedSemaphores.clear();

		if (m_Swapchain != VK_NULL_HANDLE) {
			vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
			m_Swapchain = VK_NULL_HANDLE;
		}

		m_ImagesInFlight.clear();
		m_CurrentFrame = 0;
	}

	bool VK_Swapchain::CreateImageViews() {
		for (auto& image : m_Images)
			image.m_ImageView = VK_NULL_HANDLE;

		for (size_t i = 0; i < m_Images.size(); ++i) {
			VkImageViewCreateInfo createInfo{};
			createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
			createInfo.image = m_Images[i].m_Image;
			createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			createInfo.format = m_SwapchainImageFormat;
			createInfo.components = {
				VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
				VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
			};
			createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			createInfo.subresourceRange.baseMipLevel = 0;
			createInfo.subresourceRange.levelCount = 1;
			createInfo.subresourceRange.baseArrayLayer = 0;
			createInfo.subresourceRange.layerCount = 1;

			VkResult res = vkCreateImageView(m_Device, &createInfo, nullptr, &m_Images[i].m_ImageView);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}
		return true;
	}

	bool VK_Swapchain::CreateSyncObjects() {
		VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
		VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
		fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

		m_FrameSync.clear();
		m_FrameSync.resize(m_FramesInFlight);
		for (uint32_t i = 0; i < m_FramesInFlight; ++i) {
			VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_FrameSync[i].m_ImageAvailableSemaphore);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;

			res = vkCreateFence(m_Device, &fenceInfo, nullptr, &m_FrameSync[i].m_InFlightFence);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}

		m_RenderFinishedSemaphores.resize(m_Images.size());
		for (size_t i = 0; i < m_RenderFinishedSemaphores.size(); ++i) {
			VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinishedSemaphores[i]);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}

		m_ImagesInFlight.assign(m_Images.size(), VK_NULL_HANDLE);
		m_CurrentFrame = 0;
		return true;
	}

	void VK_Swapchain::DestroySyncObjects() {
		for (auto& sync : m_FrameSync) {
			if (sync.m_ImageAvailableSemaphore) {
				vkDestroySemaphore(m_Device, sync.m_ImageAvailableSemaphore, nullptr);
				sync.m_ImageAvailableSemaphore = VK_NULL_HANDLE;
			}
			if (sync.m_InFlightFence) {
				vkDestroyFence(m_Device, sync.m_InFlightFence, nullptr);
				sync.m_InFlightFence = VK_NULL_HANDLE;
			}
		}
		m_FrameSync.clear();

		for (auto& sem : m_RenderFinishedSemaphores) {
			if (sem) {
				vkDestroySemaphore(m_Device, sem, nullptr);
				sem = VK_NULL_HANDLE;
			}
		}
		m_RenderFinishedSemaphores.clear();
		m_ImagesInFlight.clear();
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

		m_CommandBuffers.resize(m_Images.size());

		VkCommandBufferAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
		allocInfo.commandPool = m_CommandPool;
		allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		allocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

		VkResult res = vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data());
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

} // namespace Nova::Core::Renderer::Backends::Vulkan