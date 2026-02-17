#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

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

// Tracks whether BeginFrame successfully started recording this frame.
// This protects you from calling Render()/EndFrame() when BeginFrame early-returned.
static bool s_FrameActive = false;

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

        s_FrameActive = false;

        NV_LOG_INFO("Vulkan renderer created successfully (minimal mode).");
        return true;
    }

    void VK_Renderer::Destroy() {
        NV_LOG_INFO("Destroying Vulkan renderer...");

        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_VKDevice.GetDevice());
        }
        m_VKSwapchain.Destroy();
        m_VKDevice.Destroy();
        m_VKInstance.Destroy();

        s_FrameActive = false;

        NV_LOG_INFO("Vulkan renderer destroyed.");
    }

    bool VK_Renderer::Resize(int w, int h) {
        (void)w; (void)h;
        // Mark for swapchain recreation on next BeginFrame
        m_FramebufferResized = true;
        return true;
    }

    void VK_Renderer::Update(float dt) {
        (void)dt;
    }

    void VK_Renderer::BeginFrame() {
        s_FrameActive = false;

        // ImGui safe default (ne pas laisser ImGui �crire dans un cmd buffer invalide)
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        }

        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            return;

        // Recreate swapchain si demand� AVANT acquire
        if (m_FramebufferResized) {
            m_FramebufferResized = false;
            if (!m_VKSwapchain.RecreateSwapchain())
                return;
        }

        // Index frame-in-flight (0..FRAMES_IN_FLIGHT-1)
        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        auto& fs = m_VKSwapchain.GetFrameSync()[frameIndex];

        // Attendre que la frame-in-flight soit libre
        CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence, VK_TRUE, UINT64_MAX));

        // Acquire image (renvoie un index d'image swapchain)
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

        // Stocker l�index d�image acquise dans la swapchain (plus besoin dans VK_Renderer)
        m_VKSwapchain.SetAcquiredImageIndex(imageIndex);

        // Attendre si cette image swapchain est d�j� utilis�e par une frame pr�c�dente
        auto& imagesInFlight = m_VKSwapchain.GetImagesInFlight();
        if (imagesInFlight[imageIndex] != VK_NULL_HANDLE) {
            CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &imagesInFlight[imageIndex], VK_TRUE, UINT64_MAX));
        }
        // Marquer cette image comme "in flight" via la fence de la frame courante
        imagesInFlight[imageIndex] = fs.m_InFlightFence;

        // IMPORTANT: cmd buffer index� par IMAGE (pas par frame-in-flight)
        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];
        CheckVkResult(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo));

        VkClearValue clearColor{};
        clearColor.color = { 0.1f, 0.1f, 0.12f, 1.0f };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_VKSwapchain.GetBackBufferRenderPass();
        rpBegin.framebuffer = m_VKSwapchain.GetFrames()[imageIndex].m_Framebuffer; // imageIndex
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = m_VKSwapchain.GetExtent();
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

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

        // ImGui enregistre dans le cmd buffer de CETTE image
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(cmd);
        }

        s_FrameActive = true;
    }

    void VK_Renderer::Render() {
        if (!s_FrameActive)
            return;

        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[m_VKSwapchain.GetAcquiredImageIndex()];

        // Minimal triangle draw (if pipeline exists).
        // If you haven't provided shaders, this simply does nothing (you still get a clear color).
        if (m_VKSwapchain.GetTrianglePipeline() != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_VKSwapchain.GetTrianglePipeline());
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

        // NOTE:
        // ImGui rendering is NOT done here. Your ImGuiLayer::OnEnd() will call
        // ImGui_ImplVulkan_RenderDrawData(drawData, cmd) when it runs.
        // This is why the render pass must still be active until EndFrame().
    }

    void VK_Renderer::EndFrame() {
        if (!s_FrameActive)
            return;

        // Emp�cher ImGui d'�crire apr�s la fermeture du cmd buffer
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        }

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        auto& fs = m_VKSwapchain.GetFrameSync()[frameIndex];

        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();

        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];

        vkCmdEndRenderPass(cmd);
        CheckVkResult(vkEndCommandBuffer(cmd));

        // Reset fence juste avant submit (pattern safe)
        CheckVkResult(vkResetFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence));

        VkSemaphore waitSemaphores[] = { fs.m_ImageAvailableSemaphore };   // celle du acquire de cette frame
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

        s_FrameActive = false;

        // next frame-in-flight
        m_VKSwapchain.AdvanceFrame();
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan