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

#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <limits>
#include <string>
#include <vector>
#include <optional>
#include <utility>
#include <filesystem>

#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    static VkShaderStageFlags ToVkStageFlags(RHI::RHI_ShaderStageMask mask) {
        VkShaderStageFlags out = 0;
        const uint32_t m = static_cast<uint32_t>(mask);
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Vertex)) out |= VK_SHADER_STAGE_VERTEX_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Fragment)) out |= VK_SHADER_STAGE_FRAGMENT_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Geometry)) out |= VK_SHADER_STAGE_GEOMETRY_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::TessCtrl)) out |= VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::TessEval)) out |= VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT;
        if (m & static_cast<uint32_t>(RHI::RHI_ShaderStageMask::Compute)) out |= VK_SHADER_STAGE_COMPUTE_BIT;
        return out;
    }

    static VkDescriptorType ToVkDescriptorType(const RHI::RHI_BindingInfo& b) {
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

    // Flag the engine MVP and Material constant buffers (resolved by reflection name) as dynamic
    // uniform buffers, wherever Slang reflection placed them.
    static void MarkEngineDynamicBuffers(RHI::RHI_ProgramReflection& refl) {
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

    // Build a Vulkan descriptor set layout for a single reflection set. Set index and bindings come
    // straight from Slang reflection; nothing is hardcoded.
    static bool CreateDescriptorSetLayoutFromReflection(
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

    bool VK_Renderer::TransitionViewportImageToShaderRead() {
        if (m_ViewportImage == VK_NULL_HANDLE)
            return false;

        VkCommandPool commandPool = m_VKSwapchain.GetCommandPool();
        VkDevice device = m_VKDevice.GetDevice();
        VkQueue graphicsQueue = m_VKDevice.GetGraphicsQueue();
        if (commandPool == VK_NULL_HANDLE || device == VK_NULL_HANDLE || graphicsQueue == VK_NULL_HANDLE)
            return false;

        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.commandPool = commandPool;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        VkResult res = vkAllocateCommandBuffers(device, &allocInfo, &cmd);
        CheckVkResult(res);
        if (res != VK_SUCCESS)
            return false;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        res = vkBeginCommandBuffer(cmd, &beginInfo);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &cmd);
            return false;
        }

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
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

        res = vkEndCommandBuffer(cmd);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            vkFreeCommandBuffers(device, commandPool, 1, &cmd);
            return false;
        }

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        res = vkQueueSubmit(graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        CheckVkResult(res);
        if (res == VK_SUCCESS) {
            res = vkQueueWaitIdle(graphicsQueue);
            CheckVkResult(res);
        }

        vkFreeCommandBuffers(device, commandPool, 1, &cmd);
        return (res == VK_SUCCESS);
    }

    bool VK_Renderer::Create(const RHI::RHI_SwapchainDesc& desc) {
        m_SwapchainDesc = desc;
        NV_LOG_INFO("Creating Vulkan renderer (instance, device, swapchain)...");

        const std::vector<const char*> deviceExtensions = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME
        };

        if (!m_VKInstance.Create(desc.m_CreateSurface)) {
            NV_LOG_ERROR("VK_Instance::Create failed");
            return false;
        }

        if (!m_VKDevice.Create(
                m_VKInstance.GetInstance(),
                m_VKInstance.GetSurface(),
                deviceExtensions)) {
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
        NV_LOG_INFO("Vulkan renderer core created (render resources deferred until first frame).");
        return true;
    }

    void VK_Renderer::Destroy() {
        NV_LOG_INFO("Destroying Vulkan renderer...");

        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_VKDevice.GetDevice());
        }

        // Viewport framebuffer first: frees its descriptor set from the ImGui pool.
        DestroyViewportFramebuffer();

        // Shutdown ImGui's Vulkan backend before the descriptor pool and device are destroyed.
        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.DestroyImGuiBackend(GraphicsAPI::Vulkan);

        for (VkPipeline p : m_FullscreenPipelines)
            vkDestroyPipeline(m_VKDevice.GetDevice(), p, nullptr);
        m_FullscreenPipelines.clear();

        if (m_VKDevice.GetDevice() != VK_NULL_HANDLE) {
            VkDescriptorPool pool = m_ImGuiDescriptorPool;
            for (auto& [pipeline, state] : m_FullscreenPipelineState) {
                for (VkDescriptorSet ds : state.ownedSets) {
                    if (ds != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
                        vkFreeDescriptorSets(m_VKDevice.GetDevice(), pool, 1, &ds);
                }
                if (state.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_VKDevice.GetDevice(), state.layout, nullptr);
                for (VkDescriptorSetLayout l : state.setLayouts)
                    if (l != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_VKDevice.GetDevice(), l, nullptr);
            }
        }
        m_FullscreenPipelineState.clear();

        for (auto& [key, mesh] : m_MeshCache) {
            if (mesh) {
                mesh->Release();
            }
        }
        m_MeshCache.clear();

        DestroyRenderResources();
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
        m_ImGuiSwapchainPassBegun = false;

        {
            auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
            imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
            imguiLayer.SetVulkanBeforeRenderCallback({});
        }

        if (!InitRenderResources())
            return;

        SDL_Window* window = Nova::Core::Application::Get().GetWindow().GetSDLWindow();
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED)
            return;

        if (m_FramebufferResized) {
            m_FramebufferResized = false;
            if (!m_VKSwapchain.RecreateSwapchain())
                return;
            if (!RecreateSwapchainRenderTargets())
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
        clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
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
            rpBegin.renderPass = m_ViewportRenderPass;
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

            if (m_ModelPipeline != VK_NULL_HANDLE)
                vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_ModelPipeline);

            m_RenderedToViewportThisFrame = true;
        } else {
            // Render directly to swapchain
            VkRenderPassBeginInfo rpBegin{};
            rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpBegin.renderPass = m_BackBufferRenderPass;
            rpBegin.framebuffer = m_SwapchainFramebuffers[imageIndex];
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

        if (m_Shader)
            m_Shader->ResetDynamicUBOs();

        m_FrameActive = true;
    }

    void VK_Renderer::BeginScene(const glm::mat4& view, const glm::mat4& proj) {
        if (!m_Shader || !m_Shader->IsValid()) return;

        m_Shader->SetParameter("view", view);
        m_Shader->SetParameter("proj", proj);
        m_Shader->SetParameter("viewProj", proj * view);
        m_Shader->SetParameter("invViewProj", glm::inverse(proj * view));
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

    void VK_Renderer::PrepareForImGui() {
        if (!m_FrameActive)
            return;
        if (m_RenderedToViewportThisFrame) {
            BeginImGuiRenderPass();
            return;
        }
        const uint32_t imageIndex = m_VKSwapchain.GetAcquiredImageIndex();
        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[imageIndex];
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

    std::shared_ptr<VK_Mesh> VK_Renderer::GetOrUploadMesh(
        const std::shared_ptr<Renderer::RHI::RHI_Mesh>& cpuMesh)
    {
        NV_ASSERT_MSG(cpuMesh, "VK_Renderer::GetOrUploadMesh received a null mesh.");
        if (!cpuMesh)
            return nullptr;

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
        VkFormat colorFormat = m_VKSwapchain.GetImageFormat();
        VkFormat depthFormat = m_DepthFormat;

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
        fbInfo.renderPass = m_ViewportRenderPass;
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

        // Keep descriptor layout contract valid immediately after creation.
        if (m_ViewportDescriptorSet != VK_NULL_HANDLE) {
            if (TransitionViewportImageToShaderRead()) {
                m_ViewportImageFirstUse = false;
            }
            else {
                NV_LOG_WARN("VK_Renderer: failed to pre-transition viewport image to SHADER_READ_ONLY_OPTIMAL.");
            }
        }
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
            VkDescriptorPool pool = m_ImGuiDescriptorPool;
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
        rpBegin.renderPass = m_BackBufferRenderPass;
        rpBegin.framebuffer = m_SwapchainFramebuffers[imageIndex];
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

        m_ImGuiSwapchainPassBegun = true;
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
        presentInfo.pImageIndices = &imageIndex; // index of the acquired swapchain image

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

    // =========================================================================
    // Fullscreen quad shader (used by EditorLayer for grid, etc.)
    // =========================================================================

    void VK_Renderer::CreateFullscreenQuadBuffer() {
        VkDevice device = m_VKDevice.GetDevice();
        if (device == VK_NULL_HANDLE)
            return;

        static const float kQuadVerts[] = {
            -1.f, -1.f, 0.f, 0.f,
             1.f, -1.f, 1.f, 0.f,
            -1.f,  1.f, 0.f, 1.f,
            -1.f,  1.f, 0.f, 1.f,
             1.f, -1.f, 1.f, 0.f,
             1.f,  1.f, 1.f, 1.f,
        };

        VkBufferCreateInfo bufInfo{};
        bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufInfo.size = sizeof(kQuadVerts);
        bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult res = vkCreateBuffer(device, &bufInfo, nullptr, &m_FullscreenQuadBuffer);
        CheckVkResult(res);
        if (res != VK_SUCCESS)
            return;

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, m_FullscreenQuadBuffer, &memReq);

        const VkPhysicalDeviceMemoryProperties& memProps = m_VKDevice.GetMemoryProperties();
        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
                (memProps.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
                memTypeIndex = i;
                break;
            }
        }
        if (memTypeIndex == UINT32_MAX) {
            NV_LOG_WARN("CreateFullscreenQuadBuffer: no host-visible coherent memory type");
            vkDestroyBuffer(device, m_FullscreenQuadBuffer, nullptr);
            m_FullscreenQuadBuffer = VK_NULL_HANDLE;
            return;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memTypeIndex;
        res = vkAllocateMemory(device, &allocInfo, nullptr, &m_FullscreenQuadMemory);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            vkDestroyBuffer(device, m_FullscreenQuadBuffer, nullptr);
            m_FullscreenQuadBuffer = VK_NULL_HANDLE;
            return;
        }

        vkBindBufferMemory(device, m_FullscreenQuadBuffer, m_FullscreenQuadMemory, 0);

        void* data = nullptr;
        res = vkMapMemory(device, m_FullscreenQuadMemory, 0, sizeof(kQuadVerts), 0, &data);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            vkFreeMemory(device, m_FullscreenQuadMemory, nullptr);
            m_FullscreenQuadMemory = VK_NULL_HANDLE;
            vkDestroyBuffer(device, m_FullscreenQuadBuffer, nullptr);
            m_FullscreenQuadBuffer = VK_NULL_HANDLE;
            return;
        }
        std::memcpy(data, kQuadVerts, sizeof(kQuadVerts));
        vkUnmapMemory(device, m_FullscreenQuadMemory);
    }

    void VK_Renderer::DestroyFullscreenQuadBuffer() {
        VkDevice device = m_VKDevice.GetDevice();
        if (device == VK_NULL_HANDLE)
            return;
        if (m_FullscreenQuadBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(device, m_FullscreenQuadBuffer, nullptr);
            m_FullscreenQuadBuffer = VK_NULL_HANDLE;
        }
        if (m_FullscreenQuadMemory != VK_NULL_HANDLE) {
            vkFreeMemory(device, m_FullscreenQuadMemory, nullptr);
            m_FullscreenQuadMemory = VK_NULL_HANDLE;
        }
    }

    RHI::RHI_Shaders* VK_Renderer::CreateFullscreenShader(
        const RHI::RHI_ShaderCompileInput& vertIn,
        const RHI::RHI_ShaderCompileInput& fragIn)
    {
        RHI::RHI_ShaderCompileResult vertOut = RHI::RHI_ShaderCompiler::Compile(vertIn);
        if (!vertOut.m_Success) {
            NV_LOG_WARN(("CreateFullscreenShader vertex compile failed:\n" + vertOut.m_Log).c_str());
            return nullptr;
        }
        RHI::RHI_ShaderCompileResult fragOut = RHI::RHI_ShaderCompiler::Compile(fragIn);
        if (!fragOut.m_Success) {
            NV_LOG_WARN(("CreateFullscreenShader fragment compile failed:\n" + fragOut.m_Log).c_str());
            return nullptr;
        }

        VkDevice device = m_VKDevice.GetDevice();

        VK_ShaderModule vertModule, fragModule;
        if (!vertModule.Create(device, vertOut.m_Binary) ||
            !fragModule.Create(device, fragOut.m_Binary))
        {
            NV_LOG_WARN("CreateFullscreenShader: failed to create shader modules");
            return nullptr;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule.GetModule();
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule.GetModule();
        stages[1].pName  = "main";

        VkVertexInputBindingDescription vertexBinding{};
        vertexBinding.binding = 0;
        vertexBinding.stride = sizeof(float) * 4;
        vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        VkVertexInputAttributeDescription vertexAttrs[2]{};
        vertexAttrs[0].location = 0;
        vertexAttrs[0].binding = 0;
        vertexAttrs[0].format = VK_FORMAT_R32G32_SFLOAT;
        vertexAttrs[0].offset = 0;
        vertexAttrs[1].location = 1;
        vertexAttrs[1].binding = 0;
        vertexAttrs[1].format = VK_FORMAT_R32G32_SFLOAT;
        vertexAttrs[1].offset = sizeof(float) * 2;

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = 1;
        vertexInput.pVertexBindingDescriptions = &vertexBinding;
        vertexInput.vertexAttributeDescriptionCount = 2;
        vertexInput.pVertexAttributeDescriptions = vertexAttrs;

        VkPipelineInputAssemblyStateCreateInfo inputAsm{};
        inputAsm.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode    = VK_CULL_MODE_NONE;
        raster.frontFace   = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType            = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable  = VK_TRUE;
        depthStencil.depthWriteEnable = VK_TRUE;
        depthStencil.depthCompareOp   = VK_COMPARE_OP_LESS_OR_EQUAL;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.blendEnable         = VK_TRUE;
        blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blendAttachment.colorBlendOp        = VK_BLEND_OP_ADD;
        blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
        blendAttachment.alphaBlendOp        = VK_BLEND_OP_ADD;
        blendAttachment.colorWriteMask      =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments    = &blendAttachment;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates    = dynamicStates;

        // Build a pipeline layout entirely from merged reflection: one descriptor set layout per
        // set the shaders declare. Set/binding indices come from Slang reflection.
        RHI::RHI_ProgramReflection reflForVk =
            RHI::MergeProgramReflections({ vertOut.m_Reflection, fragOut.m_Reflection });

        MarkEngineDynamicBuffers(reflForVk);

        // The descriptor set holding the engine resources (nova.*) is reused from the swapchain;
        // any other reflected set gets its own descriptor set allocated below.
        std::optional<uint32_t> engineResourceSetIndex;
        if (const RHI::RHI_BindingKey* frameKey = reflForVk.FindBindingKeyByName(RHI::EngineResourceName::Frame))
            engineResourceSetIndex = frameKey->m_Set;
        else if (const RHI::RHI_BindingKey* mvpKey = reflForVk.FindBindingKeyByName(RHI::EngineResourceName::Mvp))
            engineResourceSetIndex = mvpKey->m_Set;

        // Create a descriptor set layout for every reflection set.
        std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> setLayoutPairs;
        for (const auto& setInfo : reflForVk.m_Sets) {
            VkDescriptorSetLayout l = VK_NULL_HANDLE;
            if (CreateDescriptorSetLayoutFromReflection(device, reflForVk, setInfo.m_Set, l))
                setLayoutPairs.emplace_back(setInfo.m_Set, l);
        }
        std::sort(setLayoutPairs.begin(), setLayoutPairs.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        if (setLayoutPairs.empty()) {
            NV_LOG_WARN("CreateFullscreenShader: reflection produced no descriptor sets");
            vertModule.Destroy();
            fragModule.Destroy();
            return nullptr;
        }

        auto destroySetLayouts = [&]() {
            for (const auto& [idx, l] : setLayoutPairs)
                if (l != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(device, l, nullptr);
        };

        std::vector<VkDescriptorSetLayout> setLayouts;
        setLayouts.reserve(setLayoutPairs.size());
        for (const auto& [idx, l] : setLayoutPairs) setLayouts.push_back(l);

        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        VkResult res = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            destroySetLayouts();
            vertModule.Destroy();
            fragModule.Destroy();
            return nullptr;
        }

        VkGraphicsPipelineCreateInfo pipe{};
        pipe.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipe.stageCount          = 2;
        pipe.pStages             = stages;
        pipe.pVertexInputState   = &vertexInput;
        pipe.pInputAssemblyState = &inputAsm;
        pipe.pViewportState      = &viewportState;
        pipe.pRasterizationState = &raster;
        pipe.pMultisampleState   = &msaa;
        pipe.pDepthStencilState  = &depthStencil;
        pipe.pColorBlendState    = &blend;
        pipe.pDynamicState       = &dynamic;
        pipe.layout              = layout;
        pipe.renderPass          = m_BackBufferRenderPass;
        pipe.subpass             = 0;

        VkPipeline pipeline = VK_NULL_HANDLE;
        res = vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe, nullptr, &pipeline);

        vertModule.Destroy();
        fragModule.Destroy();

        if (res != VK_SUCCESS) {
            NV_LOG_WARN("CreateFullscreenShader: pipeline creation failed");
            vkDestroyPipelineLayout(device, layout, nullptr);
            destroySetLayouts();
            return nullptr;
        }

        m_FullscreenPipelines.push_back(pipeline);

        // Build the descriptor set list for the shader: reuse the swapchain's engine descriptor set
        // for the engine set, and allocate a fresh descriptor set for every other (user) set.
        const auto& swapchainSets = m_DescriptorSets;
        auto findSwapchainSet = [&](uint32_t setIndex) -> VkDescriptorSet {
            for (const auto& [idx, ds] : swapchainSets) if (idx == setIndex) return ds;
            return VK_NULL_HANDLE;
        };

        std::vector<VkDescriptorSet> ownedSets;
        std::vector<std::pair<uint32_t, VkDescriptorSet>> shaderSets;
        bool allocFailed = false;
        for (const auto& [setIndex, setLayout] : setLayoutPairs) {
            if (engineResourceSetIndex && setIndex == *engineResourceSetIndex) {
                shaderSets.emplace_back(setIndex, findSwapchainSet(setIndex));
                continue;
            }
            VkDescriptorSetAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocInfo.descriptorPool = m_ImGuiDescriptorPool;
            allocInfo.descriptorSetCount = 1;
            allocInfo.pSetLayouts = &setLayout;
            VkDescriptorSet ds = VK_NULL_HANDLE;
            res = vkAllocateDescriptorSets(device, &allocInfo, &ds);
            CheckVkResult(res);
            if (res != VK_SUCCESS) { allocFailed = true; break; }
            ownedSets.push_back(ds);
            shaderSets.emplace_back(setIndex, ds);
        }

        if (allocFailed) {
            NV_LOG_WARN("CreateFullscreenShader: failed to allocate descriptor set");
            VkDescriptorPool pool = m_ImGuiDescriptorPool;
            for (VkDescriptorSet ds : ownedSets)
                if (ds != VK_NULL_HANDLE && pool != VK_NULL_HANDLE) vkFreeDescriptorSets(device, pool, 1, &ds);
            m_FullscreenPipelines.pop_back();
            vkDestroyPipeline(device, pipeline, nullptr);
            vkDestroyPipelineLayout(device, layout, nullptr);
            destroySetLayouts();
            return nullptr;
        }

        m_FullscreenPipelineState[pipeline] = FullscreenPipelineState{ layout, std::move(setLayouts), std::move(ownedSets) };

        auto* shader = new VK_Shaders();
        shader->SetPipeline(pipeline, layout);
        shader->SetSceneBuffers(device,
            m_BufGlobals,    m_BufGlobalsMemory,
            m_BufMvp,        m_BufMvpMemory, m_MvpDynamicStride,
            m_BufMaterials,  m_BufMaterialsMemory, m_MaterialDynamicStride,
            m_BufInstances,  m_BufInstancesMemory,
            m_BufInstancesSize,
            std::move(shaderSets));
        shader->SetReflection(reflForVk);

        NV_LOG_INFO("Fullscreen shader pipeline created.");
        return shader;
    }

    void VK_Renderer::DestroyFullscreenShader(RHI::RHI_Shaders* shader) {
        if (!shader) return;
        vkDeviceWaitIdle(m_VKDevice.GetDevice());

        auto* vkShader = static_cast<VK_Shaders*>(shader);
        VkPipeline pipeline = vkShader->GetPipeline();
        if (pipeline != VK_NULL_HANDLE) {
            auto it = std::find(m_FullscreenPipelines.begin(), m_FullscreenPipelines.end(), pipeline);
            if (it != m_FullscreenPipelines.end())
                m_FullscreenPipelines.erase(it);

            if (auto itState = m_FullscreenPipelineState.find(pipeline); itState != m_FullscreenPipelineState.end()) {
                const auto state = itState->second;
                VkDescriptorPool pool = m_ImGuiDescriptorPool;
                for (VkDescriptorSet ds : state.ownedSets) {
                    if (ds != VK_NULL_HANDLE && pool != VK_NULL_HANDLE)
                        vkFreeDescriptorSets(m_VKDevice.GetDevice(), pool, 1, &ds);
                }
                if (state.layout != VK_NULL_HANDLE) vkDestroyPipelineLayout(m_VKDevice.GetDevice(), state.layout, nullptr);
                for (VkDescriptorSetLayout l : state.setLayouts)
                    if (l != VK_NULL_HANDLE) vkDestroyDescriptorSetLayout(m_VKDevice.GetDevice(), l, nullptr);
                m_FullscreenPipelineState.erase(itState);
            }

            vkDestroyPipeline(m_VKDevice.GetDevice(), pipeline, nullptr);
        }

        delete vkShader;
    }

    void VK_Renderer::DrawFullscreen(RHI::RHI_Shaders* shader) {
        if (!m_FrameActive || !shader) return;

        VkCommandBuffer cmd = m_VKSwapchain.GetCommandBuffers()[m_VKSwapchain.GetAcquiredImageIndex()];

        if (m_Shader && m_Shader->IsValid())
            m_Shader->ApplyParameters(cmd);

        shader->Bind(cmd);

        if (m_FullscreenQuadBuffer == VK_NULL_HANDLE) {
            NV_LOG_WARN("DrawFullscreen: fullscreen quad buffer not allocated");
            return;
        }
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_FullscreenQuadBuffer, offsets);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan