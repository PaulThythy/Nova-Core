#include "Renderer/Backends/Vulkan/VK_Renderer.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"

#include "Core/Application.h"
#include "Core/Assert.h"
#include "Core/Window.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    std::vector<VkImageView> VK_Renderer::GetSwapchainImageViews() const {
        std::vector<VkImageView> views;
        const auto& images = m_VKSwapchain.GetImages();
        views.reserve(images.size());
        for (const auto& img : images)
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

    void VK_Renderer::WarnIfNoRenderGraph(const char* operation) const {
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

        if (!m_MemoryAllocator.Create(
                m_VKInstance.GetInstance(),
                m_VKDevice.GetPhysicalDevice(),
                m_VKDevice.GetDevice(),
                VK_API_VERSION_1_3)) {
            NV_LOG_ERROR("VK_MemoryAllocator::Create failed");
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

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.DestroyImGuiBackend(GraphicsAPI::Vulkan);

        for (auto& [key, mesh] : m_MeshCache) {
            if (mesh) mesh->Release();
        }
        m_MeshCache.clear();

        m_RenderGraph.reset();

        m_VKSwapchain.Destroy();
        m_MemoryAllocator.Destroy();
        m_VKDevice.Destroy();
        m_VKInstance.Destroy();

        m_FrameActive = false;
        NV_LOG_INFO("Vulkan renderer destroyed.");
    }

    void VK_Renderer::SetRenderGraph(std::unique_ptr<RHI::IRenderGraph> graph) {
        if (auto* vkGraph = GetVKRenderGraph())
            vkGraph->Destroy();

        m_RenderGraph = std::move(graph);
        if (!m_RenderGraph) return;

        auto* vkGraph = dynamic_cast<VK_RenderGraph*>(m_RenderGraph.get());
        if (!vkGraph) {
            NV_LOG_ERROR("VK_Renderer::SetRenderGraph - render graph is not a Vulkan implementation");
            m_RenderGraph.reset();
            return;
        }

        if (!vkGraph->Create(*this)) {
            NV_LOG_ERROR("VK_Renderer::SetRenderGraph - failed to compile render graph");
            m_RenderGraph.reset();
        }
    }

    void VK_Renderer::Update(float dt) {
        if (m_RenderGraph)
            m_RenderGraph->ReloadChangedShaders();
        (void)dt;
    }

    bool VK_Renderer::Resize(int w, int h) {
        if (w <= 0 || h <= 0)
            return true;

        if (auto* vkGraph = GetVKRenderGraph())
            return vkGraph->Resize(static_cast<uint32_t>(w), static_cast<uint32_t>(h));

        return true;
    }

    void VK_Renderer::BeginFrame() {
        m_FrameActive = false;

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        imguiLayer.SetVulkanBeforeRenderCallback({});

        if (!m_RenderGraph) {
            WarnIfNoRenderGraph("BeginFrame skipped");
            return;
        }

        auto* vkGraph = GetVKRenderGraph();
        if (!vkGraph) {
            WarnIfNoRenderGraph("BeginFrame skipped");
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
            if (auto* vkGraph = GetVKRenderGraph())
                vkGraph->ExecutePresentPasses();
        });

        m_FrameActive = true;
        m_RenderGraph->OnBeginFrame();
    }

    void VK_Renderer::ExecuteScenePasses() {
        if (!m_FrameActive || !m_RenderGraph) return;
        if (auto* vkGraph = GetVKRenderGraph())
            vkGraph->ExecuteScenePasses();
    }

    void VK_Renderer::Draw(const RHI::RHI_DrawCommand& cmd) {
        if (!m_FrameActive || !cmd.m_Mesh) return;

        auto vkMesh = GetOrUploadMesh(cmd.m_Mesh);
        if (!vkMesh) return;

        VkCommandBuffer vkCmd = GetCurrentCommandBuffer();
        vkMesh->SetCommandBuffer(vkCmd);
        vkMesh->Bind();
        vkCmdDraw(vkCmd, cmd.m_VertexCount, cmd.m_InstanceCount, cmd.m_FirstVertex, cmd.m_FirstInstance);
    }

    void VK_Renderer::DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        if (!m_FrameActive || !cmd.m_Mesh) return;

        auto vkMesh = GetOrUploadMesh(cmd.m_Mesh);
        if (!vkMesh) return;

        if (cmd.m_IndexType != RHI::RHI_IndexType::UInt32) {
            NV_LOG_WARN("VK_Renderer::DrawIndexed currently supports only UInt32 index buffers.");
            return;
        }

        VkCommandBuffer vkCmd = GetCurrentCommandBuffer();
        vkMesh->SetCommandBuffer(vkCmd);
        vkMesh->Bind();
        vkCmdDrawIndexed(vkCmd, cmd.m_IndexCount, cmd.m_InstanceCount, cmd.m_FirstIndex, cmd.m_VertexOffset, cmd.m_FirstInstance);
    }

    void* VK_Renderer::GetTextureImGuiID(RHI::RHI_TextureHandle handle) const {
        if (m_RenderGraph)
            return m_RenderGraph->GetTextureImGuiID(handle);
        return nullptr;
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
            &m_MemoryAllocator,
            m_VKDevice.GetDevice(),
            m_VKSwapchain.GetCommandPool(),
            m_VKDevice.GetGraphicsQueue());
        vkMesh->Upload(*cpuMesh);

        m_MeshCache[cpuMesh.get()] = vkMesh;
        return vkMesh;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan