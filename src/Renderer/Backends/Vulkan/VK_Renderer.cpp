#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include "Core/Application.h"
#include "Core/Assert.h"
#include "Core/Window.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

#include "imgui.h"
#include "backends/imgui_impl_vulkan.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <array>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    const std::vector<VkImageView>& VK_Renderer::GetSwapchainImageViews() const {
        static thread_local std::vector<VkImageView> views;
        views.clear();
        for (const auto& img : m_VKSwapchain.GetImages())
            views.push_back(img.m_ImageView);
        return views;
    }

    VkFramebuffer VK_Renderer::GetSwapchainFramebuffer(uint32_t imageIndex) const {
        if (auto* graph = GetVKRenderGraph()) {
            const auto& fbs = graph->GetSwapchainFramebuffers();
            if (imageIndex < fbs.size()) return fbs[imageIndex];
        }
        return VK_NULL_HANDLE;
    }

    VK_RenderGraph* VK_Renderer::GetVKRenderGraph() const {
        return dynamic_cast<VK_RenderGraph*>(m_RenderGraph.get());
    }

    void VK_Renderer::WarnIfNoPipeline(const char* operation) const {
        if (!m_RenderGraph || !m_RenderGraph->IsCompiled()) {
            NV_LOG_WARN(("VK_Renderer: no render graph bound — " + std::string(operation)).c_str());
        }
    }

    VkCommandBuffer VK_Renderer::GetCurrentCommandBuffer() {
        if (!m_FrameActive) return VK_NULL_HANDLE;
        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();
        const auto& cmdBuffers = m_VKSwapchain.GetCommandBuffers();
        if (imageIndex >= cmdBuffers.size()) return VK_NULL_HANDLE;
        return cmdBuffers[imageIndex];
    }

    bool VK_Renderer::Create(const RHI::RHI_SwapchainDesc& desc) {
        m_SwapchainDesc = desc;
        NV_LOG_INFO("Creating Vulkan renderer (instance, device, swapchain)...");

        const std::vector<const char*> deviceExtensions = { VK_KHR_SWAPCHAIN_EXTENSION_NAME };

        if (!m_VKInstance.Create(desc.m_CreateSurface)) {
            NV_LOG_ERROR("VK_Instance::Create failed");
            return false;
        }

        if (!m_VKDevice.Create(m_VKInstance.GetInstance(), m_VKInstance.GetSurface(), deviceExtensions)) {
            NV_LOG_ERROR("VK_Device::Create failed");
            return false;
        }

        if (desc.m_EnableSwapchain && m_VKInstance.GetSurface() != VK_NULL_HANDLE) {
            if (!m_VKSwapchain.Create(
                    m_VKDevice.GetPhysicalDevice(),
                    m_VKDevice.GetDevice(),
                    m_VKInstance.GetSurface(),
                    m_VKDevice.GetGraphicsQueue(),
                    m_VKDevice.GetPresentQueue(),
                    m_VKDevice.GetGraphicsQueueFamily(),
                    m_VKDevice.GetPresentQueueFamily(),
                    desc)) {
                NV_LOG_ERROR("Failed to create swapchain");
                return false;
            }
        } else if (desc.m_EnableSwapchain) {
            NV_LOG_WARN("Swapchain requested but no surface is available; skipping swapchain creation.");
        }

        m_FrameActive = false;
        NV_LOG_INFO("Vulkan renderer core created.");
        return true;
    }

    void VK_Renderer::Destroy() {
        NV_LOG_INFO("Destroying Vulkan renderer...");

        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE)
            vkDeviceWaitIdle(m_VKDevice.GetDevice());

        DestroyViewportFramebuffer();

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.DestroyImGuiBackend(GraphicsAPI::Vulkan);

        for (auto& [key, mesh] : m_MeshCache) {
            if (mesh) mesh->Release();
        }
        m_MeshCache.clear();

        m_RenderGraph.reset();

        m_VKSwapchain.Destroy();
        m_VKDevice.Destroy();
        m_VKInstance.Destroy();

        m_FrameActive = false;
        NV_LOG_INFO("Vulkan renderer destroyed.");
    }

    void VK_Renderer::SetPipeline(std::unique_ptr<RHI::IRenderGraph> graph) {
        if (auto* vkGraph = GetVKRenderGraph())
            vkGraph->Destroy();

        m_RenderGraph = std::move(graph);
        if (!m_RenderGraph) return;

        auto* vkGraph = dynamic_cast<VK_RenderGraph*>(m_RenderGraph.get());
        if (!vkGraph) {
            NV_LOG_ERROR("VK_Renderer::SetPipeline - render graph is not a Vulkan implementation");
            m_RenderGraph.reset();
            return;
        }

        if (!vkGraph->Create(*this)) {
            NV_LOG_ERROR("VK_Renderer::SetPipeline - failed to compile render graph");
            m_RenderGraph.reset();
        }
    }

    void VK_Renderer::Update(float dt) {
        if (m_RenderGraph)
            m_RenderGraph->ReloadChangedShaders();
        (void)dt;
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

    void VK_Renderer::BeginFrame() {
        m_FrameActive = false;

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        imguiLayer.SetVulkanBeforeRenderCallback({});

        if (!m_RenderGraph) {
            WarnIfNoPipeline("BeginFrame skipped");
            return;
        }

        auto* vkGraph = GetVKRenderGraph();
        if (!vkGraph) {
            WarnIfNoPipeline("BeginFrame skipped");
            return;
        }

        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            return;

        if (m_FramebufferResized) {
            m_FramebufferResized = false;
            if (!m_VKSwapchain.RecreateSwapchain())
                return;
            if (!vkGraph->RecreateSwapchainRenderTargets())
                return;
        }

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        auto& fs = m_VKSwapchain.GetFrameSync()[frameIndex];

        CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence, VK_TRUE, UINT64_MAX));

        uint32_t imageIndex = 0;
        VkResult acquireRes = vkAcquireNextImageKHR(
            m_VKDevice.GetDevice(),
            m_VKSwapchain.GetSwapchain(),
            UINT64_MAX,
            fs.m_ImageAvailableSemaphore,
            VK_NULL_HANDLE,
            &imageIndex);

        if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) {
            m_FramebufferResized = true;
            return;
        }
        if (acquireRes != VK_SUCCESS && acquireRes != VK_SUBOPTIMAL_KHR) {
            NV_LOG_ERROR("vkAcquireNextImageKHR failed");
            return;
        }

        m_VKSwapchain.SetAcquiredImageIndex(imageIndex);

        auto& imagesInFlight = m_VKSwapchain.GetImagesInFlight();
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE)
            CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX));
        imagesInFlight[imageIndex] = fs.m_InFlightFence;

        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];
        CheckVkResult(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo));

        imguiLayer.SetVulkanCommandBuffer(cmd);
        imguiLayer.SetVulkanBeforeRenderCallback([this]() {
            if (m_RenderGraph) m_RenderGraph->OnTransitionToImGuiPass();
        });

        m_FrameActive = true;
        m_RenderGraph->OnBeginFrame();
    }

    void VK_Renderer::BeginScene(const glm::mat4& view, const glm::mat4& proj) {
        if (m_RenderGraph)
            m_RenderGraph->OnBeginScene(view, proj);
    }

    void VK_Renderer::SetModelMatrix(const glm::mat4& model) {
        if (m_RenderGraph)
            m_RenderGraph->OnSetModelMatrix(model);
    }

    void VK_Renderer::Draw(const RHI::RHI_DrawCommand& cmd) {
        if (!m_FrameActive) return;
        if (!m_RenderGraph) { WarnIfNoPipeline("Draw ignored"); return; }
        m_RenderGraph->OnDraw(cmd);
    }

    void VK_Renderer::DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        if (!m_FrameActive) return;
        if (!m_RenderGraph) { WarnIfNoPipeline("DrawIndexed ignored"); return; }
        m_RenderGraph->OnDrawIndexed(cmd);
    }

    void* VK_Renderer::GetViewportTextureID() const {
        if (m_ViewportDescriptorSet == VK_NULL_HANDLE)
            return nullptr;
        return (void*)m_ViewportDescriptorSet;
    }

    void VK_Renderer::EndFrame() {
        if (!m_FrameActive) return;

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        imguiLayer.SetVulkanBeforeRenderCallback({});

        if (m_RenderGraph)
            m_RenderGraph->OnEndFrame();

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        auto& fs = m_VKSwapchain.GetFrameSync()[frameIndex];
        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();
        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];

        CheckVkResult(vkEndCommandBuffer(cmd));

        CheckVkResult(vkResetFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence));

        VkSemaphore waitSemaphores[] = { fs.m_ImageAvailableSemaphore };
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
        presentInfo.pImageIndices = &imageIndex;

        VkResult presentRes = vkQueuePresentKHR(m_VKDevice.GetPresentQueue(), &presentInfo);
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR)
            m_FramebufferResized = true;
        else if (presentRes != VK_SUCCESS)
            NV_LOG_ERROR("vkQueuePresentKHR failed");

        m_FrameActive = false;
        m_VKSwapchain.AdvanceFrame();
    }

    std::shared_ptr<VK_Mesh> VK_Renderer::GetOrUploadMesh(const std::shared_ptr<Renderer::RHI::RHI_Mesh>& cpuMesh) {
        NV_ASSERT_MSG(cpuMesh, "VK_Renderer::GetOrUploadMesh received a null mesh.");
        if (!cpuMesh) return nullptr;

        auto it = m_MeshCache.find(cpuMesh.get());
        if (it != m_MeshCache.end())
            return it->second;

        auto vkMesh = std::make_shared<VK_Mesh>(*cpuMesh);
        vkMesh->Init(
            m_VKDevice.GetDevice(),
            m_VKDevice.GetPhysicalDevice(),
            m_VKSwapchain.GetCommandPool(),
            m_VKDevice.GetGraphicsQueue());
        vkMesh->Upload(*cpuMesh);

        m_MeshCache[cpuMesh.get()] = vkMesh;
        return vkMesh;
    }

    void VK_Renderer::CreateViewportFramebuffer(int w, int h) {
        if (w <= 0 || h <= 0) return;

        VkDevice device = m_VKDevice.GetDevice();
        VkPhysicalDevice physicalDevice = m_VKDevice.GetPhysicalDevice();
        VkFormat colorFormat = m_VKSwapchain.GetImageFormat();
        VkFormat depthFormat = GetVKRenderGraph() ? GetVKRenderGraph()->GetDepthFormat() : VK_FORMAT_D32_SFLOAT;

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

        CheckVkResult(vkCreateImage(device, &imageInfo, nullptr, &m_ViewportImage));

        VkMemoryRequirements memReq{};
        vkGetImageMemoryRequirements(device, m_ViewportImage, &memReq);
        VkPhysicalDeviceMemoryProperties memProps{};
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
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        CheckVkResult(vkCreateImageView(device, &viewInfo, nullptr, &m_ViewportImageView));

        VkImageCreateInfo depthImageInfo = imageInfo;
        depthImageInfo.format = depthFormat;
        depthImageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
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

        VkImageViewCreateInfo depthViewInfo = viewInfo;
        depthViewInfo.image = m_ViewportDepthImage;
        depthViewInfo.format = depthFormat;
        depthViewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        CheckVkResult(vkCreateImageView(device, &depthViewInfo, nullptr, &m_ViewportDepthImageView));

        std::array<VkImageView, 2> attachments = { m_ViewportImageView, m_ViewportDepthImageView };
        VkFramebufferCreateInfo fbInfo{};
        fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fbInfo.renderPass = GetVKRenderGraph() ? GetVKRenderGraph()->GetViewportRenderPass() : VK_NULL_HANDLE;
        fbInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        fbInfo.pAttachments = attachments.data();
        fbInfo.width = static_cast<uint32_t>(w);
        fbInfo.height = static_cast<uint32_t>(h);
        fbInfo.layers = 1;
        CheckVkResult(vkCreateFramebuffer(device, &fbInfo, nullptr, &m_ViewportFramebuffer));

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        CheckVkResult(vkCreateSampler(device, &samplerInfo, nullptr, &m_ViewportSampler));

        if (GetVKRenderGraph()) {
            m_ViewportDescriptorSet = ImGui_ImplVulkan_AddTexture(
                m_ViewportSampler,
                m_ViewportImageView,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void VK_Renderer::DestroyViewportFramebuffer() {
        VkDevice device = m_VKDevice.GetDevice();
        if (device == VK_NULL_HANDLE) return;

        CheckVkResult(vkDeviceWaitIdle(device));

        if (m_ViewportDescriptorSet != VK_NULL_HANDLE) {
            if (auto* vkGraph = GetVKRenderGraph()) {
                VkDescriptorPool pool = vkGraph->GetImGuiDescriptorPool();
                if (pool != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(device, pool, 1, &m_ViewportDescriptorSet);
            }
            m_ViewportDescriptorSet = VK_NULL_HANDLE;
        }
        if (m_ViewportSampler != VK_NULL_HANDLE) { vkDestroySampler(device, m_ViewportSampler, nullptr); m_ViewportSampler = VK_NULL_HANDLE; }
        if (m_ViewportFramebuffer != VK_NULL_HANDLE) { vkDestroyFramebuffer(device, m_ViewportFramebuffer, nullptr); m_ViewportFramebuffer = VK_NULL_HANDLE; }
        if (m_ViewportDepthImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_ViewportDepthImageView, nullptr); m_ViewportDepthImageView = VK_NULL_HANDLE; }
        if (m_ViewportDepthImage != VK_NULL_HANDLE) { vkDestroyImage(device, m_ViewportDepthImage, nullptr); m_ViewportDepthImage = VK_NULL_HANDLE; }
        if (m_ViewportDepthImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_ViewportDepthImageMemory, nullptr); m_ViewportDepthImageMemory = VK_NULL_HANDLE; }
        if (m_ViewportImageView != VK_NULL_HANDLE) { vkDestroyImageView(device, m_ViewportImageView, nullptr); m_ViewportImageView = VK_NULL_HANDLE; }
        if (m_ViewportImage != VK_NULL_HANDLE) { vkDestroyImage(device, m_ViewportImage, nullptr); m_ViewportImage = VK_NULL_HANDLE; }
        if (m_ViewportImageMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_ViewportImageMemory, nullptr); m_ViewportImageMemory = VK_NULL_HANDLE; }
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan