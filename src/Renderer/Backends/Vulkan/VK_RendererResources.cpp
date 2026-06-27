#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>
#include <vector>

#include "Asset/AssetManager.h"
#include "Asset/Assets/ShaderAsset.h"
#include "Core/Application.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Graphics/Vertex.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

#include "backends/imgui_impl_vulkan.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	VkShaderStageFlags ToVkStageFlags(RHI::RHI_ShaderStageMask mask) {
		VkShaderStageFlags out = 0;
		const uint32_t m = static_cast<uint32_t>(mask);
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Vertex)) out |= VK_SHADER_STAGE_VERTEX_BIT;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Fragment)) out |= VK_SHADER_STAGE_FRAGMENT_BIT;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Geometry)) out |= VK_SHADER_STAGE_GEOMETRY_BIT;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::TessCtrl)) out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::TessEval)) out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Compute)) out |= VK_SHADER_STAGE_COMPUTE_BIT;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::RayGen)) out |= VK_SHADER_STAGE_RAYGEN_BIT_KHR;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::RayMiss)) out |= VK_SHADER_STAGE_MISS_BIT_KHR;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::RayCHit)) out |= VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::RayAHit)) out |= VK_SHADER_STAGE_ANY_HIT_BIT_KHR;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::RayISect)) out |= VK_SHADER_STAGE_INTERSECTION_BIT_KHR;
		if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::RayCall)) out |= VK_SHADER_STAGE_CALLABLE_BIT_KHR;
		return out;
	}

	VkDescriptorType ToVkDescriptorType(const RHI::RHI_BindingInfo& b) {
		using RK = RHI::RHI_ResourceKind;
		switch (b.m_Kind) {
			case RK::ConstantBuffer: return b.m_IsDynamicUniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case RK::StorageBuffer:  return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case RK::Texture:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case RK::Sampler:        return VK_DESCRIPTOR_TYPE_SAMPLER;
			case RK::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
			case RK::RWTexture:      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
			case RK::RWBuffer:       return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			default:                 return VK_DESCRIPTOR_TYPE_MAX_ENUM;
		}
	}

	void MarkEngineDynamicBuffers(RHI::RHI_ProgramReflection& refl) {
		const char* dynamicNames[] = { RHI::EngineResourceName::Mvp, RHI::EngineResourceName::Material };
		for (const char* name : dynamicNames) {
			const RHI::RHI_BindingKey* key = refl.FindBindingKeyByName(name);
			if (!key) continue;
			if (auto* set = const_cast<RHI::RHI_DescriptorSetLayoutInfo*>(refl.FindSet(key->m_Set))) {
				for (auto& b : set->m_Bindings) {
					if (b.m_Key.m_Binding == key->m_Binding && b.m_Kind == RHI::RHI_ResourceKind::ConstantBuffer)
						b.m_IsDynamicUniformBuffer = true;
				}
			}
		}
	}

	bool CreateDescriptorSetLayoutFromReflection(
		VkDevice device,
		const RHI::RHI_ProgramReflection& refl,
		uint32_t setIndex,
		VkDescriptorSetLayout& outLayout)
	{
		outLayout = VK_NULL_HANDLE;
		const auto* set = refl.FindSet(setIndex);
		if (!set || set->m_Bindings.empty()) return false;

		std::vector<VkDescriptorSetLayoutBinding> bindings;
		bindings.reserve(set->m_Bindings.size());
		for (const auto& b : set->m_Bindings) {
			VkDescriptorType type = ToVkDescriptorType(b);
			if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) continue;

			VkDescriptorSetLayoutBinding vkB{};
			vkB.binding = b.m_Key.m_Binding;
			vkB.descriptorType = type;
			vkB.descriptorCount = (b.m_ArrayCount == 0) ? 1u : b.m_ArrayCount;
			vkB.stageFlags = ToVkStageFlags(b.m_Stages);
			bindings.push_back(vkB);
		}

		if (bindings.empty()) return false;

		VkDescriptorSetLayoutCreateInfo info{};
		info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
		info.bindingCount = static_cast<uint32_t>(bindings.size());
		info.pBindings = bindings.data();

		const VkResult res = vkCreateDescriptorSetLayout(device, &info, nullptr, &outLayout);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	bool VK_Renderer::InitRenderResources() {
		if (m_RenderResourcesInitialized)
			return true;

		if (m_VKSwapchain.GetSwapchain() == VK_NULL_HANDLE) {
			NV_LOG_ERROR("VK_Renderer::InitRenderResources - swapchain is not initialized");
			return false;
		}

		if (!CreateImGuiDescriptorPool()) return false;
		if (!CreateDepthResources()) return false;
		if (!CreateBackBufferRenderPass()) return false;
		if (!CreateViewportRenderPass()) return false;
		if (!CreateSwapchainFramebuffers()) return false;
		CreateModelPipeline();

		ImGui_ImplVulkan_InitInfo initInfo{};
		initInfo.ApiVersion = VK_API_VERSION_1_3;
		initInfo.Instance = m_VKInstance.GetInstance();
		initInfo.PhysicalDevice = m_VKDevice.GetPhysicalDevice();
		initInfo.Device = m_VKDevice.GetDevice();
		initInfo.QueueFamily = m_VKDevice.GetGraphicsQueueFamily();
		initInfo.Queue = m_VKDevice.GetGraphicsQueue();
		initInfo.DescriptorPool = m_ImGuiDescriptorPool;
		initInfo.DescriptorPoolSize = 0;
		initInfo.MinImageCount = m_VKSwapchain.GetImageCount();
		initInfo.ImageCount = m_VKSwapchain.GetImageCount();
		initInfo.PipelineCache = VK_NULL_HANDLE;
		initInfo.PipelineInfoMain.RenderPass = m_BackBufferRenderPass;
		initInfo.PipelineInfoMain.Subpass = 0;
		initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
		initInfo.PipelineInfoForViewports = initInfo.PipelineInfoMain;
		initInfo.UseDynamicRendering = false;
		initInfo.Allocator = nullptr;
		initInfo.CheckVkResultFn = CheckVkResult;
		initInfo.MinAllocationSize = 0;

		auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
		imguiLayer.SetVulkanInitInfo(initInfo);
		imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
		imguiLayer.SetVulkanBeforeRenderCallback({});

		m_Shader = std::make_unique<VK_Shaders>();
		m_Shader->SetPipeline(m_ModelPipeline, m_ModelPipelineLayout);
		m_Shader->SetSceneBuffers(
			m_VKDevice.GetDevice(),
			m_BufGlobals, m_BufGlobalsMemory,
			m_BufMvp, m_BufMvpMemory, m_MvpDynamicStride,
			m_BufMaterials, m_BufMaterialsMemory, m_MaterialDynamicStride,
			m_BufInstances, m_BufInstancesMemory, m_BufInstancesSize,
			m_DescriptorSets
		);
		m_Shader->SetReflection(m_ModelPipelineReflection);

		CreateFullscreenQuadBuffer();

		m_RenderResourcesInitialized = true;
		NV_LOG_INFO("VK_Renderer render resources initialized.");
		return true;
	}

	void VK_Renderer::DestroyRenderResources() {
		if (!m_RenderResourcesInitialized && m_ImGuiDescriptorPool == VK_NULL_HANDLE &&
			m_BackBufferRenderPass == VK_NULL_HANDLE && m_ModelPipeline == VK_NULL_HANDLE) {
			return;
		}

		DestroyFullscreenQuadBuffer();
		m_Shader.reset();
		DestroyModelPipeline();
		DestroySwapchainFramebuffers();
		DestroyViewportRenderPass();
		DestroyBackBufferRenderPass();
		DestroyDepthResources();
		DestroyImGuiDescriptorPool();
		m_RenderResourcesInitialized = false;
	}

	bool VK_Renderer::RecreateSwapchainRenderTargets() {
		DestroySwapchainFramebuffers();
		DestroyDepthResources();
		if (!CreateDepthResources()) return false;
		if (!CreateSwapchainFramebuffers()) return false;
		return true;
	}

	bool VK_Renderer::CreateBackBufferRenderPass() {
		if (m_BackBufferRenderPass != VK_NULL_HANDLE)
			return true;

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_VKSwapchain.GetImageFormat();
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

		VkAttachmentDescription depthAttachment{};
		depthAttachment.format = m_DepthFormat;
		depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VkResult res = vkCreateRenderPass(m_VKDevice.GetDevice(), &renderPassInfo, nullptr, &m_BackBufferRenderPass);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Renderer::DestroyBackBufferRenderPass() {
		if (m_BackBufferRenderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(m_VKDevice.GetDevice(), m_BackBufferRenderPass, nullptr);
			m_BackBufferRenderPass = VK_NULL_HANDLE;
		}
	}

	bool VK_Renderer::CreateViewportRenderPass() {
		if (m_ViewportRenderPass != VK_NULL_HANDLE)
			return true;

		VkAttachmentDescription colorAttachment{};
		colorAttachment.format = m_VKSwapchain.GetImageFormat();
		colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
		colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
		colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
		colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
		colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
		colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
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

		VkAttachmentReference colorRef{ 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
		VkAttachmentReference depthRef{ 1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };

		VkSubpassDescription subpass{};
		subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		subpass.colorAttachmentCount = 1;
		subpass.pColorAttachments = &colorRef;
		subpass.pDepthStencilAttachment = &depthRef;

		VkSubpassDependency dependency{};
		dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
		dependency.dstSubpass = 0;
		dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.srcAccessMask = 0;
		dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
		dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

		std::array<VkAttachmentDescription, 2> attachments = { colorAttachment, depthAttachment };
		VkRenderPassCreateInfo renderPassInfo{};
		renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
		renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
		renderPassInfo.pAttachments = attachments.data();
		renderPassInfo.subpassCount = 1;
		renderPassInfo.pSubpasses = &subpass;
		renderPassInfo.dependencyCount = 1;
		renderPassInfo.pDependencies = &dependency;

		VkResult res = vkCreateRenderPass(m_VKDevice.GetDevice(), &renderPassInfo, nullptr, &m_ViewportRenderPass);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Renderer::DestroyViewportRenderPass() {
		if (m_ViewportRenderPass != VK_NULL_HANDLE) {
			vkDestroyRenderPass(m_VKDevice.GetDevice(), m_ViewportRenderPass, nullptr);
			m_ViewportRenderPass = VK_NULL_HANDLE;
		}
	}

	bool VK_Renderer::CreateSwapchainFramebuffers() {
		const auto& images = m_VKSwapchain.GetImages();
		m_SwapchainFramebuffers.assign(images.size(), VK_NULL_HANDLE);

		for (size_t i = 0; i < images.size(); ++i) {
			std::array<VkImageView, 2> attachments = {
				images[i].m_ImageView,
				m_DepthImageView
			};

			VkFramebufferCreateInfo framebufferInfo{};
			framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
			framebufferInfo.renderPass = m_BackBufferRenderPass;
			framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
			framebufferInfo.pAttachments = attachments.data();
			framebufferInfo.width = m_VKSwapchain.GetExtent().width;
			framebufferInfo.height = m_VKSwapchain.GetExtent().height;
			framebufferInfo.layers = 1;

			VkResult res = vkCreateFramebuffer(m_VKDevice.GetDevice(), &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]);
			CheckVkResult(res);
			if (res != VK_SUCCESS) return false;
		}
		return true;
	}

	void VK_Renderer::DestroySwapchainFramebuffers() {
		for (auto& fb : m_SwapchainFramebuffers) {
			if (fb != VK_NULL_HANDLE) {
				vkDestroyFramebuffer(m_VKDevice.GetDevice(), fb, nullptr);
				fb = VK_NULL_HANDLE;
			}
		}
		m_SwapchainFramebuffers.clear();
	}

	bool VK_Renderer::CreateImGuiDescriptorPool() {
		std::array<VkDescriptorPoolSize, 11> poolSizes = {
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

		VkDescriptorPoolCreateInfo poolInfo{};
		poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
		poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
		poolInfo.maxSets = 1000 * static_cast<uint32_t>(poolSizes.size());
		poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
		poolInfo.pPoolSizes = poolSizes.data();

		VkResult res = vkCreateDescriptorPool(m_VKDevice.GetDevice(), &poolInfo, nullptr, &m_ImGuiDescriptorPool);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Renderer::DestroyImGuiDescriptorPool() {
		if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
			vkDestroyDescriptorPool(m_VKDevice.GetDevice(), m_ImGuiDescriptorPool, nullptr);
			m_ImGuiDescriptorPool = VK_NULL_HANDLE;
		}
	}

	bool VK_Renderer::CreateDepthResources() {
		const std::vector<VkFormat> candidates = {
			VK_FORMAT_D32_SFLOAT,
			VK_FORMAT_D32_SFLOAT_S8_UINT,
			VK_FORMAT_D24_UNORM_S8_UINT
		};
		m_DepthFormat = VK_FORMAT_UNDEFINED;

		for (VkFormat format : candidates) {
			VkFormatProperties props{};
			vkGetPhysicalDeviceFormatProperties(m_VKDevice.GetPhysicalDevice(), format, &props);
			if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
				m_DepthFormat = format;
				break;
			}
		}

		if (m_DepthFormat == VK_FORMAT_UNDEFINED) {
			NV_LOG_ERROR("VK_Renderer::CreateDepthResources - no supported depth format found");
			return false;
		}

		VkImageCreateInfo imageInfo{};
		imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
		imageInfo.imageType = VK_IMAGE_TYPE_2D;
		imageInfo.extent.width = m_VKSwapchain.GetExtent().width;
		imageInfo.extent.height = m_VKSwapchain.GetExtent().height;
		imageInfo.extent.depth = 1;
		imageInfo.mipLevels = 1;
		imageInfo.arrayLayers = 1;
		imageInfo.format = m_DepthFormat;
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
		imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

		VkResult res = vkCreateImage(m_VKDevice.GetDevice(), &imageInfo, nullptr, &m_DepthImage);
		CheckVkResult(res);
		if (res != VK_SUCCESS) return false;

		VkMemoryRequirements memReq{};
		vkGetImageMemoryRequirements(m_VKDevice.GetDevice(), m_DepthImage, &memReq);

		uint32_t memTypeIndex = UINT32_MAX;
		const auto& memProps = m_VKDevice.GetMemoryProperties();
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) {
			NV_LOG_ERROR("VK_Renderer::CreateDepthResources - no suitable memory for depth image");
			vkDestroyImage(m_VKDevice.GetDevice(), m_DepthImage, nullptr);
			m_DepthImage = VK_NULL_HANDLE;
			return false;
		}

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		res = vkAllocateMemory(m_VKDevice.GetDevice(), &allocInfo, nullptr, &m_DepthImageMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) return false;
		vkBindImageMemory(m_VKDevice.GetDevice(), m_DepthImage, m_DepthImageMemory, 0);

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

		res = vkCreateImageView(m_VKDevice.GetDevice(), &viewInfo, nullptr, &m_DepthImageView);
		CheckVkResult(res);
		return (res == VK_SUCCESS);
	}

	void VK_Renderer::DestroyDepthResources() {
		if (m_DepthImageView != VK_NULL_HANDLE) {
			vkDestroyImageView(m_VKDevice.GetDevice(), m_DepthImageView, nullptr);
			m_DepthImageView = VK_NULL_HANDLE;
		}
		if (m_DepthImage != VK_NULL_HANDLE) {
			vkDestroyImage(m_VKDevice.GetDevice(), m_DepthImage, nullptr);
			m_DepthImage = VK_NULL_HANDLE;
		}
		if (m_DepthImageMemory != VK_NULL_HANDLE) {
			vkFreeMemory(m_VKDevice.GetDevice(), m_DepthImageMemory, nullptr);
			m_DepthImageMemory = VK_NULL_HANDLE;
		}
	}

	void VK_Renderer::CreateModelPipeline() {
		if (m_ModelPipeline != VK_NULL_HANDLE) return;

		const std::filesystem::path shaderDir = std::filesystem::current_path()
			/ "Nova-Core" / "Resources" / "Engine" / "Shaders";

		using Nova::Core::Asset::AssetManager;
		using Nova::Core::Asset::Assets::ShaderAsset;
		auto vertAsset = AssetManager::Get().Acquire<ShaderAsset>(shaderDir / "Scene.vert.slang");
		auto fragAsset = AssetManager::Get().Acquire<ShaderAsset>(shaderDir / "Scene.frag.slang");

		if (!vertAsset || !fragAsset) { NV_LOG_WARN("CreateModelPipeline: failed to acquire shaders"); return; }
		if (!vertAsset->Compile()) { NV_LOG_WARN(("VS compile failed:\n" + vertAsset->GetLastLog()).c_str()); return; }
		if (!fragAsset->Compile()) { NV_LOG_WARN(("FS compile failed:\n" + fragAsset->GetLastLog()).c_str()); return; }

		VK_ShaderModule vertModule, fragModule;
		if (!vertModule.Create(m_VKDevice.GetDevice(), vertAsset->GetBinary()) ||
			!fragModule.Create(m_VKDevice.GetDevice(), fragAsset->GetBinary()))
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

		{
			RHI::RHI_ProgramReflection reflForVk =
				RHI::MergeProgramReflections({ vertAsset->GetReflection(), fragAsset->GetReflection() });
			MarkEngineDynamicBuffers(reflForVk);
			m_ModelPipelineReflection = reflForVk;

			m_SetLayouts.clear();
			for (const auto& setInfo : reflForVk.m_Sets) {
				VkDescriptorSetLayout layout = VK_NULL_HANDLE;
				if (CreateDescriptorSetLayoutFromReflection(m_VKDevice.GetDevice(), reflForVk, setInfo.m_Set, layout)) {
					m_SetLayouts.emplace_back(setInfo.m_Set, layout);
				}
			}
			std::sort(m_SetLayouts.begin(), m_SetLayouts.end(),
				[](const auto& a, const auto& b) { return a.first < b.first; });

			if (m_SetLayouts.empty()) {
				NV_LOG_WARN("CreateModelPipeline: reflection produced no descriptor sets");
				return;
			}
		}

		const VkDeviceSize globalsSize = sizeof(Renderer::RHI::FrameUniforms);
		VkBufferCreateInfo bufInfo{};
		bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
		bufInfo.size = globalsSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
		bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
		VkResult res = vkCreateBuffer(m_VKDevice.GetDevice(), &bufInfo, nullptr, &m_BufGlobals);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }

		VkMemoryRequirements memReq{};
		vkGetBufferMemoryRequirements(m_VKDevice.GetDevice(), m_BufGlobals, &memReq);
		const auto& memProps = m_VKDevice.GetMemoryProperties();
		uint32_t memTypeIndex = UINT32_MAX;
		for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
			if ((memReq.memoryTypeBits & (1u << i)) &&
				(memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
				memTypeIndex = i;
				break;
			}
		}
		if (memTypeIndex == UINT32_MAX) { DestroyModelPipeline(); return; }

		VkMemoryAllocateInfo allocInfo{};
		allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
		allocInfo.allocationSize = memReq.size;
		allocInfo.memoryTypeIndex = memTypeIndex;
		res = vkAllocateMemory(m_VKDevice.GetDevice(), &allocInfo, nullptr, &m_BufGlobalsMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_VKDevice.GetDevice(), m_BufGlobals, m_BufGlobalsMemory, 0);

		const VkDeviceSize mvpSize = sizeof(Renderer::RHI::MVP);
		VkPhysicalDeviceProperties physProps{};
		vkGetPhysicalDeviceProperties(m_VKDevice.GetPhysicalDevice(), &physProps);
		auto alignUp = [](VkDeviceSize v, VkDeviceSize a) -> VkDeviceSize {
			return (a > 0) ? ((v + a - 1) / a) * a : v;
		};
		m_MvpDynamicStride = alignUp(mvpSize, physProps.limits.minUniformBufferOffsetAlignment);
		const VkDeviceSize mvpBufferSize = m_MvpDynamicStride * static_cast<VkDeviceSize>(MAX_MODEL_DRAWS);

		bufInfo.size = mvpBufferSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		res = vkCreateBuffer(m_VKDevice.GetDevice(), &bufInfo, nullptr, &m_BufMvp);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkGetBufferMemoryRequirements(m_VKDevice.GetDevice(), m_BufMvp, &memReq);
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
		res = vkAllocateMemory(m_VKDevice.GetDevice(), &allocInfo, nullptr, &m_BufMvpMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_VKDevice.GetDevice(), m_BufMvp, m_BufMvpMemory, 0);

		const VkDeviceSize materialSize = sizeof(Renderer::RHI::Material);
		m_MaterialDynamicStride = alignUp(materialSize, physProps.limits.minUniformBufferOffsetAlignment);
		const VkDeviceSize materialBufferSize = m_MaterialDynamicStride * static_cast<VkDeviceSize>(MAX_MODEL_DRAWS);

		bufInfo.size = materialBufferSize;
		bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
		res = vkCreateBuffer(m_VKDevice.GetDevice(), &bufInfo, nullptr, &m_BufMaterials);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkGetBufferMemoryRequirements(m_VKDevice.GetDevice(), m_BufMaterials, &memReq);
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
		res = vkAllocateMemory(m_VKDevice.GetDevice(), &allocInfo, nullptr, &m_BufMaterialsMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_VKDevice.GetDevice(), m_BufMaterials, m_BufMaterialsMemory, 0);

		const VkDeviceSize instanceSize = sizeof(Renderer::RHI::Instance) * MAX_MODEL_INSTANCES;
		bufInfo.size = instanceSize;
		bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
		res = vkCreateBuffer(m_VKDevice.GetDevice(), &bufInfo, nullptr, &m_BufInstances);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkGetBufferMemoryRequirements(m_VKDevice.GetDevice(), m_BufInstances, &memReq);
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
		res = vkAllocateMemory(m_VKDevice.GetDevice(), &allocInfo, nullptr, &m_BufInstancesMemory);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
		vkBindBufferMemory(m_VKDevice.GetDevice(), m_BufInstances, m_BufInstancesMemory, 0);
		m_BufInstancesSize = instanceSize;

		m_DescriptorSets.clear();
		for (const auto& [setIndex, layout] : m_SetLayouts) {
			VkDescriptorSetAllocateInfo allocSetInfo{};
			allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
			allocSetInfo.descriptorPool = m_ImGuiDescriptorPool;
			allocSetInfo.descriptorSetCount = 1;
			allocSetInfo.pSetLayouts = &layout;
			VkDescriptorSet ds = VK_NULL_HANDLE;
			res = vkAllocateDescriptorSets(m_VKDevice.GetDevice(), &allocSetInfo, &ds);
			CheckVkResult(res);
			if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }
			m_DescriptorSets.emplace_back(setIndex, ds);
		}

		auto findDescriptorSet = [&](uint32_t set) -> VkDescriptorSet {
			for (const auto& [idx, ds] : m_DescriptorSets) if (idx == set) return ds;
			return VK_NULL_HANDLE;
		};
		auto writeEngineBuffer = [&](const char* name, VkBuffer buffer, VkDeviceSize range) {
			const RHI::RHI_BindingInfo* info = m_ModelPipelineReflection.FindBindingByName(name);
			if (!info || buffer == VK_NULL_HANDLE) return;
			VkDescriptorSet ds = findDescriptorSet(info->m_Key.m_Set);
			if (ds == VK_NULL_HANDLE) return;

			VkDescriptorBufferInfo bufferInfo{};
			bufferInfo.buffer = buffer;
			bufferInfo.offset = 0;
			bufferInfo.range = range;

			VkWriteDescriptorSet write{};
			write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
			write.dstSet = ds;
			write.dstBinding = info->m_Key.m_Binding;
			write.dstArrayElement = 0;
			write.descriptorCount = 1;
			write.descriptorType = ToVkDescriptorType(*info);
			write.pBufferInfo = &bufferInfo;
			vkUpdateDescriptorSets(m_VKDevice.GetDevice(), 1, &write, 0, nullptr);
		};
		writeEngineBuffer(RHI::EngineResourceName::Frame,     m_BufGlobals,   globalsSize);
		writeEngineBuffer(RHI::EngineResourceName::Mvp,       m_BufMvp,       mvpSize);
		writeEngineBuffer(RHI::EngineResourceName::Instances, m_BufInstances, instanceSize);
		writeEngineBuffer(RHI::EngineResourceName::Material,  m_BufMaterials, materialSize);

		std::vector<VkDescriptorSetLayout> setLayouts;
		setLayouts.reserve(m_SetLayouts.size());
		for (const auto& [setIndex, layout] : m_SetLayouts) setLayouts.push_back(layout);

		VkPipelineLayoutCreateInfo layoutInfo{};
		layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
		layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
		layoutInfo.pSetLayouts = setLayouts.data();

		res = vkCreatePipelineLayout(m_VKDevice.GetDevice(), &layoutInfo, nullptr, &m_ModelPipelineLayout);
		CheckVkResult(res);
		if (res != VK_SUCCESS) { DestroyModelPipeline(); return; }

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

		res = vkCreateGraphicsPipelines(m_VKDevice.GetDevice(), VK_NULL_HANDLE, 1, &pipe, nullptr, &m_ModelPipeline);
		CheckVkResult(res);

		if (res == VK_SUCCESS) NV_LOG_INFO("Model pipeline created.");
		else { NV_LOG_WARN("CreateModelPipeline: pipeline creation failed."); DestroyModelPipeline(); }
	}

	void VK_Renderer::DestroyModelPipeline() {
		if (m_ModelPipeline != VK_NULL_HANDLE) {
			vkDestroyPipeline(m_VKDevice.GetDevice(), m_ModelPipeline, nullptr);
			m_ModelPipeline = VK_NULL_HANDLE;
		}
		if (m_ModelPipelineLayout != VK_NULL_HANDLE) {
			vkDestroyPipelineLayout(m_VKDevice.GetDevice(), m_ModelPipelineLayout, nullptr);
			m_ModelPipelineLayout = VK_NULL_HANDLE;
		}
		if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
			for (auto& [setIndex, ds] : m_DescriptorSets) {
				if (ds != VK_NULL_HANDLE) vkFreeDescriptorSets(m_VKDevice.GetDevice(), m_ImGuiDescriptorPool, 1, &ds);
			}
		}
		m_DescriptorSets.clear();
		if (m_BufInstances != VK_NULL_HANDLE) { vkDestroyBuffer(m_VKDevice.GetDevice(), m_BufInstances, nullptr); m_BufInstances = VK_NULL_HANDLE; }
		if (m_BufInstancesMemory != VK_NULL_HANDLE) { vkFreeMemory(m_VKDevice.GetDevice(), m_BufInstancesMemory, nullptr); m_BufInstancesMemory = VK_NULL_HANDLE; }
		m_BufInstancesSize = 0;
		if (m_BufMaterials != VK_NULL_HANDLE) { vkDestroyBuffer(m_VKDevice.GetDevice(), m_BufMaterials, nullptr); m_BufMaterials = VK_NULL_HANDLE; }
		if (m_BufMaterialsMemory != VK_NULL_HANDLE) { vkFreeMemory(m_VKDevice.GetDevice(), m_BufMaterialsMemory, nullptr); m_BufMaterialsMemory = VK_NULL_HANDLE; }
		if (m_BufMvp != VK_NULL_HANDLE) { vkDestroyBuffer(m_VKDevice.GetDevice(), m_BufMvp, nullptr); m_BufMvp = VK_NULL_HANDLE; }
		if (m_BufMvpMemory != VK_NULL_HANDLE) { vkFreeMemory(m_VKDevice.GetDevice(), m_BufMvpMemory, nullptr); m_BufMvpMemory = VK_NULL_HANDLE; }
		if (m_BufGlobals != VK_NULL_HANDLE) { vkDestroyBuffer(m_VKDevice.GetDevice(), m_BufGlobals, nullptr); m_BufGlobals = VK_NULL_HANDLE; }
		if (m_BufGlobalsMemory != VK_NULL_HANDLE) { vkFreeMemory(m_VKDevice.GetDevice(), m_BufGlobalsMemory, nullptr); m_BufGlobalsMemory = VK_NULL_HANDLE; }
		for (auto& [setIndex, layout] : m_SetLayouts) {
			if (layout != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_VKDevice.GetDevice(), layout, nullptr);
		}
		m_SetLayouts.clear();
	}

} // namespace Nova::Core::Renderer::Backends::Vulkan
