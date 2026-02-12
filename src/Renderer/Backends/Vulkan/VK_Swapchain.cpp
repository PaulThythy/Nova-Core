#include "Renderer/Backends/Vulkan/VK_Swapchain.h"

#include <algorithm>
#include <limits>
#include <fstream>

#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"

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
		if (!CreateImageViews())              return false;
		if (!CreateBackBufferRenderPass())              return false;
		if (!CreateFramebuffers())            return false;

		if (!CreateCommandPoolAndBuffers())   return false;
		if (!CreateSyncObjects())             return false;

		CreateTrianglePipeline(); // optional

		if (!CreateImGuiDescriptorPool())     return false;

		NV_LOG_INFO("VK_Swapchain created successfully.");
		return true;
	}

	void VK_Swapchain::Destroy() {
		if (m_Device == VK_NULL_HANDLE)
			return;

		vkDeviceWaitIdle(m_Device);

		DestroyTrianglePipeline();

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

		if (!CreateSwapchain())        return false;
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
		// Create once. We keep it alive across swapchain recreations (format usually stays the same).
		if (m_BackBufferRenderPass != VK_NULL_HANDLE)
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

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
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

		VkResult res = vkCreateRenderPass(m_Device, &renderPassInfo, nullptr, &m_BackBufferRenderPass);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	bool VK_Swapchain::CreateFramebuffers() {
		for (auto& f : m_Frames) f.m_Framebuffer = VK_NULL_HANDLE;

		for (size_t i = 0; i < m_Frames.size(); i++) {
			VkImageView attachments[] = { m_Frames[i].m_ImageView };

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_BackBufferRenderPass;
			framebufferInfo.attachmentCount = 1;
			framebufferInfo.pAttachments = attachments;
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

	void VK_Swapchain::CreateTrianglePipeline() {
		// If already created, do nothing.
		if (m_TrianglePipeline != VK_NULL_HANDLE)
			return;

		std::filesystem::path p = std::filesystem::current_path();
		std::filesystem::path shaderDir = p / "Nova-Core" / "resources" / "engine" / "shaders";

		// You can change these paths to whatever your project uses.
		const std::filesystem::path vertPath = shaderDir / "program.vert";
    	const std::filesystem::path fragPath = shaderDir / "program.frag";

		Nova::Core::Renderer::RHI::RHI_ShaderCompileOptions compileOptions{};
		compileOptions.m_TargetApi = Nova::Core::GraphicsAPI::Vulkan;
		compileOptions.m_DebugInfo = true;
		compileOptions.m_Optimize = true;

		// Vertex
		Nova::Core::Renderer::RHI::RHI_ShaderDesc vertDesc{};
		vertDesc.m_FilePath = vertPath;
		vertDesc.m_Type = Nova::Core::Renderer::RHI::RHI_ShaderStage::Vertex;
		vertDesc.m_EntryPoint = "main";
		vertDesc.m_GlslVersion = 450;

		Nova::Core::Renderer::RHI::RHI_ShaderCompilationOutput vertOut{};
		if (!Nova::Core::Renderer::RHI::CompileShader(vertDesc, compileOptions, vertOut)) {
			NV_LOG_WARN(("Vertex shader compilation failed:\n" + vertOut.m_Log).c_str());
			return;
		}

		// Fragment
		Nova::Core::Renderer::RHI::RHI_ShaderDesc fragDesc{};
		fragDesc.m_FilePath = fragPath;
		fragDesc.m_Type = Nova::Core::Renderer::RHI::RHI_ShaderStage::Fragment;
		fragDesc.m_EntryPoint = "main";
		fragDesc.m_GlslVersion = 450;

		Nova::Core::Renderer::RHI::RHI_ShaderCompilationOutput fragOut{};
		if (!Nova::Core::Renderer::RHI::CompileShader(fragDesc, compileOptions, fragOut)) {
			NV_LOG_WARN(("Fragment shader compilation failed:\n" + fragOut.m_Log).c_str());
			return;
		}

		// Create Vulkan shader modules via VK_Shaders (Vulkan-only)
		Nova::Core::Renderer::Backends::Vulkan::VK_ShaderModule vertModule;
		Nova::Core::Renderer::Backends::Vulkan::VK_ShaderModule fragModule;

		if (!vertModule.Create(m_Device, vertOut.m_Spirv) || !fragModule.Create(m_Device, fragOut.m_Spirv)) {
			NV_LOG_WARN("Failed to create Vulkan shader modules from SPIR-V.");
			return;
		}

		VkPipelineShaderStageCreateInfo vertStage{};
		vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
		vertStage.module = vertModule.GetModule();
		vertStage.pName = "main";

		VkPipelineShaderStageCreateInfo fragStage{};
		fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		fragStage.module = fragModule.GetModule();
		fragStage.pName = "main";

		VkPipelineShaderStageCreateInfo stages[] = { vertStage, fragStage };

		// No vertex buffers (positions generated in vertex shader via gl_VertexIndex)
		VkPipelineVertexInputStateCreateInfo vertexInput{};
		vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;

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
		raster.frontFace = VK_FRONT_FACE_CLOCKWISE; // depends on your coordinate system; change if needed
		raster.lineWidth = 1.0f;

		VkPipelineMultisampleStateCreateInfo msaa{};
		msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
		msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

		VkPipelineColorBlendAttachmentState blendAttachment{};
		blendAttachment.colorWriteMask =
			VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

		VkPipelineColorBlendStateCreateInfo blend{};
		blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
		blend.attachmentCount = 1;
		blend.pAttachments = &blendAttachment;

		// TODO add VK_DYNAMIC_STATE_CULL_MODE to change cull mode dynamically (call vkCmdSetCullMode)
		// TODO add VK_DYNAMIC_STATE_POLYGON_MODE_EXT but only if VK_EXT_extended_dynamic_state3 extension is compatible
		VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
		VkPipelineDynamicStateCreateInfo dynamic{};
		dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
		dynamic.dynamicStateCount = 2;
		dynamic.pDynamicStates = dynamicStates;

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

		VkResult res = vkCreatePipelineLayout(m_Device, &layoutInfo, nullptr, &m_TrianglePipelineLayout);
		CheckVkResult(res);
		if (res != VK_SUCCESS) {
			NV_LOG_WARN("Failed to create pipeline layout. Rendering will only clear the screen.");
			vkDestroyShaderModule(m_Device, vertModule.GetModule(), nullptr);
			vkDestroyShaderModule(m_Device, fragModule.GetModule(), nullptr);
			return;
		}

		VkGraphicsPipelineCreateInfo pipe{};
		pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
		pipe.stageCount = 2;
		pipe.pStages = stages;
		pipe.pVertexInputState = &vertexInput;
		pipe.pInputAssemblyState = &inputAsm;
		pipe.pViewportState = &viewportState;
		pipe.pRasterizationState = &raster;
		pipe.pMultisampleState = &msaa;
		pipe.pColorBlendState = &blend;
		pipe.pDynamicState = &dynamic;
		pipe.layout = m_TrianglePipelineLayout;
		pipe.renderPass = m_BackBufferRenderPass;
		pipe.subpass = 0;

		res = vkCreateGraphicsPipelines(m_Device, VK_NULL_HANDLE, 1, &pipe, nullptr, &m_TrianglePipeline);
		CheckVkResult(res);

		if (res != VK_SUCCESS) {
			NV_LOG_WARN("Failed to create triangle pipeline. Rendering will only clear the screen.");
			DestroyTrianglePipeline();
		}
		else {
			NV_LOG_INFO("Triangle pipeline created.");
		}
	}

	void VK_Swapchain::DestroyTrianglePipeline() {
		if (m_TrianglePipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(m_Device, m_TrianglePipeline, nullptr);
			m_TrianglePipeline = VK_NULL_HANDLE;
		}
		if (m_TrianglePipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(m_Device, m_TrianglePipelineLayout, nullptr);
			m_TrianglePipelineLayout = VK_NULL_HANDLE;
		}
	}

} // namespace Nova::Core::Renderer::Backends::Vulkan

