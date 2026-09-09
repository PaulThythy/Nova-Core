#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"

#include "Renderer/Backends/Vulkan/VK_Renderer.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Math/Vertex.h"
#include "Core/Application.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

#include "backends/imgui_impl_vulkan.h"

#include <array>
#include <vector>
#include <cstring>

namespace Nova::Core::Renderer::Backends::Vulkan {

    VK_RenderGraph::VK_RenderGraph(RHI::RHI_CompiledRenderGraph compiled)
        : IRenderGraph(std::move(compiled)) {}

    bool VK_RenderGraph::Create(VK_Renderer& renderer) {
        Destroy();
        m_Renderer = &renderer;

        if (m_Passes.empty()) {
            NV_LOG_ERROR("VK_RenderGraph::Create - empty render graph");
            return false;
        }

        // Prefer an offscreen color attachment format when the graph renders into
        // a panel texture; otherwise pipelines must match the swapchain format.
        VkFormat sceneColorFormat = renderer.GetSwapchainImageFormat();
        for (const auto& texRes : m_Data.m_Textures) {
            if (texRes.m_IsSwapchain || texRes.m_Imported)
                continue;
            if (IsDepthFormat(texRes.m_Desc.m_Format))
                continue;
            if (!HasTextureUsage(texRes.m_Desc.m_Usage, RHI::RHI_TextureUsage::ColorAttachment))
                continue;
            sceneColorFormat = ToVkFormat(texRes.m_Desc.m_Format);
            break;
        }

        // Depth format comes from swapchain render targets (created with the renderer).
        const VkFormat depthFormat = renderer.GetSwapchain().GetDepthFormat();
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            NV_LOG_ERROR("VK_RenderGraph::Create - swapchain depth format is undefined");
            return false;
        }
        m_DepthFormat = depthFormat;

        if (!m_PipelineCache.Create(renderer, m_Data.m_Shaders, sceneColorFormat, depthFormat)) {
            NV_LOG_ERROR("VK_RenderGraph::Create - failed to create pipeline cache");
            return false;
        }

        if (!InitPresentationResources()) {
            NV_LOG_ERROR("VK_RenderGraph::Create - failed to init presentation resources");
            return false;
        }

        if (!CreateTransientResources()) {
            NV_LOG_ERROR("VK_RenderGraph::Create - failed to create transient resources");
            Destroy();
            return false;
        }

        m_PassRenderTargets.resize(m_Passes.size());
        m_Compiled = true;
        return true;
    }

    void VK_RenderGraph::Destroy() {
        DestroyTransientResources();
        DestroyPassRenderTargets();
        DestroyPresentationResources();
        m_PipelineCache.Destroy();
        m_Renderer = nullptr;
        m_Compiled = false;
        m_ShadersReloaded = false;
    }

    void VK_RenderGraph::ReleaseImGuiTextures() {
        for (auto& texture : m_Textures)
            texture.UnregisterImGui();
    }

    VkFormat VK_RenderGraph::ToVkFormat(RHI::RHI_TextureFormat format) const {
        switch (format) {
            case RHI::RHI_TextureFormat::RGBA8:            return VK_FORMAT_R8G8B8A8_UNORM;
            case RHI::RHI_TextureFormat::RGBA16F:          return VK_FORMAT_R16G16B16A16_SFLOAT;
            case RHI::RHI_TextureFormat::RGBA32F:          return VK_FORMAT_R32G32B32A32_SFLOAT;
            case RHI::RHI_TextureFormat::Depth32:          return VK_FORMAT_D32_SFLOAT;
            case RHI::RHI_TextureFormat::Depth24Stencil8:  return VK_FORMAT_D24_UNORM_S8_UINT;
            default:                                       return VK_FORMAT_UNDEFINED;
        }
    }

    bool VK_RenderGraph::IsDepthFormat(RHI::RHI_TextureFormat format) const {
        return format == RHI::RHI_TextureFormat::Depth32
            || format == RHI::RHI_TextureFormat::Depth24Stencil8;
    }

    bool VK_RenderGraph::CreateTransientResources() {
        m_Textures.clear();
        m_Textures.resize(m_Data.m_Textures.size());

        m_SceneWidth = 0;
        m_SceneHeight = 0;

        for (size_t i = 0; i < m_Data.m_Textures.size(); ++i) {
            m_Textures[i].GetGraphDesc() = m_Data.m_Textures[i];
            const auto& res = m_Data.m_Textures[i];

            if (res.m_IsSwapchain || res.m_Imported)
                continue;

            const uint32_t w = res.m_Desc.m_Width;
            const uint32_t h = res.m_Desc.m_Height;
            if (w == 0 || h == 0) continue;

            if (!IsDepthFormat(res.m_Desc.m_Format)) {
                m_SceneWidth = std::max(m_SceneWidth, w);
                m_SceneHeight = std::max(m_SceneHeight, h);
            }

            if (!m_Renderer)
                return false;

            const VkFormat format = IsDepthFormat(res.m_Desc.m_Format)
                ? m_DepthFormat
                : ToVkFormat(res.m_Desc.m_Format);

            if (!m_Textures[i].CreateAsRenderTarget(
                    m_Renderer->GetMemoryAllocator(),
                    m_Renderer->GetDevice(),
                    res.m_Desc,
                    w,
                    h,
                    format))
                return false;
        }
        return true;
    }

    void VK_RenderGraph::DestroyTransientResources() {
        for (auto& tex : m_Textures)
            tex.Release();
        m_Textures.clear();
    }

    void VK_RenderGraph::DestroyPassRenderTargets() {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();

        for (auto& rt : m_PassRenderTargets) {
            if (rt.renderPassClear != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, rt.renderPassClear, nullptr);
                rt.renderPassClear = VK_NULL_HANDLE;
            }
            if (rt.renderPassLoad != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, rt.renderPassLoad, nullptr);
                rt.renderPassLoad = VK_NULL_HANDLE;
            }
            rt.colorAttachments.clear();
            rt.depthAttachment = {};
        }
    }

    bool VK_RenderGraph::Resize(uint32_t width, uint32_t height) {
        if (width == 0 || height == 0)
            return true;

        if (m_Renderer) {
            VkDevice device = m_Renderer->GetDevice();
            if (device != VK_NULL_HANDLE)
                vkDeviceWaitIdle(device);
        }

        DestroyPassRenderTargets();
        DestroyTransientResources();

        for (auto& texRes : m_Data.m_Textures) {
            if (texRes.m_IsSwapchain || texRes.m_Imported)
                continue;
            if (!texRes.m_Desc.m_ResizeWithViewport)
                continue;
            texRes.m_Desc.m_Width = width;
            texRes.m_Desc.m_Height = height;
        }

        if (!CreateTransientResources())
            return false;

        return true;
    }

    bool VK_RenderGraph::BindEngineShadowMaps(RHI::RHI_TextureHandle shadowMaps) {
        if (!shadowMaps.IsValid() || shadowMaps.m_Index >= m_Textures.size())
            return false;
        VK_Texture& tex = m_Textures[shadowMaps.m_Index];
        if (tex.GetImageView() == VK_NULL_HANDLE || tex.GetSampler() == VK_NULL_HANDLE)
            return false;
        return m_PipelineCache.BindEngineShadowMaps(tex.GetImageView(), tex.GetSampler());
    }

    bool VK_RenderGraph::GetSampledTextureNativeHandles(RHI::RHI_TextureHandle textureHandle, uint64_t& outImageView, uint64_t& outSampler) const {
        outImageView = 0;
        outSampler = 0;
        if (!textureHandle.IsValid() || textureHandle.m_Index >= m_Textures.size())
            return false;

        const VK_Texture& tex = m_Textures[textureHandle.m_Index];
        if (tex.GetSampledView() == VK_NULL_HANDLE || tex.GetSampler() == VK_NULL_HANDLE)
            return false;

        outImageView = reinterpret_cast<uint64_t>(tex.GetSampledView());
        outSampler = reinterpret_cast<uint64_t>(tex.GetSampler());
        return true;
    }

    void* VK_RenderGraph::GetTextureImGuiID(RHI::RHI_TextureHandle handle) const {
        if (!handle.IsValid() || handle.m_Index >= m_Textures.size())
            return nullptr;
        return m_Textures[handle.m_Index].GetImGuiID();
    }

    VkCommandBuffer VK_RenderGraph::GetCurrentCommandBuffer() const {
        if (!m_Renderer) return VK_NULL_HANDLE;
        return m_Renderer->GetCurrentCommandBuffer();
    }

    void VK_RenderGraph::OnBeginFrame() {
        m_PipelineCache.ResetFrameDynamicUBOs();
        m_SwapchainColorWritten = false;
    }

    void VK_RenderGraph::ExecuteScenePasses() {
        for (size_t passIndex : m_ExecutionOrder) {
            const auto& pass = m_Passes[passIndex];
            if (pass.m_PresentOnly)
                continue;
            ExecutePass(passIndex, false, false);
        }
    }

    void VK_RenderGraph::ExecutePresentPasses() {
        for (size_t passIndex : m_ExecutionOrder) {
            const auto& pass = m_Passes[passIndex];
            if (!pass.m_PresentOnly)
                continue;
            // Leave the render pass open so ImGui can record draw commands into the swapchain.
            ExecutePass(passIndex, true, true);
        }
    }

    void VK_RenderGraph::OnEndFrame() {
        if (m_InsideRenderPass) {
            vkCmdEndRenderPass(GetCurrentCommandBuffer());
            m_InsideRenderPass = false;
        }
    }

    bool VK_RenderGraph::ReloadChangedShaders() {
        const bool changed = m_PipelineCache.ReloadChangedShaders();
        if (changed)
            m_ShadersReloaded = true;
        return changed;
    }

    bool VK_RenderGraph::EnsurePassRenderTarget(PassRenderTarget& rt, const RHI::RHI_RenderGraphPassDesc& pass) {
        if (!m_Renderer) return false;

        rt.colorAttachments.clear();
        rt.depthAttachment = {};
        rt.depthOnly = false;

        for (RHI::RHI_TextureHandle handle : pass.m_WriteTextures) {
            if (!handle.IsValid() || handle.m_Index >= m_Textures.size())
                continue;
            const auto& graphDesc = m_Textures[handle.m_Index].GetGraphDesc();
            const auto& texDesc = graphDesc.m_Desc;
            if (graphDesc.m_IsSwapchain)
                continue;

            if (IsDepthFormat(texDesc.m_Format))
                rt.depthAttachment = handle;
            else
                rt.colorAttachments.push_back(handle);
        }

        rt.depthOnly = rt.colorAttachments.empty() && rt.depthAttachment.IsValid();

        if (rt.colorAttachments.empty() && !rt.depthAttachment.IsValid())
            return true;

        // LOAD only when the *written* attachment is also declared as a read.
        // Sampling a different color texture (post-process) must not force LOAD on the output.
        const bool loadsWrittenColor = [&]() {
            for (RHI::RHI_TextureHandle written : rt.colorAttachments) {
                if (std::find(pass.m_ReadTextures.begin(), pass.m_ReadTextures.end(), written)
                    != pass.m_ReadTextures.end())
                    return true;
            }
            return false;
        }();

        const bool loadsWrittenDepth = rt.depthAttachment.IsValid()
            && std::find(pass.m_ReadTextures.begin(), pass.m_ReadTextures.end(), rt.depthAttachment)
                != pass.m_ReadTextures.end();

        const VkAttachmentLoadOp colorLoad = loadsWrittenColor
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : VK_ATTACHMENT_LOAD_OP_CLEAR;
        const VkAttachmentLoadOp depthLoad = loadsWrittenDepth
            ? VK_ATTACHMENT_LOAD_OP_LOAD
            : VK_ATTACHMENT_LOAD_OP_CLEAR;

        if (rt.depthOnly) {
            if (rt.renderPassClear == VK_NULL_HANDLE)
                rt.renderPassClear = CreateDepthOnlyRenderPass(VK_ATTACHMENT_LOAD_OP_CLEAR);
            if (rt.renderPassLoad == VK_NULL_HANDLE)
                rt.renderPassLoad = CreateDepthOnlyRenderPass(VK_ATTACHMENT_LOAD_OP_LOAD);

            VK_Texture& depthTex = m_Textures[rt.depthAttachment.m_Index];
            const auto& depthDesc = depthTex.GetGraphDesc().m_Desc;
            const uint32_t layers = std::max(1u, depthDesc.m_Layers);
            auto& layerFbs = depthTex.GetLayerFramebuffers();
            const auto& layerViews = depthTex.GetLayerViews();
            if (layerFbs.size() != layers) {
                for (VkFramebuffer fb : layerFbs) {
                    if (fb != VK_NULL_HANDLE)
                        vkDestroyFramebuffer(m_Renderer->GetDevice(), fb, nullptr);
                }
                layerFbs.assign(layers, VK_NULL_HANDLE);
                for (uint32_t layer = 0; layer < layers; ++layer) {
                    VkImageView attachView = (layers > 1 && layer < layerViews.size())
                        ? layerViews[layer]
                        : depthTex.GetImageView();
                    VkFramebufferCreateInfo fbInfo{};
                    fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                    fbInfo.renderPass = rt.renderPassClear;
                    fbInfo.attachmentCount = 1;
                    fbInfo.pAttachments = &attachView;
                    fbInfo.width = depthDesc.m_Width;
                    fbInfo.height = depthDesc.m_Height;
                    fbInfo.layers = 1;
                    vkCreateFramebuffer(m_Renderer->GetDevice(), &fbInfo, nullptr, &layerFbs[layer]);
                }
            }
            return true;
        }

        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        if (!rt.colorAttachments.empty()) {
            const auto& first = m_Textures[rt.colorAttachments[0].m_Index].GetGraphDesc().m_Desc;
            colorFormat = ToVkFormat(first.m_Format);
        }

        if (rt.renderPassClear == VK_NULL_HANDLE) {
            rt.renderPassClear = CreateColorDepthRenderPass(
                colorFormat, colorLoad, depthLoad, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            if (loadsWrittenColor || loadsWrittenDepth) {
                rt.renderPassLoad = CreateColorDepthRenderPass(
                    colorFormat, VK_ATTACHMENT_LOAD_OP_LOAD,
                    loadsWrittenDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : depthLoad,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }

        if (!rt.colorAttachments.empty() && rt.colorAttachments[0].IsValid()) {
            VK_Texture& colorTex = m_Textures[rt.colorAttachments[0].m_Index];
            if (colorTex.GetFramebuffer() == VK_NULL_HANDLE) {
                std::vector<VkImageView> views;
                views.push_back(colorTex.GetImageView());
                if (rt.depthAttachment.IsValid())
                    views.push_back(m_Textures[rt.depthAttachment.m_Index].GetImageView());

                const auto& colorDesc = colorTex.GetGraphDesc().m_Desc;
                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbInfo.renderPass = rt.renderPassClear;
                fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
                fbInfo.pAttachments = views.data();
                fbInfo.width = colorDesc.m_Width;
                fbInfo.height = colorDesc.m_Height;
                fbInfo.layers = 1;

                VkFramebuffer fb = VK_NULL_HANDLE;
                vkCreateFramebuffer(m_Renderer->GetDevice(), &fbInfo, nullptr, &fb);
                colorTex.SetFramebuffer(fb);
            }
        }

        return true;
    }

    bool VK_RenderGraph::ExecutePass(size_t passIndex, bool presentPhase, bool leaveRenderPassOpen) {
        if (!m_Renderer || passIndex >= m_Passes.size()) return false;

        const RHI::RHI_RenderGraphPassDesc& pass = m_Passes[passIndex];
        PassRenderTarget& rt = m_PassRenderTargets[passIndex];

        const bool writesSwapchain = PassWritesSwapchain(pass);

        if (!writesSwapchain)
            EnsurePassRenderTarget(rt, pass);

        VkCommandBuffer cmd = GetCurrentCommandBuffer();
        if (cmd == VK_NULL_HANDLE) return false;

        // Depth-only passes manage begin/end per layer inside the execute callback.
        if (!writesSwapchain && rt.depthOnly) {
            uint32_t width = m_SceneWidth;
            uint32_t height = m_SceneHeight;
            if (rt.depthAttachment.IsValid()) {
                const auto& d = m_Textures[rt.depthAttachment.m_Index].GetGraphDesc().m_Desc;
                width = d.m_Width;
                height = d.m_Height;
            }
            if (pass.m_Execute) {
                PassContext ctx(*this, width, height, &rt);
                pass.m_Execute(ctx);
            }

            for (RHI::RHI_TextureHandle handle : pass.m_WriteTextures) {
                if (!handle.IsValid() || handle.m_Index >= m_Textures.size()) continue;
                VK_Texture& tex = m_Textures[handle.m_Index];
                if (!HasTextureUsage(tex.GetGraphDesc().m_Desc.m_Usage, RHI::RHI_TextureUsage::Sampled))
                    continue;
                TransitionTextureForSampling(cmd, tex);
            }
            return true;
        }

        uint32_t width = m_SceneWidth;
        uint32_t height = m_SceneHeight;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        if (writesSwapchain) {
            width = m_Renderer->GetSwapchainWidth();
            height = m_Renderer->GetSwapchainHeight();
            framebuffer = m_Renderer->GetSwapchainFramebuffer(m_Renderer->GetAcquiredImageIndex());

            const auto& swapchain = m_Renderer->GetSwapchain();
            if (presentPhase) {
                renderPass = m_SwapchainColorWritten
                    ? swapchain.GetPresentLoadRenderPass()
                    : swapchain.GetPresentClearRenderPass();
            } else {
                renderPass = m_SwapchainColorWritten
                    ? swapchain.GetSceneLoadRenderPass()
                    : swapchain.GetSceneClearRenderPass();
            }
        } else if (!rt.colorAttachments.empty()) {
            const RHI::RHI_TextureHandle colorHandle = rt.colorAttachments[0];
            VK_Texture& colorTex = m_Textures[colorHandle.m_Index];
            const auto& colorDesc = colorTex.GetGraphDesc().m_Desc;
            width = colorDesc.m_Width;
            height = colorDesc.m_Height;
            framebuffer = colorTex.GetFramebuffer();

            const bool readsColor = std::find(pass.m_ReadTextures.begin(), pass.m_ReadTextures.end(), colorHandle)
                != pass.m_ReadTextures.end();
            renderPass = (readsColor && rt.renderPassLoad != VK_NULL_HANDLE)
                ? rt.renderPassLoad
                : rt.renderPassClear;
        }

        if (framebuffer == VK_NULL_HANDLE || renderPass == VK_NULL_HANDLE)
            return true;

        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPass;
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea = { { 0, 0 }, { width, height } };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_InsideRenderPass = true;
        SetViewportScissor(cmd, width, height);

        if (writesSwapchain)
            m_SwapchainColorWritten = true;

        if (pass.m_Execute) {
            PassContext ctx(*this, width, height, &rt);
            pass.m_Execute(ctx);
        }

        if (!leaveRenderPassOpen) {
            vkCmdEndRenderPass(cmd);
            m_InsideRenderPass = false;
        }

        if (!writesSwapchain) {
            auto isLastWriter = [&](RHI::RHI_TextureHandle handle) {
                auto it = std::find(m_ExecutionOrder.begin(), m_ExecutionOrder.end(), passIndex);
                if (it == m_ExecutionOrder.end())
                    return true;
                for (++it; it != m_ExecutionOrder.end(); ++it) {
                    const auto& later = m_Passes[*it];
                    if (later.m_PresentOnly)
                        continue;
                    const auto& writes = later.m_WriteTextures;
                    if (std::find(writes.begin(), writes.end(), handle) != writes.end())
                        return false;
                }
                return true;
            };

            for (RHI::RHI_TextureHandle handle : pass.m_WriteTextures) {
                if (!handle.IsValid() || handle.m_Index >= m_Textures.size()) continue;
                VK_Texture& tex = m_Textures[handle.m_Index];
                if (tex.GetGraphDesc().m_IsSwapchain) continue;
                if (!HasTextureUsage(tex.GetGraphDesc().m_Desc.m_Usage, RHI::RHI_TextureUsage::Sampled))
                    continue;
                if (!isLastWriter(handle))
                    continue;
                TransitionTextureForSampling(cmd, tex);
            }
        }

        return true;
    }

    void VK_RenderGraph::PassContext::BeginDepthLayer(RHI::RHI_TextureHandle depth, uint32_t layer, bool clear) {
        if (!m_Rt || !depth.IsValid() || depth.m_Index >= m_Graph.m_Textures.size())
            return;
        VK_Texture& tex = m_Graph.m_Textures[depth.m_Index];
        const auto& layerFbs = tex.GetLayerFramebuffers();
        if (layer >= layerFbs.size())
            return;

        VkCommandBuffer cmd = m_Graph.GetCurrentCommandBuffer();
        if (cmd == VK_NULL_HANDLE) return;

        if (m_Graph.m_InsideRenderPass) {
            vkCmdEndRenderPass(cmd);
            m_Graph.m_InsideRenderPass = false;
        }

        VkRenderPass rp = clear ? m_Rt->renderPassClear : m_Rt->renderPassLoad;
        if (rp == VK_NULL_HANDLE)
            rp = m_Rt->renderPassClear;
        if (rp == VK_NULL_HANDLE)
            return;

        const auto& depthDesc = tex.GetGraphDesc().m_Desc;
        VkClearValue clearValue{};
        clearValue.depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = rp;
        rpBegin.framebuffer = layerFbs[layer];
        rpBegin.renderArea = { { 0, 0 }, { depthDesc.m_Width, depthDesc.m_Height } };
        rpBegin.clearValueCount = 1;
        rpBegin.pClearValues = &clearValue;

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_Graph.m_InsideRenderPass = true;
        m_Width = depthDesc.m_Width;
        m_Height = depthDesc.m_Height;
        m_Graph.SetViewportScissor(cmd, m_Width, m_Height);
    }

    void VK_RenderGraph::PassContext::EndDepthLayer() {
        VkCommandBuffer cmd = m_Graph.GetCurrentCommandBuffer();
        if (cmd == VK_NULL_HANDLE || !m_Graph.m_InsideRenderPass)
            return;
        vkCmdEndRenderPass(cmd);
        m_Graph.m_InsideRenderPass = false;
    }

    void VK_RenderGraph::TransitionTextureForSampling(VkCommandBuffer cmd, VK_Texture& texture) {
        if (texture.GetImage() == VK_NULL_HANDLE) return;

        const auto& desc = texture.GetGraphDesc().m_Desc;
        const bool isDepth = IsDepthFormat(desc.m_Format);
        const uint32_t layers = std::max(1u, desc.m_Layers);

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = isDepth
            ? VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT
            : VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = isDepth
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = texture.GetImage();
        barrier.subresourceRange.aspectMask = isDepth
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.baseArrayLayer = 0;
        barrier.subresourceRange.layerCount = layers;

        const VkPipelineStageFlags srcStage = isDepth
            ? (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        vkCmdPipelineBarrier(cmd,
            srcStage,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        texture.SetResourceState(RHI::RHI_ResourceState::ShaderRead);
    }

    void VK_RenderGraph::SetViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
        VkViewport viewport{};
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.extent = { width, height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void VK_RenderGraph::DrawFullscreenQuad(VkCommandBuffer cmd) {
        if (m_FullscreenQuadBuffer.buffer == VK_NULL_HANDLE) return;
        VkDeviceSize offsets[] = { 0 };
        const VkBuffer quadBuffer = m_FullscreenQuadBuffer.buffer;
        vkCmdBindVertexBuffers(cmd, 0, 1, &quadBuffer, offsets);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    void VK_RenderGraph::PassContext::DrawFullscreen(RHI::RHI_ShaderHandle handle) {
        RHI::IShaders* shader = m_Graph.m_PipelineCache.Get(handle);
        VkCommandBuffer cmd = m_Graph.GetCurrentCommandBuffer();
        if (!shader || cmd == VK_NULL_HANDLE) return;
        shader->ApplyParameters(cmd);
        shader->Bind(cmd);
        m_Graph.DrawFullscreenQuad(cmd);
    }

    void VK_RenderGraph::PassContext::Draw(const RHI::RHI_DrawCommand& cmd) {
        if (!m_Graph.m_Renderer) return;
        m_Graph.m_Renderer->Draw(cmd);
    }

    void VK_RenderGraph::PassContext::DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        if (!m_Graph.m_Renderer) return;
        m_Graph.m_Renderer->DrawIndexed(cmd);
    }

    void VK_RenderGraph::PassContext::BindShader(RHI::RHI_ShaderHandle handle) {
        RHI::IShaders* shader = m_Graph.m_PipelineCache.Get(handle);
        VkCommandBuffer cmd = m_Graph.GetCurrentCommandBuffer();
        if (!shader || cmd == VK_NULL_HANDLE) return;
        shader->Bind(cmd);
        shader->ApplyParameters(cmd);
    }

    void VK_RenderGraph::PassContext::SetDepthBias(float constantFactor, float slopeFactor, float clamp) {
        VkCommandBuffer cmd = m_Graph.GetCurrentCommandBuffer();
        if (cmd == VK_NULL_HANDLE) return;
        vkCmdSetDepthBias(cmd, constantFactor, clamp, slopeFactor);
    }


    VkRenderPass VK_RenderGraph::CreateRenderPass(
        const VK_RenderPassAttachmentDesc* colors, uint32_t colorCount,
        const VK_RenderPassAttachmentDesc* depth) const
    {
        if (!m_Renderer) return VK_NULL_HANDLE;

        std::vector<VkAttachmentDescription> attachments;
        attachments.reserve(colorCount + (depth ? 1u : 0u));

        std::vector<VkAttachmentReference> colorRefs;
        colorRefs.reserve(colorCount);
        for (uint32_t i = 0; i < colorCount; ++i) {
            VkAttachmentDescription a{};
            a.format = colors[i].format;
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = colors[i].loadOp;
            a.storeOp = colors[i].storeOp;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = colors[i].initialLayout;
            a.finalLayout = colors[i].finalLayout;
            colorRefs.push_back({ static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL });
            attachments.push_back(a);
        }

        VkAttachmentReference depthRef{};
        const bool hasDepth = depth != nullptr;
        if (hasDepth) {
            VkAttachmentDescription a{};
            a.format = depth->format != VK_FORMAT_UNDEFINED ? depth->format : m_DepthFormat;
            a.samples = VK_SAMPLE_COUNT_1_BIT;
            a.loadOp = depth->loadOp;
            a.storeOp = depth->storeOp;
            a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            a.initialLayout = depth->initialLayout;
            a.finalLayout = depth->finalLayout;
            depthRef = { static_cast<uint32_t>(attachments.size()), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            attachments.push_back(a);
        }

        VkSubpassDescription subpass{};
        subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        subpass.colorAttachmentCount = colorCount;
        subpass.pColorAttachments = colorRefs.empty() ? nullptr : colorRefs.data();
        subpass.pDepthStencilAttachment = hasDepth ? &depthRef : nullptr;

        VkSubpassDependency dependency{};
        dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
        dependency.dstSubpass = 0;
        if (colorCount > 0) {
            dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
            dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        } else {
            dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        }

        VkRenderPassCreateInfo renderPassInfo{};
        renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
        renderPassInfo.pAttachments = attachments.data();
        renderPassInfo.subpassCount = 1;
        renderPassInfo.pSubpasses = &subpass;
        renderPassInfo.dependencyCount = 1;
        renderPassInfo.pDependencies = &dependency;

        VkRenderPass renderPass = VK_NULL_HANDLE;
        vkCreateRenderPass(m_Renderer->GetDevice(), &renderPassInfo, nullptr, &renderPass);
        return renderPass;
    }

    VkRenderPass VK_RenderGraph::CreateDepthOnlyRenderPass(VkAttachmentLoadOp depthLoad) const {
        VK_RenderPassAttachmentDesc depth{};
        depth.format = m_DepthFormat;
        depth.loadOp = depthLoad;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.initialLayout = (depthLoad == VK_ATTACHMENT_LOAD_OP_LOAD)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        return CreateRenderPass(nullptr, 0, &depth);
    }

    VkRenderPass VK_RenderGraph::CreateColorDepthRenderPass(
        VkFormat colorFormat,
        VkAttachmentLoadOp colorLoad,
        VkAttachmentLoadOp depthLoad,
        VkImageLayout finalColorLayout,
        VkImageLayout colorInitialLayout) const
    {
        VK_RenderPassAttachmentDesc color{};
        color.format = colorFormat;
        color.loadOp = colorLoad;
        color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        if (colorLoad == VK_ATTACHMENT_LOAD_OP_LOAD) {
            color.initialLayout = (colorInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                ? colorInitialLayout
                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else if (colorInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            color.initialLayout = colorInitialLayout;
        } else {
            color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        color.finalLayout = finalColorLayout;

        VK_RenderPassAttachmentDesc depth{};
        depth.format = m_DepthFormat;
        depth.loadOp = depthLoad;
        depth.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        depth.initialLayout = (depthLoad == VK_ATTACHMENT_LOAD_OP_LOAD)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
        depth.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

        return CreateRenderPass(&color, 1, &depth);
    }

    bool VK_RenderGraph::InitPresentationResources() {
        if (!m_Renderer) return false;
        if (m_ResourcesInitialized) return true;

        CreateFullscreenQuadBuffer();

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = m_Renderer->GetVkInstance();
        initInfo.PhysicalDevice = m_Renderer->GetPhysicalDevice();
        initInfo.Device = m_Renderer->GetDevice();
        initInfo.QueueFamily = m_Renderer->GetGraphicsQueueFamily();
        initInfo.Queue = m_Renderer->GetGraphicsQueue();
        initInfo.DescriptorPool = m_PipelineCache.GetDescriptorPool();
        initInfo.MinImageCount = m_Renderer->GetSwapchainImageCount();
        initInfo.ImageCount = m_Renderer->GetSwapchainImageCount();
        initInfo.PipelineInfoMain.RenderPass = m_Renderer->GetSwapchain().GetPresentClearRenderPass();
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        // Secondary viewports create their own render pass/swapchain; do not reuse
        // the main window render pass here (ImGui overwrites RenderPass on create).
        initInfo.PipelineInfoForViewports.Subpass = 0;
        initInfo.PipelineInfoForViewports.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.UseDynamicRendering = false;
        initInfo.CheckVkResultFn = CheckVkResult;

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanInitInfo(initInfo);
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        imguiLayer.SetVulkanBeforeRenderCallback({});

        m_ResourcesInitialized = true;
        return true;
    }

    void VK_RenderGraph::DestroyPresentationResources() {
        DestroyFullscreenQuadBuffer();
        m_ResourcesInitialized = false;
    }

    void VK_RenderGraph::CreateFullscreenQuadBuffer() {
        if (m_FullscreenQuadBuffer.buffer != VK_NULL_HANDLE || !m_Renderer) return;

        const float quadVertices[] = {
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f, -1.0f, 1.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f, -1.0f, 0.0f, 0.0f,
             1.0f,  1.0f, 1.0f, 1.0f,
            -1.0f,  1.0f, 0.0f, 1.0f,
        };

        if (!m_Renderer->GetMemoryAllocator().CreateBuffer(
                sizeof(quadVertices),
                VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MemoryLocation::CpuToGpu,
                m_FullscreenQuadBuffer))
            return;

        m_Renderer->GetMemoryAllocator().WriteToBuffer(
            m_FullscreenQuadBuffer, 0, sizeof(quadVertices), quadVertices);
    }

    void VK_RenderGraph::DestroyFullscreenQuadBuffer() {
        if (m_Renderer)
            m_Renderer->GetMemoryAllocator().DestroyBuffer(m_FullscreenQuadBuffer);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan