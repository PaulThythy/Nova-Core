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

namespace Nova::Core::Renderer::Backends::Vulkan {

    // --- Swapchain support helpers -------------------------------------------------------------

    struct SwapChainSupportDetails {
        VkSurfaceCapabilitiesKHR        capabilities{};
        std::vector<VkSurfaceFormatKHR> formats;
        std::vector<VkPresentModeKHR>   presentModes;
    };

    static SwapChainSupportDetails QuerySwapChainSupport(VkPhysicalDevice device, VkSurfaceKHR surface) {
        SwapChainSupportDetails details;

        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, surface, &details.capabilities);

        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, nullptr);
        if (formatCount != 0) {
            details.formats.resize(formatCount);
            vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formatCount, details.formats.data());
        }

        uint32_t presentModeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, nullptr);
        if (presentModeCount != 0) {
            details.presentModes.resize(presentModeCount);
            vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &presentModeCount, details.presentModes.data());
        }

        return details;
    }

    static VkSurfaceFormatKHR ChooseSwapSurfaceFormat(const std::vector<VkSurfaceFormatKHR>& availableFormats) {
        for (const auto& availableFormat : availableFormats) {
            if (availableFormat.format == VK_FORMAT_B8G8R8A8_UNORM &&
                availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
                return availableFormat;
            }
        }
        return availableFormats[0];
    }

    static VkPresentModeKHR ChoosePresentMode(const std::vector<VkPresentModeKHR>& availablePresentModes) {
        // Try mailbox (triple buffering) first
        for (const auto& availablePresentMode : availablePresentModes) {
            if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
                return availablePresentMode;
            }
        }
        // Fallback: FIFO is guaranteed to be available
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    static VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities, SDL_Window* window) {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return capabilities.currentExtent;
        }

        int width = 0, height = 0;
        SDL_GetWindowSizeInPixels(window, &width, &height);

        VkExtent2D actualExtent = {
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height)
        };

        actualExtent.width = std::clamp(actualExtent.width,
            capabilities.minImageExtent.width,
            capabilities.maxImageExtent.width);
        actualExtent.height = std::clamp(actualExtent.height,
            capabilities.minImageExtent.height,
            capabilities.maxImageExtent.height);
        return actualExtent;
    }

    // --- Queue families helper -----------------------------------------------------------------

    static bool FindQueueFamilies(VkPhysicalDevice physicalDevice,
        VkSurfaceKHR surface,
        uint32_t& outGraphics,
        uint32_t& outPresent)
    {
        outGraphics = std::numeric_limits<uint32_t>::max();
        outPresent = std::numeric_limits<uint32_t>::max();

        uint32_t qCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, nullptr);
        if (qCount == 0) return false;

        std::vector<VkQueueFamilyProperties> qProps(qCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &qCount, qProps.data());

        for (uint32_t i = 0; i < qCount; i++) {
            if ((qProps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && outGraphics == std::numeric_limits<uint32_t>::max()) {
                outGraphics = i;
            }

            VkBool32 presentSupport = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, i, surface, &presentSupport);
            if (presentSupport && outPresent == std::numeric_limits<uint32_t>::max()) {
                outPresent = i;
            }
        }

        return outGraphics != std::numeric_limits<uint32_t>::max() &&
            outPresent != std::numeric_limits<uint32_t>::max();
    }

    // --- VK_Renderer implementation ------------------------------------------------------------

    bool VK_Renderer::Create() {
        NV_LOG_INFO("Creating Vulkan renderer...");

        if (!m_VKInstance.Create())   return false;
        
        if (!PickPhysicalDevice()) return false;
        if (!CreateDevice())     return false;
        if (!CreateSwapchain())  return false;
        if (!CreateImageViews()) return false;
        if (!CreateRenderPass()) return false;
        if (!CreateFramebuffers()) return false;
        if (!CreateCommandPool())  return false;
        if (!AllocateCommandBuffers()) return false;
        if (!CreateSyncObjects()) return false;
        if (!CreateImGuiDescriptorPool()) return false;

        // ---- Init ImGui Vulkan backend ----
        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = m_VKInstance.GetInstance();
        initInfo.PhysicalDevice = m_PhysicalDevice;
        initInfo.Device = m_Device;
        initInfo.QueueFamily = m_GraphicsQueueFamily;
        initInfo.Queue = m_GraphicsQueue;
        initInfo.DescriptorPool = m_ImGuiDescriptorPool;
        initInfo.DescriptorPoolSize = 0;
        initInfo.MinImageCount = m_MinImageCount;
        initInfo.ImageCount = static_cast<uint32_t>(m_SwapchainImages.size());
        initInfo.PipelineCache = VK_NULL_HANDLE;

        initInfo.PipelineInfoMain.RenderPass = m_RenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;

        initInfo.PipelineInfoForViewports = initInfo.PipelineInfoMain;

        initInfo.UseDynamicRendering = false;

        initInfo.Allocator = nullptr;
        initInfo.CheckVkResultFn = CheckVkResult;
        initInfo.MinAllocationSize = 0;

        Nova::Core::ImGuiLayer& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanInitInfo(initInfo);

        NV_LOG_INFO("Vulkan renderer created successfully.");
        return true;
    }

    void VK_Renderer::Destroy() {
        if (m_Device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_Device);
        }

        if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(m_Device, m_ImGuiDescriptorPool, nullptr);
            m_ImGuiDescriptorPool = VK_NULL_HANDLE;
        }

        CleanupSwapchain();

        if (m_CommandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(m_Device, m_CommandPool, nullptr);
            m_CommandPool = VK_NULL_HANDLE;
        }

        if (m_Device != VK_NULL_HANDLE) {
            vkDestroyDevice(m_Device, nullptr);
            m_Device = VK_NULL_HANDLE;
        }

        m_VKInstance.Destroy();

        NV_LOG_INFO("Vulkan renderer destroyed.");
    }

    bool VK_Renderer::Resize(int w, int h) {
        (void)w; (void)h;
        // We just mark that swapchain must be recreated.
        m_FramebufferResized = true;
        return true;
    }

    void VK_Renderer::Update(float dt) {
        (void)dt;
        // Later: animations, camera, etc.
    }

    void VK_Renderer::BeginFrame() {
        if (m_FramebufferResized) {
            m_FramebufferResized = false;
            if (!RecreateSwapchain()) {
                NV_LOG_ERROR("Failed to recreate swapchain on resize");
                return;
            }
        }

        vkWaitForFences(m_Device, 1, &m_InFlightFence, VK_TRUE, UINT64_MAX);
        vkResetFences(m_Device, 1, &m_InFlightFence);

        VkResult result = vkAcquireNextImageKHR(
            m_Device,
            m_Swapchain,
            UINT64_MAX,
            m_ImageAvailableSemaphore,
            VK_NULL_HANDLE,
            &m_CurrentImageIndex
        );

        if (result == VK_ERROR_OUT_OF_DATE_KHR) {
            RecreateSwapchain();
            return;
        }
        else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
            NV_LOG_ERROR("Failed to acquire swap chain image");
            return;
        }

        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentImageIndex];

        vkResetCommandBuffer(cmd, 0);

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        CheckVkResult(vkBeginCommandBuffer(cmd, &beginInfo));

        VkClearValue clearColor{};
        clearColor.color = { { 0.1f, 0.1f, 0.12f, 1.0f } };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = m_RenderPass;
        rpBegin.framebuffer = m_SwapchainFramebuffers[m_CurrentImageIndex];
        rpBegin.renderArea.offset = { 0, 0 };
        rpBegin.renderArea.extent = m_SwapchainExtent;
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearColor;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);

        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(m_SwapchainExtent.width);
        viewport.height = static_cast<float>(m_SwapchainExtent.height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = m_SwapchainExtent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        // Inform ImGuiLayer about current command buffer
        Nova::Core::ImGuiLayer& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanCommandBuffer(cmd);
    }

    void VK_Renderer::Render() {
        // For now we only clear the screen and let ImGui draw.
        // Later you will record your 3D pipelines and meshes here,
        // before ImGuiLayer::End() appends its own draw calls.
    }

    void VK_Renderer::EndFrame() {
        VkCommandBuffer cmd = m_CommandBuffers[m_CurrentImageIndex];

        vkCmdEndRenderPass(cmd);
        CheckVkResult(vkEndCommandBuffer(cmd));

        VkSemaphore waitSemaphores[] = { m_ImageAvailableSemaphore };
        VkPipelineStageFlags waitStages[] = { VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
        VkSemaphore signalSemaphores[] = { m_RenderFinishedSemaphore };

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.waitSemaphoreCount = 1;
        submitInfo.pWaitSemaphores = waitSemaphores;
        submitInfo.pWaitDstStageMask = waitStages;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;
        submitInfo.signalSemaphoreCount = 1;
        submitInfo.pSignalSemaphores = signalSemaphores;

        CheckVkResult(vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, m_InFlightFence));

        VkPresentInfoKHR presentInfo{};
        presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        presentInfo.waitSemaphoreCount = 1;
        presentInfo.pWaitSemaphores = signalSemaphores;
        presentInfo.swapchainCount = 1;
        presentInfo.pSwapchains = &m_Swapchain;
        presentInfo.pImageIndices = &m_CurrentImageIndex;

        VkResult result = vkQueuePresentKHR(m_PresentQueue, &presentInfo);
        if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR) {
            RecreateSwapchain();
        }
        else if (result != VK_SUCCESS) {
            NV_LOG_ERROR("Failed to present swap chain image");
        }
    }

    // --- Creation helpers ----------------------------------------------------------------------

    bool VK_Renderer::PickPhysicalDevice() {
        uint32_t deviceCount = 0;
        VkResult res = vkEnumeratePhysicalDevices(m_VKInstance.GetInstance(), &deviceCount, nullptr);
        CheckVkResult(res);
        if (deviceCount == 0) {
            NV_LOG_ERROR("No Vulkan physical devices found.");
            return false;
        }

        std::vector<VkPhysicalDevice> devices(deviceCount);
        res = vkEnumeratePhysicalDevices(m_VKInstance.GetInstance(), &deviceCount, devices.data());
        CheckVkResult(res);

        for (VkPhysicalDevice dev : devices) {
            uint32_t g = 0, p = 0;
            if (!FindQueueFamilies(dev, m_VKInstance.GetSurface(), g, p)) {
                continue;
            }

            if (!HasDeviceExtension(dev, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
                continue;
            }

            m_PhysicalDevice = dev;
            m_GraphicsQueueFamily = g;
            m_PresentQueueFamily = p;

            VkPhysicalDeviceProperties props{};
            vkGetPhysicalDeviceProperties(m_PhysicalDevice, &props);

            NV_LOG_INFO((std::string("Selected GPU: ") + props.deviceName).c_str());
            LogDeviceExtensions(m_PhysicalDevice);
            return true;
        }

        NV_LOG_ERROR("No suitable Vulkan physical devices found (graphics + present + swapchain).");
        return false;
    }

    bool VK_Renderer::CreateDevice() {
        const float priority = 1.0f;

        std::vector<VkDeviceQueueCreateInfo> qcis;
        qcis.reserve(2);

        VkDeviceQueueCreateInfo qci{};
        qci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        qci.queueFamilyIndex = m_GraphicsQueueFamily;
        qci.queueCount = 1;
        qci.pQueuePriorities = &priority;
        qcis.push_back(qci);

        if (m_PresentQueueFamily != m_GraphicsQueueFamily) {
            VkDeviceQueueCreateInfo pqci{};
            pqci.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            pqci.queueFamilyIndex = m_PresentQueueFamily;
            pqci.queueCount = 1;
            pqci.pQueuePriorities = &priority;
            qcis.push_back(pqci);
        }

        std::vector<const char*> enabledDeviceExts;
        enabledDeviceExts.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);

        VkPhysicalDeviceFeatures features{};
        // Enable features if you need them (e.g. samplerAnisotropy = VK_TRUE)

        VkDeviceCreateInfo dci{};
        dci.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        dci.queueCreateInfoCount = static_cast<uint32_t>(qcis.size());
        dci.pQueueCreateInfos = qcis.data();
        dci.pEnabledFeatures = &features;
        dci.enabledExtensionCount = static_cast<uint32_t>(enabledDeviceExts.size());
        dci.ppEnabledExtensionNames = enabledDeviceExts.data();

        VkResult res = vkCreateDevice(m_PhysicalDevice, &dci, nullptr, &m_Device);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        vkGetDeviceQueue(m_Device, m_GraphicsQueueFamily, 0, &m_GraphicsQueue);
        vkGetDeviceQueue(m_Device, m_PresentQueueFamily, 0, &m_PresentQueue);

        NV_LOG_INFO("Vulkan logical device created.");
        return true;
    }

    bool VK_Renderer::CreateSwapchain() {
        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();

        SwapChainSupportDetails details = QuerySwapChainSupport(m_PhysicalDevice, m_VKInstance.GetSurface());
        if (details.formats.empty() || details.presentModes.empty()) {
            NV_LOG_ERROR("Swapchain support is incomplete.");
            return false;
        }

        VkSurfaceFormatKHR surfaceFormat = ChooseSwapSurfaceFormat(details.formats);
        VkPresentModeKHR presentMode = ChoosePresentMode(details.presentModes);
        VkExtent2D extent = ChooseSwapExtent(details.capabilities, window);

        uint32_t imageCount = details.capabilities.minImageCount + 1;
        if (details.capabilities.maxImageCount > 0 &&
            imageCount > details.capabilities.maxImageCount) {
            imageCount = details.capabilities.maxImageCount;
        }
        m_MinImageCount = details.capabilities.minImageCount;

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = m_VKInstance.GetSurface();
        ci.minImageCount = imageCount;
        ci.imageFormat = surfaceFormat.format;
        ci.imageColorSpace = surfaceFormat.colorSpace;
        ci.imageExtent = extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        uint32_t queueFamilyIndices[] = { m_GraphicsQueueFamily, m_PresentQueueFamily };

        if (m_GraphicsQueueFamily != m_PresentQueueFamily) {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = queueFamilyIndices;
        }
        else {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ci.queueFamilyIndexCount = 0;
            ci.pQueueFamilyIndices = nullptr;
        }

        ci.preTransform = details.capabilities.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = presentMode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = VK_NULL_HANDLE;

        VkResult res = vkCreateSwapchainKHR(m_Device, &ci, nullptr, &m_Swapchain);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, nullptr);
        m_SwapchainImages.resize(imageCount);
        vkGetSwapchainImagesKHR(m_Device, m_Swapchain, &imageCount, m_SwapchainImages.data());

        m_SwapchainImageFormat = surfaceFormat.format;
        m_SwapchainExtent = extent;

        NV_LOG_INFO("Swapchain created.");
        return true;
    }

    bool VK_Renderer::CreateImageViews() {
        m_SwapchainImageViews.resize(m_SwapchainImages.size());

        for (size_t i = 0; i < m_SwapchainImages.size(); ++i) {
            VkImageViewCreateInfo ci{};
            ci.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            ci.image = m_SwapchainImages[i];
            ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
            ci.format = m_SwapchainImageFormat;
            ci.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
            ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            ci.subresourceRange.baseMipLevel = 0;
            ci.subresourceRange.levelCount = 1;
            ci.subresourceRange.baseArrayLayer = 0;
            ci.subresourceRange.layerCount = 1;

            VkResult res = vkCreateImageView(m_Device, &ci, nullptr, &m_SwapchainImageViews[i]);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;
        }

        NV_LOG_INFO("Swapchain image views created.");
        return true;
    }

    bool VK_Renderer::CreateRenderPass() {
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
        dependency.srcAccessMask = 0;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 1;
        rpInfo.pAttachments = &colorAttachment;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;

        VkResult res = vkCreateRenderPass(m_Device, &rpInfo, nullptr, &m_RenderPass);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        NV_LOG_INFO("Render pass created.");
        return true;
    }

    bool VK_Renderer::CreateFramebuffers() {
        m_SwapchainFramebuffers.resize(m_SwapchainImageViews.size());

        for (size_t i = 0; i < m_SwapchainImageViews.size(); ++i) {
            VkImageView attachments[] = { m_SwapchainImageViews[i] };

            VkFramebufferCreateInfo fbInfo{};
            fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fbInfo.renderPass = m_RenderPass;
            fbInfo.attachmentCount = 1;
            fbInfo.pAttachments = attachments;
            fbInfo.width = m_SwapchainExtent.width;
            fbInfo.height = m_SwapchainExtent.height;
            fbInfo.layers = 1;

            VkResult res = vkCreateFramebuffer(m_Device, &fbInfo, nullptr, &m_SwapchainFramebuffers[i]);
            CheckVkResult(res);
            if (res != VK_SUCCESS) return false;
        }

        NV_LOG_INFO("Framebuffers created.");
        return true;
    }

    bool VK_Renderer::CreateCommandPool() {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        poolInfo.queueFamilyIndex = m_GraphicsQueueFamily;

        VkResult res = vkCreateCommandPool(m_Device, &poolInfo, nullptr, &m_CommandPool);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        NV_LOG_INFO("Command pool created.");
        return true;
    }

    bool VK_Renderer::AllocateCommandBuffers() {
        m_CommandBuffers.resize(m_SwapchainFramebuffers.size());

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = static_cast<uint32_t>(m_CommandBuffers.size());

        VkResult res = vkAllocateCommandBuffers(m_Device, &allocInfo, m_CommandBuffers.data());
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        NV_LOG_INFO("Command buffers allocated.");
        return true;
    }

    bool VK_Renderer::CreateSyncObjects() {
        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VkResult res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_ImageAvailableSemaphore);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        res = vkCreateSemaphore(m_Device, &semInfo, nullptr, &m_RenderFinishedSemaphore);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        res = vkCreateFence(m_Device, &fenceInfo, nullptr, &m_InFlightFence);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        NV_LOG_INFO("Sync objects created.");
        return true;
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

        VkResult res = vkCreateDescriptorPool(m_Device, &pool_info, nullptr, &m_ImGuiDescriptorPool);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        NV_LOG_INFO("ImGui descriptor pool created.");
        return true;
    }

    void VK_Renderer::CleanupSwapchain() {
        for (auto fb : m_SwapchainFramebuffers) {
            vkDestroyFramebuffer(m_Device, fb, nullptr);
        }
        m_SwapchainFramebuffers.clear();

        if (m_RenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(m_Device, m_RenderPass, nullptr);
            m_RenderPass = VK_NULL_HANDLE;
        }

        for (auto view : m_SwapchainImageViews) {
            vkDestroyImageView(m_Device, view, nullptr);
        }
        m_SwapchainImageViews.clear();

        if (m_Swapchain != VK_NULL_HANDLE) {
            vkDestroySwapchainKHR(m_Device, m_Swapchain, nullptr);
            m_Swapchain = VK_NULL_HANDLE;
        }
    }

    bool VK_Renderer::RecreateSwapchain() {
        vkDeviceWaitIdle(m_Device);

        CleanupSwapchain();

        if (!CreateSwapchain())      return false;
        if (!CreateImageViews())     return false;
        if (!CreateRenderPass())     return false;
        if (!CreateFramebuffers())   return false;
        if (!AllocateCommandBuffers()) return false; // reallocate to match framebuffer count

        NV_LOG_INFO("Swapchain recreated.");
        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan
