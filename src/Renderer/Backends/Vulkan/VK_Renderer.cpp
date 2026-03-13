#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <filesystem>

namespace Nova::Core::Renderer::Backends::Vulkan {

    bool VK_Renderer::Create() {
        NV_LOG_INFO("Creating Vulkan renderer (minimal mode)...");

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        // Instance
        if (!m_VKInstance.Create()) {
            NV_LOG_ERROR("VK_Instance::Create failed");
            return false;
        }

        // Device
        if (!m_VKDevice.Create(m_VKInstance.GetInstance(), m_VKInstance.GetSurface(), deviceExtensions)) {
            NV_LOG_ERROR("VK_Device::Create failed");
            return false;
        }

        // Swapchain
        if (!m_VKSwapchain.Create(
                m_VKDevice.GetPhysicalDevice(),
                m_VKDevice.GetDevice(),
                m_VKInstance.GetSurface(),
                m_VKDevice.GetGraphicsQueue(),
                m_VKDevice.GetPresentQueue(),
                m_VKDevice.GetGraphicsQueueFamily(),
                m_VKDevice.GetPresentQueueFamily()
            )) {
            NV_LOG_ERROR("Failed to create swapchain");
            return false;
        }
        
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = m_VKInstance.GetInstance();
        initInfo.PhysicalDevice = m_VKDevice.GetPhysicalDevice();
        initInfo.Device = m_VKDevice.GetDevice();
        initInfo.QueueFamily = m_VKDevice.GetGraphicsQueueFamily();
        initInfo.Queue = m_VKDevice.GetGraphicsQueue();
        initInfo.DescriptorPool = m_VKSwapchain.GetImGuiDescriptorPool();
        initInfo.DescriptorPoolSize = 0;
        initInfo.MinImageCount = m_VKSwapchain.GetFrames().size();
        initInfo.ImageCount = m_VKSwapchain.GetFrames().size();
        initInfo.PipelineCache = VK_NULL_HANDLE;

        initInfo.PipelineInfoMain.RenderPass = m_VKSwapchain.GetBackBufferRenderPass();
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        initInfo.PipelineInfoForViewports = initInfo.PipelineInfoMain;

        initInfo.UseDynamicRendering = false;

        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = CheckVkResult;
        initInfo.MinAllocationSize = 0;

        // Some imgui backends want RenderPass in their init info; your ImGuiLayer wraps this.
        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanInitInfo(initInfo);

        // Start with "no command buffer" until BeginFrame successfully starts recording.
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        imguiLayer.SetVulkanBeforeRenderCallback({});

        m_FrameActive = false;

        m_Shader = std::make_unique<VK_Shaders>();
        m_Shader->SetPipeline(m_VKSwapchain.GetModelPipeline(), m_VKSwapchain.GetModelPipelineLayout());
        m_Shader->SetSceneUBOs(
            m_VKDevice.GetDevice(),
            m_VKSwapchain.GetMVPUBOBuffer(),
            m_VKSwapchain.GetMVPUBOMemory(),
            m_VKSwapchain.GetMaterialUBOBuffer(),
            m_VKSwapchain.GetMaterialUBOMemory(),
            m_VKSwapchain.GetSceneDescriptorSet()
        );

        NV_LOG_INFO("Vulkan renderer created successfully (minimal mode).");
        return true;
    }

    void VK_Renderer::Destroy() {
        NV_LOG_INFO("Destroying Vulkan renderer...");

        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_VKDevice.GetDevice());
        }

        DestroyViewportFramebuffer();

        m_Shader.reset();

        for (auto& [key, mesh] : m_MeshCache) {
            if (mesh) {
                mesh->Release();
            }
        }
        m_MeshCache.clear();

        m_VKSwapchain.Destroy();
        m_VKDevice.Destroy();
        m_VKInstance.Destroy();

        m_FrameActive = false;

        NV_LOG_INFO("Vulkan renderer destroyed.");
    }

    bool VK_Renderer::Resize(int w, int h) {
        if (w > 0 && h > 0) {
            if (w != m_ViewportWidth || h != m_ViewportHeight) {
                DestroyViewportFramebuffer();
                CreateViewportFramebuffer(w, h);
                m_ViewportWidth = w;
                m_ViewportHeight = h;
            }
        } else {
            DestroyViewportFramebuffer();
            m_ViewportWidth = 0;
            m_ViewportHeight = 0;
        }
        return true;
    }

    void VK_Renderer::Update(float dt) {
        (void)dt;
    }

    void VK_Renderer::BeginFrame() {
        m_FrameActive = false;

        // Safe ImGui default: never let ImGui write into an invalid command buffer.
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
            imguiLayer.SetVulkanBeforeRenderCallback({});
        }

        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            return;

        // Recreate the swapchain if requested before acquiring an image.
        if (m_FramebufferResized) {
            m_FramebufferResized = false;
            if (!m_VKSwapchain.RecreateSwapchain())
                return;
        }

        // Index frame-in-flight (0..FRAMES_IN_FLIGHT-1)
        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        auto& fs = m_VKSwapchain.GetFrameSync()[frameIndex];

        // Wait until the current frame-in-flight is available.
        CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence, VK_TRUE, UINT64_MAX));

        // Acquire a swapchain image and retrieve its image index.
        uint32_t imageIndex = 0;
        VkResult acquireRes = vkAcquireNextImageKHR(
            m_VKDevice.GetDevice(),
            m_VKSwapchain.GetSwapchain(),
            UINT64_MAX,
            fs.m_ImageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex
        );

        if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
            m_FramebufferResized = true;
            return;
        }
        if (acquireRes != VK_SUCCESS && acquireRes != VK_SUBOPTIMAL_KHR) {
            NV_LOG_ERROR("vkAcquireNextImageKHR failed");
            return;
        }

        // Store the acquired image index in the swapchain state.
        m_VKSwapchain.SetAcquiredImageIndex(imageIndex);

        // Wait if that swapchain image is still used by an older frame.
        auto& imagesInFlight = m_VKSwapchain.GetImagesInFlight();
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX));
        }
        // Mark that image as in flight through the current frame fence.
        imagesInFlight[imageIndex] = fs.m_InFlightFence;

        // IMPORTANT: command buffers are indexed by swapchain image, not by frame-in-flight.
        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];
        CheckVkResult(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo));

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { 1.0f, 1.0f, 1.0f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };

        m_RenderedToViewportThisFrame = false;

        if (m_ViewportFramebuffer != VK_NULL_HANDLE) {
            // First time using viewport image: it is in UNDEFINED; transition to SHADER_READ_ONLY for the render pass initialLayout
            if (m_ViewportImageFirstUse) {
                m_ViewportImageFirstUse = false;
                VkImageMemoryBarrier barrier{};
                barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
                barrier.srcAccessMask = 0;
                barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                barrier.image = m_ViewportImage;
                barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
                barrier.subresourceRange.baseMipLevel = 0;
                barrier.subresourceRange.levelCount = 1;
                barrier.subresourceRange.baseArrayLayer = 0;
                barrier.subresourceRange.layerCount = 1;
                vkCmdPipelineBarrier(cmd,
                    VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                    VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                    0, 0, nullptr, 0, nullptr, 1, &barrier);
            }

            // Render scene to offscreen viewport for ImGui panel
            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = m_VKSwapchain.GetViewportRenderPass();
            rpBegin.framebuffer = m_ViewportFramebuffer;
            rpBegin.renderArea = { {0, 0}, { static_cast<uint32_t>(m_ViewportWidth), static_cast<uint32_t>(m_ViewportHeight) } };
            rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
            rpBegin.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)m_ViewportWidth;
            viewport.height = (float)m_ViewportHeight;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = { static_cast<uint32_t>(m_ViewportWidth), static_cast<uint32_t>(m_ViewportHeight) };
            vkCmdSetScissor(cmd, 0, 1, &scissor);

            if (m_VKSwapchain.GetModelPipeline() != VK_NULL_HANDLE)
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VKSwapchain.GetModelPipeline());

            m_RenderedToViewportThisFrame = true;
        } else {
            // Render directly to swapchain
            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = m_VKSwapchain.GetBackBufferRenderPass();
            rpBegin.framebuffer = m_VKSwapchain.GetFrames()[imageIndex].m_Framebuffer;
            rpBegin.renderArea = { {0,0}, m_VKSwapchain.GetExtent() };
            rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
            rpBegin.pClearValues = clearValues.data();

            vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = (float)m_VKSwapchain.GetExtent().width;
            viewport.height = (float)m_VKSwapchain.GetExtent().height;
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = m_VKSwapchain.GetExtent();
            vkCmdSetScissor(cmd, 0, 1, &scissor);
        }

        // ImGui records into the command buffer of this acquired image.
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(cmd);
            if (m_RenderedToViewportThisFrame) {
                imguiLayer.SetVulkanBeforeRenderCallback([this]() { BeginImGuiRenderPass(); });
            }
        }

        m_FrameActive = true;
    }

    void VK_Renderer::BeginScene(const glm::mat4& view, const glm::mat4& proj) {
        if (!m_Shader || !m_Shader->IsValid()) return;

        m_Shader->SetParameter("view", view);
        m_Shader->SetParameter("proj", proj);
    }

    void VK_Renderer::SetModelMatrix(const glm::mat4& model) {
        if (!m_Shader || !m_Shader->IsValid()) return;
        m_Shader->SetParameter("model", model);
    }

    void VK_Renderer::Draw(const RHI::RHI_DrawCommand& cmd) {
        if (!m_FrameActive) return;
        if (!cmd.m_Mesh)    return;

        // Get or upload the GPU mesh for this CPU mesh
        auto vkMesh = GetOrUploadMesh(cmd.m_Mesh);
        if (!vkMesh) return;

        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();
        VkCommandBuffer vkCmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];

        if (m_Shader && m_Shader->IsValid()) {
            m_Shader->Bind(vkCmd);
            m_Shader->ApplyParameters(vkCmd);
        }

        // Bind vertex (and optionally index) buffers
        vkMesh->SetCommandBuffer(vkCmd);
        vkMesh->Bind();

        // Non-indexed draw
        vkCmdDraw(
            vkCmd,
            static_cast<uint32_t>(cmd.m_VertexCount),
            static_cast<uint32_t>(cmd.m_InstanceCount),
            static_cast<uint32_t>(cmd.m_FirstVertex),
            static_cast<uint32_t>(cmd.m_FirstInstance)
        );
    }

    void VK_Renderer::DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        if (!m_FrameActive) return;
        if (!cmd.m_Mesh)    return;

        auto vkMesh = GetOrUploadMesh(cmd.m_Mesh);
        if (!vkMesh) return;

        VkCommandBuffer vkCmd = m_VKSwapchain.GetCommandBuffers()[m_VKSwapchain.GetAcquiredImageIndex()];

        if (m_Shader && m_Shader->IsValid()) {
            m_Shader->Bind(vkCmd);
            m_Shader->ApplyParameters(vkCmd);
        }

        // Bind mesh buffers
        vkMesh->SetCommandBuffer(vkCmd);
        vkMesh->Bind();

        if (cmd.m_IndexType != RHI::RHI_IndexType::UInt32) {
            NV_LOG_WARN("VK_Renderer::DrawIndexed currently supports only UInt32 index buffers.");
            return;
        }

        // Draw
        vkCmdDrawIndexed(vkCmd,
            static_cast<uint32_t>(cmd.m_IndexCount),
            static_cast<uint32_t>(cmd.m_InstanceCount),
            static_cast<uint32_t>(cmd.m_FirstIndex),
            cmd.m_VertexOffset,
            static_cast<uint32_t>(cmd.m_FirstInstance));
    }

    std::shared_ptr<VK_Mesh> VK_Renderer::GetOrUploadMesh(
        const std::shared_ptr<Renderer::Graphics::Mesh>& cpuMesh)
    {
        auto it = m_MeshCache.find(cpuMesh.get());
        if (it != m_MeshCache.end())
            return it->second;

        auto vkMesh = std::make_shared<VK_Mesh>(*cpuMesh);
        vkMesh->Init(
            m_VKDevice.GetDevice(),
            m_VKDevice.GetPhysicalDevice(),
            m_VKSwapchain.GetCommandPool(), // temporarily reuse the renderer command pool
            m_VKDevice.GetGraphicsQueue()
        );

        // IMPORTANT: Init() needs the command pool, not a command buffer.
        vkMesh->Upload(*cpuMesh);

        m_MeshCache[cpuMesh.get()] = vkMesh;
        return vkMesh;
    }

    void VK_Renderer::CreateViewportFramebuffer(int w, int h) {
        if (w <= 0 || h <= 0) return;

        m_ViewportImageFirstUse = true; // new image is in UNDEFINED

        VkDevice device = m_VKDevice.GetDevice();
        VkPhysicalDevice physicalDevice = m_VKDevice.GetPhysicalDevice();
        VkFormat colorFormat = m_VKSwapchain.GetSwapchainImageFormat();
        VkFormat depthFormat = m_VKSwapchain.GetDepthFormat();

        // Color image (attachment + sampled by ImGui)
        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = colorFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        CheckVkResult(vkCreateImage(device, &imageInfo, nullptr, &m_ViewportImage));

        VkMemoryRequirements memReq;
        vkGetImageMemoryRequirements(device, m_ViewportImage, &memReq);
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProps);
        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memTypeIndex = i;
                break;
            }
        }
        if (memTypeIndex == UINT32_MAX) {
            NV_LOG_ERROR("VK_Renderer: no memory type for viewport color image");
            vkDestroyImage(device, m_ViewportImage, nullptr);
            m_ViewportImage = VK_NULL_HANDLE;
            return;
        }
        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memTypeIndex;
        CheckVkResult(vkAllocateMemory(device, &allocInfo, nullptr, &m_ViewportImageMemory));
        vkBindImageMemory(device, m_ViewportImage, m_ViewportImageMemory, 0);

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_ViewportImage;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = colorFormat;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.baseArrayLayer = 0;
        viewInfo.subresourceRange.layerCount = 1;
        CheckVkResult(vkCreateImageView(device, &viewInfo, nullptr, &m_ViewportImageView));

        // Depth image
        VkImageCreateInfo depthImageInfo{};
        depthImageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        depthImageInfo.imageType = VK_IMAGE_TYPE_2D;
        depthImageInfo.extent = { static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1 };
        depthImageInfo.mipLevels = 1;
        depthImageInfo.arrayLayers = 1;
        depthImageInfo.format = depthFormat;
        depthImageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        depthImageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        depthImageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        depthImageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        CheckVkResult(vkCreateImage(device, &depthImageInfo, nullptr, &m_ViewportDepthImage));

        vkGetImageMemoryRequirements(device, m_ViewportDepthImage, &memReq);
        memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                memTypeIndex = i;
                break;
            }
        }
        if (memTypeIndex == UINT32_MAX) {
            DestroyViewportFramebuffer();
            return;
        }
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memTypeIndex;
        CheckVkResult(vkAllocateMemory(device, &allocInfo, nullptr, &m_ViewportDepthImageMemory));
        vkBindImageMemory(device, m_ViewportDepthImage, m_ViewportDepthImageMemory, 0);

        VkImageViewCreateInfo depthViewInfo{};
        depthViewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        depthViewInfo.image = m_ViewportDepthImage;
        depthViewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        depthViewInfo.format = depthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depthViewInfo.subresourceRange.baseMipLevel = 0;
        depthViewInfo.subresourceRange.levelCount = 1;
        depthViewInfo.subresourceRange.baseArrayLayer = 0;
        depthViewInfo.subresourceRange.layerCount = 1;
        CheckVkResult(vkCreateImageView(device, &depthViewInfo, nullptr, &m_ViewportDepthImageView));

        // Framebuffer
        std::array<VkImageView, 2> attachments = { m_ViewportImageView, m_ViewportDepthImageView };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = m_VKSwapchain.GetViewportRenderPass();
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = static_cast<uint32_t>(w);
        fbInfo.height = static_cast<uint32_t>(h);
        fbInfo.layers = 1;
        CheckVkResult(vkCreateFramebuffer(device, &fbInfo, nullptr, &m_ViewportFramebuffer));

        // Sampler for ImGui
        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        CheckVkResult(vkCreateSampler(device, &samplerInfo, nullptr, &m_ViewportSampler));

        // Descriptor set for ImGui::Image(GetViewportTextureID(), ...)
        m_ViewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
            m_ViewportSampler,
            m_ViewportImageView,
            VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL
        );
    }

    void VK_Renderer::DestroyViewportFramebuffer() {
        VkDevice device = m_VKDevice.GetDevice();
        if (device == VK_NULL_HANDLE) return;

        // Wait for all submitted work to finish so no command buffer still references
        // the viewport framebuffer, descriptor set, or images (VUID-vkFreeDescriptorSets-00309, etc.).
        CheckVkResult(vkDeviceWaitIdle(device));

        // Free descriptor set directly: at shutdown ImGui may already be destroyed, so do not
        // call ImGui_ImplVulkan_RemoveTexture() which would use freed backend data.
        if (m_ViewportDescriptorSet != VK_NULL_HANDLE) {
            VkDescriptorPool pool = m_VKSwapchain.GetImGuiDescriptorPool();
            if (pool != VK_NULL_HANDLE)
                vkFreeDescriptorSets(device, pool, 1, &m_ViewportDescriptorSet);
            m_ViewportDescriptorSet = VK_NULL_HANDLE;
        }
        if (m_ViewportSampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, m_ViewportSampler, nullptr);
            m_ViewportSampler = VK_NULL_HANDLE;
        }
        if (m_ViewportFramebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, m_ViewportFramebuffer, nullptr);
            m_ViewportFramebuffer = VK_NULL_HANDLE;
        }
        if (m_ViewportDepthImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_ViewportDepthImageView, nullptr);
            m_ViewportDepthImageView = VK_NULL_HANDLE;
        }
        if (m_ViewportDepthImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, m_ViewportDepthImage, nullptr);
            m_ViewportDepthImage = VK_NULL_HANDLE;
        }
        if (m_ViewportDepthImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_ViewportDepthImageMemory, nullptr);
            m_ViewportDepthImageMemory = VK_NULL_HANDLE;
        }
        if (m_ViewportImageView != VK_NULL_HANDLE) {
            vkDestroyImageView(device, m_ViewportImageView, nullptr);
            m_ViewportImageView = VK_NULL_HANDLE;
        }
        if (m_ViewportImage != VK_NULL_HANDLE) {
            vkDestroyImage(device, m_ViewportImage, nullptr);
            m_ViewportImage = VK_NULL_HANDLE;
        }
        if (m_ViewportImageMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_ViewportImageMemory, nullptr);
            m_ViewportImageMemory = VK_NULL_HANDLE;
        }
    }

    void VK_Renderer::BeginImGuiRenderPass() {
        if (!m_FrameActive || !m_RenderedToViewportThisFrame)
            return;

        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();
        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];

        vkCmdEndRenderPass(cmd);
        // The viewport render pass has finalLayout = SHADER_READ_ONLY_OPTIMAL, so the
        // image is already transitioned by the driver when ending the pass. No extra barrier.

        // Begin swapchain render pass for ImGui
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_VKSwapchain.GetBackBufferRenderPass();
        rpBegin.framebuffer = m_VKSwapchain.GetFrames()[imageIndex].m_Framebuffer;
        rpBegin.renderArea = { {0, 0}, m_VKSwapchain.GetExtent() };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_VKSwapchain.GetExtent().width;
        viewport.height = (float)m_VKSwapchain.GetExtent().height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_VKSwapchain.GetExtent();
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void* VK_Renderer::GetViewportTextureID() const {
        if (m_ViewportDescriptorSet == VK_NULL_HANDLE)
            return nullptr;
        return (void*)m_ViewportDescriptorSet;
    }

    void VK_Renderer::EndFrame() {
        if (!m_FrameActive)
            return;

        // Prevent ImGui from writing after the command buffer has been closed.
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
            imguiLayer.SetVulkanBeforeRenderCallback({});
        }

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        auto& fs = m_VKSwapchain.GetFrameSync()[frameIndex];

        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();

        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];

        vkCmdEndRenderPass(cmd);
        CheckVkResult(vkEndCommandBuffer(cmd));

        // Reset the fence right before queue submission.
        CheckVkResult(vkResetFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence));

        VkSemaphore waitSemaphores[] = { fs.m_ImageAvailableSemaphore };   // signaled by this frame's acquire
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore renderFinishedSemaphore = m_VKSwapchain.GetRenderFinishedSemaphore(imageIndex);
        VkSemaphore signalSemaphores[] = { renderFinishedSemaphore };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        CheckVkResult(vkQueueSubmit(m_VKDevice.GetGraphicsQueue(), 1, &submitInfo, fs.m_InFlightFence));

        VkSwapchainKHR swapchains[] = { m_VKSwapchain.GetSwapchain() };

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = swapchains;
        presentInfo.pImageIndices = &imageIndex; // image acquise

        VkResult presentRes = vkQueuePresentKHR(m_VKDevice.GetPresentQueue(), &presentInfo);
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
            m_FramebufferResized = true;
        }
        else if (presentRes != VK_SUCCESS) {
            NV_LOG_ERROR("vkQueuePresentKHR failed");
        }

        m_FrameActive = false;

        // next frame-in-flight
        m_VKSwapchain.AdvanceFrame();
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan