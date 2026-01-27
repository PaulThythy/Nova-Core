#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include "Core/Application.h"
#include "Core/Window.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <string>
#include <vector>
#include <cstring>
#include <limits>
#include <thread>
#include <atomic>

namespace Nova::Core::Renderer::Backends::Vulkan {

    // Tracks whether we have already submitted at least one frame.
    // Prevents deadlock on first frame if the fence was created unsignaled.
    static bool s_HasSubmittedAtLeastOnce = false;

    // We reserve the LAST secondary command buffer for ImGui.
    // All other secondary command buffers are used for engine rendering (optionally multi-threaded).
    static constexpr uint32_t GetImGuiSecondaryIndex() {
        return (VK_Swapchain::WORKER_THREAD_COUNT > 0) ? (VK_Swapchain::WORKER_THREAD_COUNT - 1) : 0;
    }

    static constexpr uint32_t GetEngineSecondaryCount() {
        return (VK_Swapchain::WORKER_THREAD_COUNT > 1) ? (VK_Swapchain::WORKER_THREAD_COUNT - 1) : 0;
    }

    bool VK_Renderer::Create() {
        NV_LOG_INFO("Creating Vulkan renderer...");

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        if (!m_VKInstance.Create()) {
            NV_LOG_ERROR("VK_Instance::Create failed");
            return false;
        }

        if (!m_VKDevice.Create(m_VKInstance.GetInstance(), m_VKInstance.GetSurface(), deviceExtensions)) {
            NV_LOG_ERROR("VK_Device::Create failed");
            return false;
        }

        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);

        VkExtent2D initialExtent{
            static_cast<uint32_t>(std::max(1, w)),
            static_cast<uint32_t>(std::max(1, h))
        };

        if (!m_VKSwapchain.Create(
            m_VKDevice.GetPhysicalDevice(),
            m_VKDevice.GetDevice(),
            m_VKInstance.GetSurface(),
            m_VKDevice.GetGraphicsQueue(),
            m_VKDevice.GetPresentQueue(),
            m_VKDevice.GetGraphicsQueueFamily(),
            m_VKDevice.GetPresentQueueFamily(),
            initialExtent
        )) {
            NV_LOG_ERROR("VK_Swapchain::Create failed");
            return false;
        }

        // --- ImGui descriptor pool ---
        if (!CreateImGuiDescriptorPool()) {
            NV_LOG_ERROR("Failed to create ImGui descriptor pool");
            return false;
        }

        // ---- Init ImGui Vulkan backend ----
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = m_VKInstance.GetInstance();
        initInfo.PhysicalDevice = m_VKDevice.GetPhysicalDevice();
        initInfo.Device = m_VKDevice.GetDevice();
        initInfo.QueueFamily = m_VKDevice.GetGraphicsQueueFamily();
        initInfo.Queue = m_VKDevice.GetGraphicsQueue();
        initInfo.DescriptorPool = m_ImGuiDescriptorPool;
        initInfo.DescriptorPoolSize = 0;
        initInfo.MinImageCount = m_VKSwapchain.GetMinImageCount();
        initInfo.ImageCount = m_VKSwapchain.GetImageCount();
        initInfo.PipelineCache = VK_NULL_HANDLE;

        initInfo.PipelineInfoMain.RenderPass = m_VKSwapchain.GetRenderPass();
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        initInfo.PipelineInfoForViewports = initInfo.PipelineInfoMain;

        initInfo.UseDynamicRendering = false;

        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = CheckVkResult;
        initInfo.MinAllocationSize = 0;

        Nova::Core::ImGuiLayer& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanInitInfo(initInfo);

        s_HasSubmittedAtLeastOnce = false;

        NV_LOG_INFO("Vulkan renderer created successfully.");
        return true;
    }

    void VK_Renderer::Destroy() {
        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_VKDevice.GetDevice());
        }

        DestroyImGuiDescriptorPool();

        m_VKSwapchain.Destroy();

        m_VKDevice.Destroy();
        m_VKInstance.Destroy();

        NV_LOG_INFO("Vulkan renderer destroyed.");
    }

    bool VK_Renderer::Resize(int w, int h) {
        (void)w; (void)h;
        m_FramebufferResized = true;
        m_VKSwapchain.SetFramebufferResized(true);
        return true;
    }

    void VK_Renderer::Update(float dt) {
        (void)dt;
        // Later: animations, camera, etc.
    }

    bool VK_Renderer::RecreateSwapchain() {
        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();

        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);

        VkExtent2D extent{
            static_cast<uint32_t>(w),
            static_cast<uint32_t>(h)
        };

        // If minimized (0,0), skip
        if (extent.width == 0 || extent.height == 0) {
            return false;
        }

        const bool ok = m_VKSwapchain.RecreateSwapchain(extent);
        if (!ok) {
            return false;
        }

        NV_LOG_INFO("Swapchain recreated.");
        return true;
    }

    void VK_Renderer::BeginFrame() {
        m_FrameActive = false;

        // ImGuiLayer::OnEnd() will record Vulkan draw calls.
        // Always clear the command buffer first to avoid stale usage on skipped frames.
        {
            Nova::Core::ImGuiLayer& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        }

        // Resize request
        if (m_FramebufferResized || m_VKSwapchain.WasFramebufferResized()) {
            m_FramebufferResized = false;
            m_VKSwapchain.SetFramebufferResized(false);

            if (!RecreateSwapchain()) {
                // Window minimized or error => skip frame
                return;
            }
        }

        uint32_t imageIndex = 0;
        if (!m_VKSwapchain.AcquireNextImage(imageIndex)) {
            // Out of date -> recreate
            RecreateSwapchain();
            return;
        }

        m_CurrentImageIndex = imageIndex;

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();

        // Wait/reset the in-flight fence BEFORE reusing per-frame command buffers.
        // Only wait if we already submitted at least once, otherwise we can deadlock at startup.
        {
            VkFence fence = m_VKSwapchain.GetInFlightFence();

            if (s_HasSubmittedAtLeastOnce) {
                CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &fence, VK_TRUE, UINT64_MAX));
            }

            CheckVkResult(vkResetFences(m_VKDevice.GetDevice(), 1, &fence));
        }

        VkCommandBuffer primary = m_VKSwapchain.GetPrimaryCommandBuffer(frameIndex);

        // Reset primary command buffer
        vkResetCommandBuffer(primary, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

        CheckVkResult(vkBeginCommandBuffer(primary, &beginInfo));

        // Begin render pass
        VkClearValue clearColor{};
        clearColor.color = { { 0.1f, 0.1f, 0.12f, 1.0f } };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_VKSwapchain.GetRenderPass();
        rpBegin.framebuffer = m_VKSwapchain.GetFramebuffer(imageIndex);
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = m_VKSwapchain.GetExtent();
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        // IMPORTANT:
        // We use SECONDARY_COMMAND_BUFFERS because we call vkCmdExecuteCommands() in this subpass.
        // With VK_SUBPASS_CONTENTS_INLINE, vkCmdExecuteCommands() is INVALID and will trigger validation errors.
        vkCmdBeginRenderPass(primary, &rpBegin, VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS);

        // --------------------------------------------------------------------
        // Prepare the ImGui secondary command buffer for this frame.
        //
        // Goal:
        // - ImGuiLayer::OnEnd() will call ImGui_ImplVulkan_RenderDrawData(drawData, cmd)
        // - That function DOES NOT call vkBeginCommandBuffer() for you.
        // - So we must provide a command buffer that is ALREADY in recording state.
        //
        // We begin the UI secondary here and we ONLY end it in EndFrame(),
        // after ImGuiLayer::OnEnd() has recorded its commands.
        // --------------------------------------------------------------------
        {
            const uint32_t uiIndex = GetImGuiSecondaryIndex();
            VkCommandBuffer uiCmd = m_VKSwapchain.GetSecondaryCommandBuffer(frameIndex, uiIndex);

            vkResetCommandBuffer(uiCmd, 0);

            VkCommandBufferInheritanceInfo inherit{};
            inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
            inherit.renderPass = m_VKSwapchain.GetRenderPass();
            inherit.subpass = 0;
            inherit.framebuffer = m_VKSwapchain.GetFramebuffer(m_CurrentImageIndex);

            VkCommandBufferBeginInfo uiBegin{};
            uiBegin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            uiBegin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            uiBegin.pInheritanceInfo = &inherit;

            CheckVkResult(vkBeginCommandBuffer(uiCmd, &uiBegin));

            // Optional: set viewport/scissor here (ImGui backend also sets them).
            const VkExtent2D extent = m_VKSwapchain.GetExtent();

            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(uiCmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = extent;
            vkCmdSetScissor(uiCmd, 0, 1, &scissor);

            // Give the recording UI secondary to ImGui for this frame.
            Nova::Core::ImGuiLayer& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(uiCmd);
        }

        m_FrameActive = true;
    }

    void VK_Renderer::Render() {
        if (!m_FrameActive)
            return;

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        VkCommandBuffer primary = m_VKSwapchain.GetPrimaryCommandBuffer(frameIndex);

        const uint32_t engineCount = GetEngineSecondaryCount();
        if (engineCount == 0) {
            // No engine secondary buffers available (only the ImGui secondary exists).
            // That's valid: engine rendering can be empty while ImGui still renders.
            return;
        }

        std::vector<VkCommandBuffer> engineCBs(engineCount);
        for (uint32_t t = 0; t < engineCount; t++) {
            engineCBs[t] = m_VKSwapchain.GetSecondaryCommandBuffer(frameIndex, t);
            vkResetCommandBuffer(engineCBs[t], 0);
        }

        VkCommandBufferInheritanceInfo inherit{};
        inherit.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO;
        inherit.renderPass = m_VKSwapchain.GetRenderPass();
        inherit.subpass = 0;
        inherit.framebuffer = m_VKSwapchain.GetFramebuffer(m_CurrentImageIndex);

        const VkExtent2D extent = m_VKSwapchain.GetExtent();

        auto BeginSecondary = [&](VkCommandBuffer cmd) {
            VkCommandBufferBeginInfo bi{};
            bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT |
                VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT;
            bi.pInheritanceInfo = &inherit;

            CheckVkResult(vkBeginCommandBuffer(cmd, &bi));

            // Dynamic viewport/scissor must be set inside each secondary if your pipelines use dynamic state
            VkViewport viewport{};
            viewport.x = 0.0f;
            viewport.y = 0.0f;
            viewport.width = static_cast<float>(extent.width);
            viewport.height = static_cast<float>(extent.height);
            viewport.minDepth = 0.0f;
            viewport.maxDepth = 1.0f;
            vkCmdSetViewport(cmd, 0, 1, &viewport);

            VkRect2D scissor{};
            scissor.offset = { 0, 0 };
            scissor.extent = extent;
            vkCmdSetScissor(cmd, 0, 1, &scissor);
            };

        std::atomic<bool> ok(true);

        auto RecordWorker = [&](uint32_t threadIndex) {
            VkCommandBuffer cmd = engineCBs[threadIndex];
            BeginSecondary(cmd);

            // TODO: record engine drawcalls here
            // vkCmdBindPipeline(...)
            // vkCmdBindVertexBuffers(...)
            // vkCmdBindIndexBuffer(...)
            // vkCmdDrawIndexed(...)

            if (vkEndCommandBuffer(cmd) != VK_SUCCESS) {
                ok.store(false);
            }
            };

        // NOTE:
        // Command pools are NOT thread-safe in Vulkan.
        // This is only valid if your swapchain provides one VkCommandPool per thread/secondary.
        // If not, record these secondary command buffers sequentially on the main thread.
        std::vector<std::thread> workers;
        workers.reserve(engineCount);
        for (uint32_t t = 0; t < engineCount; t++) {
            workers.emplace_back(RecordWorker, t);
        }
        for (auto& th : workers) th.join();

        if (!ok.load()) {
            NV_LOG_ERROR("Engine secondary command buffer recording failed.");
            return;
        }

        // Execute engine secondary command buffers inside the active render pass.
        // This is valid because the render pass was begun with VK_SUBPASS_CONTENTS_SECONDARY_COMMAND_BUFFERS.
        vkCmdExecuteCommands(primary, engineCount, engineCBs.data());
    }

    void VK_Renderer::EndFrame() {
        if (!m_FrameActive)
            return;

        const uint32_t frameIndex = m_VKSwapchain.GetCurrentFrame();
        VkCommandBuffer primary = m_VKSwapchain.GetPrimaryCommandBuffer(frameIndex);

        // IMPORTANT ORDER REQUIREMENT:
        // ImGuiLayer::OnEnd() must have been called BEFORE VK_Renderer::EndFrame().
        //
        // Why:
        // - ImGuiLayer::OnEnd() records into the UI secondary command buffer that we began in BeginFrame().
        // - Here we end + execute that command buffer, then we end the render pass and the primary command buffer.
        //
        // As a safety measure, clear the ImGui command buffer pointer first,
        // so if OnEnd is mistakenly called after EndFrame, it won't record into a closed cmd buffer.
        Nova::Core::ImGuiLayer& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        VkCommandBuffer uiCmd = m_VKSwapchain.GetSecondaryCommandBuffer(frameIndex, GetImGuiSecondaryIndex());
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);

        // End the UI secondary (it must be in recording state here)
        {
            VkResult res = vkEndCommandBuffer(uiCmd);
            CheckVkResult(res);
        }

        // Execute UI secondary inside the same subpass.
        vkCmdExecuteCommands(primary, 1, &uiCmd);

        // Now we can close the render pass and the primary command buffer.
        vkCmdEndRenderPass(primary);
        CheckVkResult(vkEndCommandBuffer(primary));

        VkSemaphore waitSemaphores[] = { m_VKSwapchain.GetImageAvailableSemaphore() };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { m_VKSwapchain.GetRenderFinishedSemaphore() };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &primary;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        CheckVkResult(vkQueueSubmit(
            m_VKDevice.GetGraphicsQueue(),
            1,
            &submitInfo,
            m_VKSwapchain.GetInFlightFence()
        ));

        s_HasSubmittedAtLeastOnce = true;

        // Present
        if (!m_VKSwapchain.Present(m_CurrentImageIndex)) {
            RecreateSwapchain();
        }
    }

    bool VK_Renderer::CreateImGuiDescriptorPool() {
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

        VkResult res = vkCreateDescriptorPool(m_VKDevice.GetDevice(), &pool_info, nullptr, &m_ImGuiDescriptorPool);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        NV_LOG_INFO("ImGui descriptor pool created.");
        return true;
    }

    void VK_Renderer::DestroyImGuiDescriptorPool() {
        if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_VKDevice.GetDevice(), m_ImGuiDescriptorPool, nullptr);
            m_ImGuiDescriptorPool = VK_NULL_HANDLE;
        }
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan