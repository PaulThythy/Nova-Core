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

static bool ReadFileBinary(const std::string& path, std::vector<char>& outData) {
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open())
        return false;

    const std::streamsize size = file.tellg();
    if (size <= 0)
        return false;

    outData.resize((size_t)size);
    file.seekg(0);
    file.read(outData.data(), size);
    return true;
}

static VkShaderModule CreateShaderModule(VkDevice device, const std::vector<char>& code) {
    if (code.empty())
        return VK_NULL_HANDLE;

    VkShaderModuleCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t*>(code.data());

    VkShaderModule module = VK_NULL_HANDLE;
    VkResult res = vkCreateShaderModule(device, &createInfo, nullptr, &module);
    if (res != VK_SUCCESS)
        return VK_NULL_HANDLE;

    return module;
}

static Nova::Core::Renderer::Backends::Vulkan::VK_Renderer::SwapchainSupportDetails QuerySwapChainSupport(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface) {
    using Details = Nova::Core::Renderer::Backends::Vulkan::VK_Renderer::SwapchainSupportDetails;

    Details details{};
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

static VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
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

static VkPresentModeKHR ChooseSwapPresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
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

static VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) {
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

namespace Nova::Core::Renderer::Backends::Vulkan {

    // ------------------------------------------------------------
    // Public API
    // ------------------------------------------------------------

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

        // Swapchain + views
        if (!CreateSwapchain() || !CreateImageViews()) {
            NV_LOG_ERROR("Failed to create swapchain or image views");
            return false;
        }

        // Render pass + framebuffers
        if (!CreateRenderPass() || !CreateFramebuffers()) {
            NV_LOG_ERROR("Failed to create render pass or framebuffers");
            return false;
        }

        // Command pool + command buffers (one primary per swapchain image)
        if (!CreateCommandPoolAndBuffers()) {
            NV_LOG_ERROR("Failed to create command pool/buffers");
            return false;
        }

        // Sync objects (single fence + semaphores)
        if (!CreateSyncObjects()) {
            NV_LOG_ERROR("Failed to create sync objects");
            return false;
        }

        // Optional: create a minimal triangle pipeline.
        // You need two SPIR-V files on disk:
        //   - shaders/triangle.vert.spv
        //   - shaders/triangle.frag.spv
        //
        // Example GLSL:
        //
        // triangle.vert
        // ------------
        // #version 450
        // vec2 pos[3] = vec2[](
        //     vec2( 0.0, -0.5),
        //     vec2( 0.5,  0.5),
        //     vec2(-0.5,  0.5)
        // );
        // void main() { gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0); }
        //
        // triangle.frag
        // ------------
        // #version 450
        // layout(location=0) out vec4 outColor;
        // void main() { outColor = vec4(1, 0, 0, 1); }
        //
        // Compile:
        //   glslc triangle.vert -o triangle.vert.spv
        //   glslc triangle.frag -o triangle.frag.spv
        //
        CreateTrianglePipeline();

        // ImGui descriptor pool (kept, but ImGui rendering will be "best effort")
        // If the app calls ImGuiLayer::OnEnd AFTER EndFrame(), we force the cmd buffer to NULL,
        // so it will not try to record into a closed command buffer.
        if (!CreateImGuiDescriptorPool()) {
            NV_LOG_WARN("Failed to create ImGui descriptor pool (ImGui may not render)");
        }
        else {
            ImGui_ImplVulkan_InitInfo initInfo{};
            initInfo.ApiVersion = VK_API_VERSION_1_3;
            initInfo.Instance = m_VKInstance.GetInstance();
            initInfo.PhysicalDevice = m_VKDevice.GetPhysicalDevice();
            initInfo.Device = m_VKDevice.GetDevice();
            initInfo.QueueFamily = m_VKDevice.GetGraphicsQueueFamily();
            initInfo.Queue = m_VKDevice.GetGraphicsQueue();
            initInfo.DescriptorPool = m_ImGuiDescriptorPool;
            initInfo.DescriptorPoolSize = 0;
            initInfo.MinImageCount = m_Frames.size();;
            initInfo.ImageCount = m_Frames.size();;
            initInfo.PipelineCache = VK_NULL_HANDLE;

            initInfo.PipelineInfoMain.RenderPass = m_RenderPass;
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
        }

        s_FrameActive = false;

        NV_LOG_INFO("Vulkan renderer created successfully (minimal mode).");
        return true;
    }

    void VK_Renderer::Destroy() {
        NV_LOG_INFO("Destroying Vulkan renderer...");

        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_VKDevice.GetDevice());
        }

        DestroyTrianglePipeline();

        DestroyImGuiDescriptorPool();

        CleanupSwapchain();

        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_VKDevice.GetDevice(), m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        DestroySyncObjects();

        if (m_CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_VKDevice.GetDevice(), m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }

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

        auto& fs = m_FrameSync[m_CurrentFrame];

        // Attendre que cette frame-in-flight soit libre
        CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence, VK_TRUE, UINT64_MAX));

        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) return;

        if (m_FramebufferResized) {
            m_FramebufferResized = false;
            if (!RecreateSwapchain()) return;
        }

        // Acquire avec le sémaphore de la frame courante
        VkResult acquireRes = vkAcquireNextImageKHR(
            m_VKDevice.GetDevice(),
            m_Swapchain,
            UINT64_MAX,
            fs.m_ImageAvailableSemaphore,
            VK_NULL_HANDLE,
            &m_CurrentImageIndex
        );

        if (acquireRes == VK_ERROR_OUT_OF_DATE_KHR) { m_FramebufferResized = true; return; }
        if (acquireRes != VK_SUCCESS && acquireRes != VK_SUBOPTIMAL_KHR) { NV_LOG_ERROR("vkAcquireNextImageKHR failed"); return; }

        // Si cette image swapchain est déjà "in flight", attendre la fence associée
        if (m_ImagesInFlight[m_CurrentImageIndex] != VK_NULL_HANDLE) {
            CheckVkResult(vkWaitForFences(m_VKDevice.GetDevice(), 1, &m_ImagesInFlight[m_CurrentImageIndex], VK_TRUE, UINT64_MAX));
        }
        // Marquer cette image comme utilisée par la fence de la frame courante
        m_ImagesInFlight[m_CurrentImageIndex] = fs.m_InFlightFence;

        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentImageIndex];
        CheckVkResult(vkResetCommandBuffer(cmd, 0));

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo));

        VkClearValue clearColor{};
        clearColor.color = { { 0.1f, 0.1f, 0.12f, 1.0f } };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_RenderPass;
        rpBegin.framebuffer = m_Frames[m_CurrentImageIndex].m_VKFramebuffer;
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = m_SwapchainExtent;
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = (float)m_SwapchainExtent.width;
        viewport.height = (float)m_SwapchainExtent.height;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_SwapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // ImGui cmd buffer pour la frame
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(cmd);
        }

        s_FrameActive = true;
    }

    void VK_Renderer::Render() {
        if (!s_FrameActive)
            return;

        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentImageIndex];

        // Minimal triangle draw (if pipeline exists).
        // If you haven't provided shaders, this simply does nothing (you still get a clear color).
        if (m_TrianglePipeline != VK_NULL_HANDLE) {
            vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_TrianglePipeline);
            vkCmdDraw(cmd, 3, 1, 0, 0);
        }

        // NOTE:
        // ImGui rendering is NOT done here. Your ImGuiLayer::OnEnd() will call
        // ImGui_ImplVulkan_RenderDrawData(drawData, cmd) when it runs.
        // This is why the render pass must still be active until EndFrame().
    }

    void VK_Renderer::EndFrame() {
        if (!s_FrameActive) return;

        auto& fs = m_FrameSync[m_CurrentFrame];

        // Empêcher ImGui d'écrire après
        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        }

        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentImageIndex];
        vkCmdEndRenderPass(cmd);
        CheckVkResult(vkEndCommandBuffer(cmd));

        // Reset fence juste avant submit
        CheckVkResult(vkResetFences(m_VKDevice.GetDevice(), 1, &fs.m_InFlightFence));

        VkSemaphore waitSemaphores[] = { fs.m_ImageAvailableSemaphore };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { fs.m_RenderFinishedSemaphore };

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

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_Swapchain;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        VkResult presentRes = vkQueuePresentKHR(m_VKDevice.GetPresentQueue(), &presentInfo);
        if (presentRes == VK_ERROR_OUT_OF_DATE_KHR || presentRes == VK_SUBOPTIMAL_KHR) {
            m_FramebufferResized = true;
        }
        else if (presentRes != VK_SUCCESS) {
            NV_LOG_ERROR("vkQueuePresentKHR failed");
        }

        s_FrameActive = false;

        // next frame-in-flight
        m_CurrentFrame = (m_CurrentFrame + 1) % MAX_FRAMES_IN_FLIGHT;
    }

    // ------------------------------------------------------------
    // Swapchain management (minimal)
    // ------------------------------------------------------------

    bool VK_Renderer::RecreateSwapchain() {
        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        int w = 0, h = 0;
        SDL_GetWindowSizeInPixels(window, &w, &h);

        if (w <= 0 || h <= 0)
            return false;

        vkDeviceWaitIdle(m_VKDevice.GetDevice());

        CleanupSwapchain();

        if (!CreateSwapchain() || !CreateImageViews() || !CreateFramebuffers() || !RecreateCommandBuffers()) {
            NV_LOG_ERROR("Failed to recreate swapchain resources");
            return false;
        }

        // Triangle pipeline stays valid because we keep the same render pass.
        // If your render pass changes, you must recreate the pipeline too.

        return true;
    }

    void VK_Renderer::CleanupSwapchain() {
        for (auto& f : m_Frames) {
            if (f.m_VKFramebuffer) vkDestroyFramebuffer(m_VKDevice.GetDevice(), f.m_VKFramebuffer, nullptr);
            if (f.m_VKImageView)   vkDestroyImageView(m_VKDevice.GetDevice(), f.m_VKImageView, nullptr);
            f.m_VKFramebuffer = VK_NULL_HANDLE;
            f.m_VKImageView = VK_NULL_HANDLE;
            f.m_VKImage = VK_NULL_HANDLE;
        }
        m_Frames.clear();

        if (!m_CommandBuffers.empty() && m_CommandPool != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(m_VKDevice.GetDevice(), m_CommandPool,
                (uint32_t)m_CommandBuffers.size(), m_CommandBuffers.data());
            m_CommandBuffers.clear();
        }

        if (m_Swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_VKDevice.GetDevice(), m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }

        m_ImagesInFlight.clear();
        m_CurrentImageIndex = 0;
    }

    bool VK_Renderer::CreateSwapchain() {
        auto swapChainSupport = QuerySwapChainSupport(m_VKDevice.GetPhysicalDevice(), m_VKInstance.GetSurface());

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(swapChainSupport.m_Formats);
        VkPresentModeKHR   presentMode = ChooseSwapPresentMode(swapChainSupport.m_PresentModes);
        VkExtent2D         extent = ChooseSwapExtent(swapChainSupport.m_Capabilities);

        uint32_t imageCount = std::max(
            swapChainSupport.m_Capabilities.minImageCount,
            MAX_FRAMES_IN_FLIGHT
        );
        if (swapChainSupport.m_Capabilities.maxImageCount > 0 &&
            imageCount > swapChainSupport.m_Capabilities.maxImageCount) {
            imageCount = swapChainSupport.m_Capabilities.maxImageCount;
        }

        VkSwapchainCreateInfoKHR createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        createInfo.surface = m_VKInstance.GetSurface();
        createInfo.minImageCount = imageCount;
        createInfo.imageFormat = surfaceFormat.format;
        createInfo.imageColorSpace = surfaceFormat.colorSpace;
        createInfo.imageExtent = extent;
        createInfo.imageArrayLayers = 1;
        createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = { m_VKDevice.GetGraphicsQueueFamily(), m_VKDevice.GetPresentQueueFamily() };
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

        VkResult res = vkCreateSwapchainKHR(m_VKDevice.GetDevice(), &createInfo, nullptr, &m_Swapchain);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        uint32_t actualImageCount = 0;
        vkGetSwapchainImagesKHR(m_VKDevice.GetDevice(), m_Swapchain, &actualImageCount, nullptr);

        std::vector<VkImage> images(actualImageCount);
        vkGetSwapchainImagesKHR(m_VKDevice.GetDevice(), m_Swapchain, &actualImageCount, images.data());

        m_Frames.clear();
        m_Frames.resize(actualImageCount);
        for (uint32_t i = 0; i < actualImageCount; ++i) {
            m_Frames[i].m_VKImage = images[i];
        }

        m_SwapchainImageFormat = surfaceFormat.format;
        m_SwapchainExtent = extent;

        // fence-per-image tracking
        m_ImagesInFlight.assign(m_Frames.size(), VK_NULL_HANDLE);

        NV_LOG_INFO(("Swapchain created with " + std::to_string(actualImageCount) + " images.").c_str());
        return true;
    }

    bool VK_Renderer::CreateImageViews() {
        for (auto& f : m_Frames) f.m_VKImageView = VK_NULL_HANDLE;

        for (size_t i = 0; i < m_Frames.size(); i++) {
            VkImageViewCreateInfo createInfo{};
            createInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            createInfo.image = m_Frames[i].m_VKImage;
            createInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            createInfo.format = m_SwapchainImageFormat;
            createInfo.components = { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                                      VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY };
            createInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            createInfo.subresourceRange.baseMipLevel = 0;
            createInfo.subresourceRange.levelCount = 1;
            createInfo.subresourceRange.baseArrayLayer = 0;
            createInfo.subresourceRange.layerCount = 1;

            VkResult res = vkCreateImageView(m_VKDevice.GetDevice(), &createInfo, nullptr, &m_Frames[i].m_VKImageView);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;
        }
        return true;
    }

    bool VK_Renderer::CreateRenderPass() {
        // Create once. We keep it alive across swapchain recreations (format usually stays the same).
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

        VkResult res = vkCreateRenderPass(m_VKDevice.GetDevice(), &renderPassInfo, nullptr, &m_RenderPass);
        CheckVkResult(res);
        return (res == VK_SUCCESS);
    }

    bool VK_Renderer::CreateFramebuffers() {
        for (auto& f : m_Frames) f.m_VKFramebuffer = VK_NULL_HANDLE;

        for (size_t i = 0; i < m_Frames.size(); i++) {
            VkImageView attachments[] = { m_Frames[i].m_VKImageView };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_RenderPass;
            framebufferInfo.attachmentCount = 1;
            framebufferInfo.pAttachments = attachments;
            framebufferInfo.width = m_SwapchainExtent.width;
            framebufferInfo.height = m_SwapchainExtent.height;
            framebufferInfo.layers = 1;

            VkResult res = vkCreateFramebuffer(m_VKDevice.GetDevice(), &framebufferInfo, nullptr, &m_Frames[i].m_VKFramebuffer);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;
        }
        return true;
    }

    bool VK_Renderer::CreateCommandPoolAndBuffers() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.queueFamilyIndex = m_VKDevice.GetGraphicsQueueFamily();
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

        VkResult res = vkCreateCommandPool(m_VKDevice.GetDevice(), &poolInfo, nullptr, &m_CommandPool);
        CheckVkResult(res);
        if (res != VK_SUCCESS)
            return false;

        return RecreateCommandBuffers();
    }

    bool VK_Renderer::RecreateCommandBuffers() {
        if (m_CommandPool == VK_NULL_HANDLE)
            return false;

        m_CommandBuffers.resize(m_Frames.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = (uint32_t)m_CommandBuffers.size();

        VkResult res = vkAllocateCommandBuffers(m_VKDevice.GetDevice(), &allocInfo, m_CommandBuffers.data());
        CheckVkResult(res);
        return (res == VK_SUCCESS);
    }

    bool VK_Renderer::CreateSyncObjects() {
        VkSemaphoreCreateInfo semInfo{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };

        VkFenceCreateInfo fenceInfo{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            VkResult res = vkCreateSemaphore(m_VKDevice.GetDevice(), &semInfo, nullptr, &m_FrameSync[i].m_ImageAvailableSemaphore);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;

            res = vkCreateSemaphore(m_VKDevice.GetDevice(), &semInfo, nullptr, &m_FrameSync[i].m_RenderFinishedSemaphore);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;

            res = vkCreateFence(m_VKDevice.GetDevice(), &fenceInfo, nullptr, &m_FrameSync[i].m_InFlightFence);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;
        }

        // fence-per-swapchain-image tracking
        m_ImagesInFlight.assign(m_Frames.size(), VK_NULL_HANDLE);

        m_CurrentFrame = 0;
        return true;
    }

    void VK_Renderer::DestroySyncObjects() {
        for (uint32_t i = 0; i < MAX_FRAMES_IN_FLIGHT; ++i) {
            if (m_FrameSync[i].m_ImageAvailableSemaphore) {
                vkDestroySemaphore(m_VKDevice.GetDevice(), m_FrameSync[i].m_ImageAvailableSemaphore, nullptr);
                m_FrameSync[i].m_ImageAvailableSemaphore = VK_NULL_HANDLE;
            }
            if (m_FrameSync[i].m_RenderFinishedSemaphore) {
                vkDestroySemaphore(m_VKDevice.GetDevice(), m_FrameSync[i].m_RenderFinishedSemaphore, nullptr);
                m_FrameSync[i].m_RenderFinishedSemaphore = VK_NULL_HANDLE;
            }
            if (m_FrameSync[i].m_InFlightFence) {
                vkDestroyFence(m_VKDevice.GetDevice(), m_FrameSync[i].m_InFlightFence, nullptr);
                m_FrameSync[i].m_InFlightFence = VK_NULL_HANDLE;
            }
        }
        m_ImagesInFlight.clear();
    }

    // ------------------------------------------------------------
    // ImGui descriptor pool (unchanged, minimal)
    // ------------------------------------------------------------

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
        return (res == VK_SUCCESS);
    }

    void VK_Renderer::DestroyImGuiDescriptorPool() {
        if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_VKDevice.GetDevice(), m_ImGuiDescriptorPool, nullptr);
            m_ImGuiDescriptorPool = VK_NULL_HANDLE;
        }
    }

    // ------------------------------------------------------------
    // Triangle pipeline (optional)
    // ------------------------------------------------------------

    void VK_Renderer::CreateTrianglePipeline() {
        // If already created, do nothing.
        if (m_TrianglePipeline != VK_NULL_HANDLE)
            return;

        // You can change these paths to whatever your project uses.
        const std::string vertPath = "C:/Users/Pault/Desktop/Nova/Nova-Core/shaders/program.vert.spv";
        const std::string fragPath = "C:/Users/Pault/Desktop/Nova/Nova-Core/shaders/program.frag.spv";


        std::vector<char> vertCode, fragCode;
        if (!ReadFileBinary(vertPath, vertCode) || !ReadFileBinary(fragPath, fragCode)) {
            NV_LOG_WARN("Triangle shaders not found (shaders/triangle.vert.spv, shaders/triangle.frag.spv). Rendering will only clear the screen.");
            return;
        }

        VkShaderModule vertModule = CreateShaderModule(m_VKDevice.GetDevice(), vertCode);
        VkShaderModule fragModule = CreateShaderModule(m_VKDevice.GetDevice(), fragCode);

        if (vertModule == VK_NULL_HANDLE || fragModule == VK_NULL_HANDLE) {
            NV_LOG_WARN("Failed to create shader modules. Rendering will only clear the screen.");
            if (vertModule) vkDestroyShaderModule(m_VKDevice.GetDevice(), vertModule, nullptr);
            if (fragModule) vkDestroyShaderModule(m_VKDevice.GetDevice(), fragModule, nullptr);
            return;
        }

        VkPipelineShaderStageCreateInfo vertStage{};
        vertStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        vertStage.stage = VK_SHADER_STAGE_VERTEX_BIT;
        vertStage.module = vertModule;
        vertStage.pName = "main";

        VkPipelineShaderStageCreateInfo fragStage{};
        fragStage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        fragStage.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        fragStage.module = fragModule;
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

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;

        VkResult res = vkCreatePipelineLayout(m_VKDevice.GetDevice(), &layoutInfo, nullptr, &m_TrianglePipelineLayout);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            NV_LOG_WARN("Failed to create pipeline layout. Rendering will only clear the screen.");
            vkDestroyShaderModule(m_VKDevice.GetDevice(), vertModule, nullptr);
            vkDestroyShaderModule(m_VKDevice.GetDevice(), fragModule, nullptr);
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
        pipe.renderPass = m_RenderPass;
        pipe.subpass = 0;

        res = vkCreateGraphicsPipelines(m_VKDevice.GetDevice(), VK_NULL_HANDLE, 1, &pipe, nullptr, &m_TrianglePipeline);
        CheckVkResult(res);

        vkDestroyShaderModule(m_VKDevice.GetDevice(), vertModule, nullptr);
        vkDestroyShaderModule(m_VKDevice.GetDevice(), fragModule, nullptr);

        if (res != VK_SUCCESS) {
            NV_LOG_WARN("Failed to create triangle pipeline. Rendering will only clear the screen.");
            DestroyTrianglePipeline();
        }
        else {
            NV_LOG_INFO("Triangle pipeline created.");
        }
    }

    void VK_Renderer::DestroyTrianglePipeline() {
        if (m_TrianglePipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(m_VKDevice.GetDevice(), m_TrianglePipeline, nullptr);
            m_TrianglePipeline = VK_NULL_HANDLE;
        }
        if (m_TrianglePipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(m_VKDevice.GetDevice(), m_TrianglePipelineLayout, nullptr);
            m_TrianglePipelineLayout = VK_NULL_HANDLE;
        }
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan