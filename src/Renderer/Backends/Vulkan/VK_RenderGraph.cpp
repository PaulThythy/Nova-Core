#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"

#include "Renderer/Backends/Vulkan/VK_Renderer.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Graphics/Vertex.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Core/Application.h"
#include "Core/ImGuiLayer.h"
#include "Core/Log.h"

#include "backends/imgui_impl_vulkan.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>

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

    bool CreateDescriptorSetLayoutFromReflection(VkDevice device, const RHI::RHI_ProgramReflection& refl, uint32_t setIndex, VkDescriptorSetLayout& outLayout) {
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

    VkAttachmentLoadOp ToVkLoadOp(RHI::RHI_LoadOp op) {
        switch (op) {
            case RHI::RHI_LoadOp::Clear:   return VK_ATTACHMENT_LOAD_OP_CLEAR;
            case RHI::RHI_LoadOp::Load:    return VK_ATTACHMENT_LOAD_OP_LOAD;
            case RHI::RHI_LoadOp::DontCare: return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        }
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }

    std::filesystem::file_time_type GetFileWriteTime(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::last_write_time(path, ec);
    }

    bool CompileGraphicsShaders(const RHI::RHI_GraphicsShaderDesc& desc, RHI::RHI_ShaderCompileResult& vertOut, RHI::RHI_ShaderCompileResult& fragOut) {
        auto buildInput = [&](const std::filesystem::path& file, RHI::RHI_ShaderStage stage) {
            RHI::RHI_ShaderCompileInput in{};
            in.m_File = file;
            in.m_Stage = stage;
            in.m_EntryPoint = desc.m_EntryPoint;
            in.m_IncludeDirs = desc.m_IncludeDirs;

            const std::filesystem::path engineRoot = std::filesystem::current_path() / "Nova-Core" / "Resources" / "Engine" / "Shaders";

            auto addInclude = [&](const std::filesystem::path& dir) {
                if (dir.empty()) return;
                if (std::find(in.m_IncludeDirs.begin(), in.m_IncludeDirs.end(), dir) == in.m_IncludeDirs.end())
                    in.m_IncludeDirs.push_back(dir);
            };
            addInclude(engineRoot);
            addInclude(file.parent_path());
            in.m_Defines.emplace_back("NOVA_VULKAN", "1");
            return in;
        };

        vertOut = RHI::RHI_ShaderCompiler::Compile(
            buildInput(desc.m_VertexShader, RHI::ShaderStageFromFileExtension(desc.m_VertexShader)));
        if (!vertOut.m_Success) {
            NV_LOG_WARN(("VK_RenderGraph: vertex compile failed:\n" + vertOut.m_Log).c_str());
            return false;
        }

        fragOut = RHI::RHI_ShaderCompiler::Compile(
            buildInput(desc.m_FragmentShader, RHI::ShaderStageFromFileExtension(desc.m_FragmentShader)));
        if (!fragOut.m_Success) {
            NV_LOG_WARN(("VK_RenderGraph: fragment compile failed:\n" + fragOut.m_Log).c_str());
            return false;
        }
        return true;
    }

    VK_RenderGraph::VK_RenderGraph(std::vector<RHI::RHI_RenderPassDesc> passes) : IRenderGraph(std::move(passes)) {}

    bool VK_RenderGraph::Create(VK_Renderer& renderer) {
        Destroy();
        m_Renderer = &renderer;

        if (m_Passes.empty()) {
            NV_LOG_ERROR("VK_RenderGraph::Create - empty render graph");
            return false;
        }

        m_GeometryPassIndex = FindPassIndexByType(RHI::RHI_RenderPassType::Geometry);
        m_ImGuiPassIndex = FindPassIndexByType(RHI::RHI_RenderPassType::ImGui);

        m_PassPipelines.resize(m_Passes.size());
        for (size_t i = 0; i < m_Passes.size(); ++i) {
            m_PassPipelines[i].type = m_Passes[i].m_Type;
            ApplyPassDesc(m_PassPipelines[i], m_Passes[i]);
        }

        if (!InitSwapchainResources()) {
            NV_LOG_ERROR("VK_RenderGraph::Create - failed to init swapchain resources");
            return false;
        }

        for (size_t i = 0; i < m_Passes.size(); ++i) {
            if (!CreatePassPipeline(i, m_Passes[i])) {
                NV_LOG_ERROR("VK_RenderGraph::Create - failed to create pass pipeline");
                Destroy();
                return false;
            }
            SetPassShader(i, m_PassPipelines[i].shader.get());
        }

        SetCompiled(true);
        return true;
    }

    void VK_RenderGraph::Destroy() {
        ClearPassShaders();

        for (auto& pass : m_PassPipelines)
            DestroyPassPipeline(pass);

        DestroySwapchainResources();
        m_PassPipelines.clear();

        m_GeometryPassIndex = -1;
        m_ImGuiPassIndex = -1;
        m_ActivePassIndex = -1;
        m_GeometryPassActive = false;
        m_InsideRenderPass = false;
        m_ResourcesInitialized = false;
        m_Renderer = nullptr;
    }

    bool VK_RenderGraph::InitSwapchainResources() {
        if (!m_Renderer) return false;
        if (m_ResourcesInitialized) return true;

        if (!CreateImGuiDescriptorPool()) return false;
        if (!CreateDepthResources()) return false;
        if (!CreateBackBufferRenderPass()) return false;

        m_ViewportColorDepthClearPass = CreateColorDepthRenderPass(
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_ViewportColorLoadDepthClearPass = CreateColorDepthRenderPass(
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        if (m_ViewportColorDepthClearPass == VK_NULL_HANDLE ||
            m_ViewportColorLoadDepthClearPass == VK_NULL_HANDLE)
            return false;

        AssignPassRenderPasses();

        if (!CreateSwapchainFramebuffers()) return false;

        CreateFullscreenQuadBuffer();

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = VK_API_VERSION_1_3;
        initInfo.Instance = m_Renderer->GetVkInstance();
        initInfo.PhysicalDevice = m_Renderer->GetPhysicalDevice();
        initInfo.Device = m_Renderer->GetDevice();
        initInfo.QueueFamily = m_Renderer->GetGraphicsQueueFamily();
        initInfo.Queue = m_Renderer->GetGraphicsQueue();
        initInfo.DescriptorPool = m_ImGuiDescriptorPool;
        initInfo.MinImageCount = m_Renderer->GetSwapchainImageCount();
        initInfo.ImageCount = m_Renderer->GetSwapchainImageCount();
        initInfo.PipelineInfoMain.RenderPass = m_BackBufferRenderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoForViewports = initInfo.PipelineInfoMain;
        initInfo.UseDynamicRendering = false;
        initInfo.CheckVkResultFn = CheckVkResult;

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetVulkanInitInfo(initInfo);
        imguiLayer.SetVulkanCommandBuffer(VK_NULL_HANDLE);
        imguiLayer.SetVulkanBeforeRenderCallback({});

        m_ResourcesInitialized = true;
        return true;
    }

    void VK_RenderGraph::DestroyViewportRenderPasses() {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();
        if (m_ViewportColorDepthClearPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_ViewportColorDepthClearPass, nullptr);
            m_ViewportColorDepthClearPass = VK_NULL_HANDLE;
        }
        if (m_ViewportColorLoadDepthClearPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_ViewportColorLoadDepthClearPass, nullptr);
            m_ViewportColorLoadDepthClearPass = VK_NULL_HANDLE;
        }
    }

    void VK_RenderGraph::DestroySwapchainResources() {
        DestroyFullscreenQuadBuffer();
        DestroySwapchainFramebuffers();
        DestroyBackBufferRenderPass();
        DestroyViewportRenderPasses();
        DestroyDepthResources();
        DestroyImGuiDescriptorPool();

        VkDevice device = m_Renderer ? m_Renderer->GetDevice() : VK_NULL_HANDLE;
        for (auto& pass : m_PassPipelines) {
            if (device != VK_NULL_HANDLE && pass.renderPassBackBuffer != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, pass.renderPassBackBuffer, nullptr);
                pass.renderPassBackBuffer = VK_NULL_HANDLE;
            }
            pass.renderPassViewport = VK_NULL_HANDLE;
        }

        m_ResourcesInitialized = false;
    }

    bool VK_RenderGraph::RecreateSwapchainRenderTargets() {
        DestroySwapchainFramebuffers();
        DestroyDepthResources();
        if (!CreateDepthResources()) return false;
        return CreateSwapchainFramebuffers();
    }

    bool VK_RenderGraph::ReloadChangedShaders() {
        if (!m_Renderer) return false;

        bool anyChanged = false;
        for (size_t i = 0; i < m_PassPipelines.size(); ++i) {
            auto& pass = m_PassPipelines[i];
            if (!pass.hasShaderDesc) continue;

            const auto vertTime = GetFileWriteTime(pass.shaderDesc.m_VertexShader);
            const auto fragTime = GetFileWriteTime(pass.shaderDesc.m_FragmentShader);
            if (vertTime == pass.vertWriteTime && fragTime == pass.fragWriteTime)
                continue;

            if (RebuildPassPipeline(i)) {
                pass.vertWriteTime = vertTime;
                pass.fragWriteTime = fragTime;
                SetPassShader(i, pass.shader.get());
                anyChanged = true;
                NV_LOG_INFO(("VK_RenderGraph: hot-reloaded pass " + std::to_string(i)).c_str());
            }
        }
        return anyChanged;
    }

    void VK_RenderGraph::ApplyPassDesc(PassPipeline& pass, const RHI::RHI_RenderPassDesc& desc) {
        pass.type = desc.m_Type;
        switch (desc.m_Type) {
        case RHI::RHI_RenderPassType::Fullscreen:
            pass.target = desc.m_Fullscreen.m_Target;
            pass.alphaBlend = desc.m_Fullscreen.m_AlphaBlend;
            pass.depthTest = desc.m_Fullscreen.m_DepthTest;
            pass.depthWrite = desc.m_Fullscreen.m_DepthWrite;
            pass.colorLoadOp = desc.m_Fullscreen.m_ClearColor ? RHI::RHI_LoadOp::Clear : RHI::RHI_LoadOp::Load;
            pass.clearDepth = desc.m_Fullscreen.m_ClearDepth;
            break;
        case RHI::RHI_RenderPassType::Geometry:
            pass.target = desc.m_Geometry.m_Target;
            pass.colorLoadOp = desc.m_Geometry.m_ColorLoadOp;
            pass.clearDepth = desc.m_Geometry.m_ClearDepth;
            pass.depthTest = desc.m_Geometry.m_DepthTest;
            pass.depthWrite = desc.m_Geometry.m_DepthWrite;
            pass.alphaBlend = false;
            break;
        case RHI::RHI_RenderPassType::ImGui:
            pass.target = desc.m_ImGui.m_Target;
            pass.colorLoadOp = desc.m_ImGui.m_ClearColor ? RHI::RHI_LoadOp::Clear : RHI::RHI_LoadOp::Load;
            pass.clearDepth = true;
            break;
        }
    }

    void VK_RenderGraph::AssignPassRenderPasses() {
        if (!m_Renderer) return;

        VkDevice device = m_Renderer->GetDevice();

        for (auto& pass : m_PassPipelines) {
            if (pass.renderPassBackBuffer != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, pass.renderPassBackBuffer, nullptr);
            }
            pass.renderPassBackBuffer = VK_NULL_HANDLE;

            pass.renderPassViewport = (pass.colorLoadOp == RHI::RHI_LoadOp::Load)
                ? m_ViewportColorLoadDepthClearPass
                : m_ViewportColorDepthClearPass;

            const VkImageLayout backBufferFinalLayout =
                (pass.type == RHI::RHI_RenderPassType::ImGui)
                    ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR
                    : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

            pass.renderPassBackBuffer = CreateColorDepthRenderPass(
                pass.colorLoadOp == RHI::RHI_LoadOp::Load ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR,
                pass.clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD,
                backBufferFinalLayout);
        }
    }

    void VK_RenderGraph::BeginRenderPasses(VkCommandBuffer cmd) {
        if (!m_Renderer) return;

        const bool useViewport = m_Renderer->HasViewportFramebuffer();
        const uint32_t width = useViewport ? m_Renderer->GetViewportWidth() : m_Renderer->GetSwapchainWidth();
        const uint32_t height = useViewport ? m_Renderer->GetViewportHeight() : m_Renderer->GetSwapchainHeight();
        if (width == 0 || height == 0) return;

        for (size_t i = 0; i < m_Passes.size(); ++i) {
            if (m_Passes[i].m_Type == RHI::RHI_RenderPassType::ImGui)
                break;

            if (m_Passes[i].m_Type == RHI::RHI_RenderPassType::Fullscreen) {
                auto& pass = m_PassPipelines[i];
                const bool passViewport = useViewport && pass.target == RHI::RHI_RenderTarget::Viewport;
                VkFramebuffer fb = passViewport
                    ? m_Renderer->GetViewportFramebuffer()
                    : m_Renderer->GetSwapchainFramebuffer(m_Renderer->GetAcquiredImageIndex());

                VkRenderPass gridRenderPass = VK_NULL_HANDLE;
                if (passViewport)
                    gridRenderPass = pass.renderPassViewport;

                if (!BeginPassRenderPass(cmd, pass, passViewport, fb, width, height, gridRenderPass))
                    continue;

                if (!passViewport)
                    m_SwapchainColorWritten = true;

                SetViewportScissor(cmd, width, height);

                if (pass.shader && pass.shader->IsValid()) {
                    pass.shader->ApplyParameters(cmd);
                    pass.shader->Bind(cmd);
                    DrawFullscreenQuad(cmd, *pass.shader);
                }

                vkCmdEndRenderPass(cmd);
                m_InsideRenderPass = false;
                continue;
            }

            if (m_Passes[i].m_Type == RHI::RHI_RenderPassType::Geometry) {
                auto& pass = m_PassPipelines[i];
                const bool passViewport = useViewport && pass.target == RHI::RHI_RenderTarget::Viewport;
                VkFramebuffer fb = passViewport
                    ? m_Renderer->GetViewportFramebuffer()
                    : m_Renderer->GetSwapchainFramebuffer(m_Renderer->GetAcquiredImageIndex());

                if (!BeginPassRenderPass(cmd, pass, passViewport, fb, width, height))
                    return;

                if (!passViewport)
                    m_SwapchainColorWritten = true;

                SetViewportScissor(cmd, width, height);

                m_ActivePassIndex = static_cast<int>(i);
                m_GeometryPassActive = true;
                return;
            }
        }
    }

    VkCommandBuffer VK_RenderGraph::GetCurrentCommandBuffer() {
        if (!m_Renderer) return VK_NULL_HANDLE;
        return m_Renderer->GetCurrentCommandBuffer();
    }

    RHI::RHI_Shaders* VK_RenderGraph::GetGeometryPassShader() const {
        if (m_GeometryPassIndex < 0) return nullptr;
        return GetPassShader(static_cast<size_t>(m_GeometryPassIndex));
    }

    void VK_RenderGraph::EnsureRenderPassesBegun() {
        if (m_RenderPassesBegun) return;
        if (!m_Renderer || !m_Renderer->IsFrameActive()) return;

        BeginRenderPasses(GetCurrentCommandBuffer());
        m_RenderPassesBegun = m_GeometryPassActive;
    }

    void VK_RenderGraph::OnBeginFrame() {
        m_RenderPassesBegun = false;
        m_ImGuiPassBegun = false;
        ResetFrameDynamicUBOs();
    }

    void VK_RenderGraph::OnEndFrame() {
        if (!m_Renderer || !m_Renderer->IsFrameActive()) return;

        if (!m_ImGuiPassBegun)
            OnTransitionToImGuiPass();

        EndActivePass(GetCurrentCommandBuffer());

        m_RenderPassesBegun = false;
        m_ImGuiPassBegun = false;
    }

    void VK_RenderGraph::OnDraw(const RHI::RHI_DrawCommand& cmd) {
        if (!m_Renderer || !m_Renderer->IsFrameActive() || !cmd.m_Mesh) return;

        EnsureRenderPassesBegun();
        if (!m_RenderPassesBegun) {
            NV_LOG_WARN("VK_RenderGraph::OnDraw called before geometry pass could start");
            return;
        }

        auto vkMesh = m_Renderer->GetOrUploadMesh(cmd.m_Mesh);
        if (!vkMesh) return;

        VkCommandBuffer vkCmd = GetCurrentCommandBuffer();

        if (auto* shader = GetGeometryPassShader()) {
            shader->Bind(vkCmd);
            shader->ApplyParameters(vkCmd);
        }

        vkMesh->SetCommandBuffer(vkCmd);
        vkMesh->Bind();
        vkCmdDraw(vkCmd, cmd.m_VertexCount, cmd.m_InstanceCount, cmd.m_FirstVertex, cmd.m_FirstInstance);
    }

    void VK_RenderGraph::OnDrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        if (!m_Renderer || !m_Renderer->IsFrameActive() || !cmd.m_Mesh) return;

        EnsureRenderPassesBegun();
        if (!m_RenderPassesBegun) {
            NV_LOG_WARN("VK_RenderGraph::OnDrawIndexed called before geometry pass could start");
            return;
        }

        auto vkMesh = m_Renderer->GetOrUploadMesh(cmd.m_Mesh);
        if (!vkMesh) return;

        VkCommandBuffer vkCmd = GetCurrentCommandBuffer();

        if (auto* shader = GetGeometryPassShader()) {
            shader->Bind(vkCmd);
            shader->ApplyParameters(vkCmd);
        }

        vkMesh->SetCommandBuffer(vkCmd);
        vkMesh->Bind();

        if (cmd.m_IndexType != RHI::RHI_IndexType::UInt32) {
            NV_LOG_WARN("VK_RenderGraph::OnDrawIndexed currently supports only UInt32 index buffers.");
            return;
        }

        vkCmdDrawIndexed(vkCmd, cmd.m_IndexCount, cmd.m_InstanceCount, cmd.m_FirstIndex, cmd.m_VertexOffset, cmd.m_FirstInstance);
    }

    void VK_RenderGraph::OnTransitionToImGuiPass() {
        if (!m_Renderer || !m_Renderer->IsFrameActive()) return;

        VkCommandBuffer cmd = GetCurrentCommandBuffer();

        if (m_RenderPassesBegun && m_GeometryPassActive) {
            EndGeometryPass(cmd);
            m_RenderPassesBegun = false;
        }

        if (!m_ImGuiPassBegun) {
            BeginImGuiPass(cmd, m_Renderer->GetAcquiredImageIndex());
            m_ImGuiPassBegun = m_InsideRenderPass;
        }
    }

    void VK_RenderGraph::TransitionViewportImageForSampling(VkCommandBuffer cmd) {
        if (!m_Renderer || !m_Renderer->HasViewportFramebuffer())
            return;

        VkImage viewportImage = m_Renderer->GetViewportImage();
        if (viewportImage == VK_NULL_HANDLE)
            return;

        VkImageMemoryBarrier barrier{};
        barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        barrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
        barrier.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = viewportImage;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);
    }

    void VK_RenderGraph::EndGeometryPass(VkCommandBuffer cmd) {
        if (!m_GeometryPassActive || !m_InsideRenderPass) return;
        vkCmdEndRenderPass(cmd);
        m_InsideRenderPass = false;
        m_GeometryPassActive = false;
        m_ActivePassIndex = -1;
        TransitionViewportImageForSampling(cmd);
    }

    void VK_RenderGraph::BeginImGuiPass(VkCommandBuffer cmd, uint32_t swapchainImageIndex) {
        if (!m_Renderer) return;

        const uint32_t width = m_Renderer->GetSwapchainWidth();
        const uint32_t height = m_Renderer->GetSwapchainHeight();
        if (width == 0 || height == 0) return;

        if (m_ImGuiPassIndex < 0) {
            SetViewportScissor(cmd, width, height);
            return;
        }

        auto& pass = m_PassPipelines[static_cast<size_t>(m_ImGuiPassIndex)];
        VkFramebuffer fb = m_Renderer->GetSwapchainFramebuffer(swapchainImageIndex);

        // Pick the render pass whose color initial layout matches the image's real state:
        // - if a previous pass already drew this swapchain image, LOAD (preserve) it
        //   (initial layout COLOR_ATTACHMENT_OPTIMAL),
        // - otherwise CLEAR it (initial layout UNDEFINED, accepts UNDEFINED/PRESENT_SRC_KHR).
        // Both end in PRESENT_SRC_KHR ready for presentation.
        VkRenderPass renderPass = (m_SwapchainColorWritten && m_BackBufferLoadRenderPass != VK_NULL_HANDLE)
            ? m_BackBufferLoadRenderPass
            : m_BackBufferRenderPass;

        if (!BeginPassRenderPass(cmd, pass, false, fb, width, height, renderPass))
            return;

        SetViewportScissor(cmd, width, height);
        m_ActivePassIndex = m_ImGuiPassIndex;
    }

    void VK_RenderGraph::EndActivePass(VkCommandBuffer cmd) {
        if (!m_InsideRenderPass) return;
        vkCmdEndRenderPass(cmd);
        m_InsideRenderPass = false;
        m_ActivePassIndex = -1;
        m_GeometryPassActive = false;
    }

    VkRenderPass VK_RenderGraph::CreateColorDepthRenderPass(VkAttachmentLoadOp colorLoad, VkAttachmentLoadOp depthLoad, VkImageLayout finalColorLayout, VkImageLayout colorInitialLayout) const {
        if (!m_Renderer) return VK_NULL_HANDLE;

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = m_Renderer->GetSwapchainImageFormat();
        colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        colorAttachment.loadOp = colorLoad;
        colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        if (colorLoad == VK_ATTACHMENT_LOAD_OP_LOAD) {
            colorAttachment.initialLayout = (colorInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED)
                ? colorInitialLayout
                : VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        } else if (colorInitialLayout != VK_IMAGE_LAYOUT_UNDEFINED) {
            colorAttachment.initialLayout = colorInitialLayout;
        } else {
            colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        }
        colorAttachment.finalLayout = finalColorLayout;

        VkAttachmentDescription depthAttachment{};
        depthAttachment.format = m_DepthFormat;
        depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
        depthAttachment.loadOp = depthLoad;
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depthAttachment.initialLayout = (depthLoad == VK_ATTACHMENT_LOAD_OP_LOAD)
            ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
            : VK_IMAGE_LAYOUT_UNDEFINED;
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

        VkRenderPass renderPass = VK_NULL_HANDLE;
        VkResult res = vkCreateRenderPass(m_Renderer->GetDevice(), &renderPassInfo, nullptr, &renderPass);
        CheckVkResult(res);
        return (res == VK_SUCCESS) ? renderPass : VK_NULL_HANDLE;
    }

    VkRenderPass VK_RenderGraph::SelectRenderPass(const PassPipeline& pass, bool useViewport) const {
        if (useViewport)
            return pass.renderPassViewport;
        if (pass.type == RHI::RHI_RenderPassType::ImGui)
            return m_BackBufferRenderPass;
        return pass.renderPassBackBuffer;
    }

    bool VK_RenderGraph::BeginPassRenderPass(VkCommandBuffer cmd, const PassPipeline& pass, bool useViewport,
        VkFramebuffer framebuffer, uint32_t width, uint32_t height, VkRenderPass renderPassOverride)
    {
        std::array<VkClearValue, 2> clearValues{};
        clearValues[0].color = { 0.0f, 0.0f, 0.0f, 1.0f };
        clearValues[1].depthStencil = { 1.0f, 0 };

        VkRenderPassBeginInfo rpBegin{};
        rpBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpBegin.renderPass = renderPassOverride != VK_NULL_HANDLE
            ? renderPassOverride
            : SelectRenderPass(pass, useViewport);
        if (rpBegin.renderPass == VK_NULL_HANDLE || framebuffer == VK_NULL_HANDLE) {
            NV_LOG_WARN("VK_RenderGraph::BeginPassRenderPass - invalid render pass or framebuffer");
            return false;
        }
        rpBegin.framebuffer = framebuffer;
        rpBegin.renderArea = { {0, 0}, { width, height } };
        rpBegin.clearValueCount = static_cast<uint32_t>(clearValues.size());
        rpBegin.pClearValues = clearValues.data();

        vkCmdBeginRenderPass(cmd, &rpBegin, VK_SUBPASS_CONTENTS_INLINE);
        m_InsideRenderPass = true;
        return true;
    }

    void VK_RenderGraph::SetViewportScissor(VkCommandBuffer cmd, uint32_t width, uint32_t height) {
        VkViewport viewport{};
        viewport.x = 0.0f;
        viewport.y = 0.0f;
        viewport.width = static_cast<float>(width);
        viewport.height = static_cast<float>(height);
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &viewport);

        VkRect2D scissor{};
        scissor.offset = { 0, 0 };
        scissor.extent = { width, height };
        vkCmdSetScissor(cmd, 0, 1, &scissor);
    }

    void VK_RenderGraph::DrawFullscreenQuad(VkCommandBuffer cmd, VK_Shaders& /*shader*/) {
        if (m_FullscreenQuadBuffer == VK_NULL_HANDLE) return;
        VkDeviceSize offsets[] = { 0 };
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_FullscreenQuadBuffer, offsets);
        vkCmdDraw(cmd, 6, 1, 0, 0);
    }

    bool VK_RenderGraph::CreatePassPipeline(size_t passIndex, const RHI::RHI_RenderPassDesc& desc) {
        return RebuildPassPipeline(passIndex, desc);
    }

    bool VK_RenderGraph::RebuildPassPipeline(size_t passIndex) {
        if (passIndex >= m_Passes.size()) return false;
        return RebuildPassPipeline(passIndex, m_Passes[passIndex]);
    }

    bool VK_RenderGraph::RebuildPassPipeline(size_t passIndex, const RHI::RHI_RenderPassDesc& desc) {
        if (!m_Renderer) return false;

        auto& pass = m_PassPipelines[passIndex];
        VkDevice device = m_Renderer->GetDevice();

        ApplyPassDesc(pass, desc);

        RHI::RHI_GraphicsShaderDesc shaderDesc{};
        bool isFullscreen = false;
        bool isGeometry = false;

        switch (desc.m_Type) {
        case RHI::RHI_RenderPassType::Fullscreen:
            shaderDesc = desc.m_Fullscreen.m_Shaders;
            isFullscreen = true;
            break;
        case RHI::RHI_RenderPassType::Geometry:
            shaderDesc = desc.m_Geometry.m_Shaders;
            isGeometry = true;
            break;
        case RHI::RHI_RenderPassType::ImGui:
            return true;
        }

        RHI::RHI_ShaderCompileResult vertOut{}, fragOut{};
        if (!CompileGraphicsShaders(shaderDesc, vertOut, fragOut)) {
            NV_LOG_WARN(("VK_RenderGraph: shader compile failed for pass " + std::to_string(passIndex)).c_str());
            return false;
        }

        DestroyPassPipeline(pass);

        VK_ShaderModule vertModule, fragModule;
        if (!vertModule.Create(device, vertOut.m_Binary) || !fragModule.Create(device, fragOut.m_Binary))
            return false;

        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule.GetModule();
        stages[0].pName = "main";
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragModule.GetModule();
        stages[1].pName = "main";

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        VkPipelineInputAssemblyStateCreateInfo inputAsm{};
        inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        VkVertexInputBindingDescription vertexBinding{};
        std::array<VkVertexInputAttributeDescription, 6> vertexAttrs{};
        uint32_t vertexAttrCount = 0;

        if (isFullscreen) {
            vertexBinding.binding = 0;
            vertexBinding.stride = sizeof(float) * 4;
            vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            vertexAttrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
            vertexAttrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 };
            vertexAttrCount = 2;
        } else if (isGeometry) {
            vertexBinding.binding = 0;
            vertexBinding.stride = sizeof(Renderer::Graphics::Vertex);
            vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

            vertexAttrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Position) };
            vertexAttrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Normal) };
            vertexAttrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Renderer::Graphics::Vertex, m_TexCoord) };
            vertexAttrs[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Color) };
            vertexAttrs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Tangent) };
            vertexAttrs[5] = { 5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Renderer::Graphics::Vertex, m_Bitangent) };
            vertexAttrCount = 6;
        }

        vertexInput.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vertexInput.vertexBindingDescriptionCount = vertexAttrCount > 0 ? 1u : 0u;
        vertexInput.pVertexBindingDescriptions = vertexAttrCount > 0 ? &vertexBinding : nullptr;
        vertexInput.vertexAttributeDescriptionCount = vertexAttrCount;
        vertexInput.pVertexAttributeDescriptions = vertexAttrCount > 0 ? vertexAttrs.data() : nullptr;

        VkPipelineViewportStateCreateInfo viewportState{};
        viewportState.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewportState.viewportCount = 1;
        viewportState.scissorCount = 1;

        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = isFullscreen ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = pass.depthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = pass.depthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = isFullscreen ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (pass.alphaBlend) {
            blendAttachment.blendEnable = VK_TRUE;
            blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
            blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
            blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
            blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
            blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
        }

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &blendAttachment;

        VkDynamicState dynamicStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = 2;
        dynamic.pDynamicStates = dynamicStates;

        RHI::RHI_ProgramReflection reflForVk =
            RHI::MergeProgramReflections({ vertOut.m_Reflection, fragOut.m_Reflection });
        MarkEngineDynamicBuffers(reflForVk);

        pass.setLayouts.clear();
        for (const auto& setInfo : reflForVk.m_Sets) {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            if (CreateDescriptorSetLayoutFromReflection(device, reflForVk, setInfo.m_Set, layout))
                pass.setLayouts.emplace_back(setInfo.m_Set, layout);
        }
        std::sort(pass.setLayouts.begin(), pass.setLayouts.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        if (pass.setLayouts.empty()) {
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        if (m_BufGlobals == VK_NULL_HANDLE) {
            const VkDeviceSize globalsSize = sizeof(Renderer::RHI::FrameUniforms);
            VkBufferCreateInfo bufInfo{};
            bufInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
            bufInfo.size = globalsSize;
            bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            bufInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            if (vkCreateBuffer(device, &bufInfo, nullptr, &m_BufGlobals) != VK_SUCCESS) return false;

            VkMemoryRequirements memReq{};
            vkGetBufferMemoryRequirements(device, m_BufGlobals, &memReq);
            const auto& memProps = m_Renderer->GetMemoryProperties();
            uint32_t memTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((memReq.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                    memTypeIndex = i;
                    break;
                }
            }
            if (memTypeIndex == UINT32_MAX) return false;

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memTypeIndex;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_BufGlobalsMemory) != VK_SUCCESS) return false;
            vkBindBufferMemory(device, m_BufGlobals, m_BufGlobalsMemory, 0);

            VkPhysicalDeviceProperties physProps{};
            vkGetPhysicalDeviceProperties(m_Renderer->GetPhysicalDevice(), &physProps);
            auto alignUp = [](VkDeviceSize v, VkDeviceSize a) -> VkDeviceSize {
                return (a > 0) ? ((v + a - 1) / a) * a : v;
            };

            const VkDeviceSize mvpSize = sizeof(Renderer::RHI::MVP);
            m_MvpDynamicStride = alignUp(mvpSize, physProps.limits.minUniformBufferOffsetAlignment);
            bufInfo.size = m_MvpDynamicStride * static_cast<VkDeviceSize>(MAX_MODEL_DRAWS);
            bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
            if (vkCreateBuffer(device, &bufInfo, nullptr, &m_BufMvp) != VK_SUCCESS) return false;
            vkGetBufferMemoryRequirements(device, m_BufMvp, &memReq);
            memTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((memReq.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                    memTypeIndex = i;
                    break;
                }
            }
            if (memTypeIndex == UINT32_MAX) return false;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memTypeIndex;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_BufMvpMemory) != VK_SUCCESS) return false;
            vkBindBufferMemory(device, m_BufMvp, m_BufMvpMemory, 0);

            const VkDeviceSize materialSize = sizeof(Renderer::RHI::Material);
            m_MaterialDynamicStride = alignUp(materialSize, physProps.limits.minUniformBufferOffsetAlignment);
            bufInfo.size = m_MaterialDynamicStride * static_cast<VkDeviceSize>(MAX_MODEL_DRAWS);
            if (vkCreateBuffer(device, &bufInfo, nullptr, &m_BufMaterials) != VK_SUCCESS) return false;
            vkGetBufferMemoryRequirements(device, m_BufMaterials, &memReq);
            memTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((memReq.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                    memTypeIndex = i;
                    break;
                }
            }
            if (memTypeIndex == UINT32_MAX) return false;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memTypeIndex;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_BufMaterialsMemory) != VK_SUCCESS) return false;
            vkBindBufferMemory(device, m_BufMaterials, m_BufMaterialsMemory, 0);

            const VkDeviceSize instanceSize = sizeof(Renderer::RHI::Instance) * MAX_MODEL_INSTANCES;
            bufInfo.size = instanceSize;
            bufInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
            if (vkCreateBuffer(device, &bufInfo, nullptr, &m_BufInstances) != VK_SUCCESS) return false;
            vkGetBufferMemoryRequirements(device, m_BufInstances, &memReq);
            memTypeIndex = UINT32_MAX;
            for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
                if ((memReq.memoryTypeBits & (1u << i)) &&
                    (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                    memTypeIndex = i;
                    break;
                }
            }
            if (memTypeIndex == UINT32_MAX) return false;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memTypeIndex;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &m_BufInstancesMemory) != VK_SUCCESS) return false;
            vkBindBufferMemory(device, m_BufInstances, m_BufInstancesMemory, 0);
            m_BufInstancesSize = instanceSize;
        }

        if (pass.descriptorSets.empty() && m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
            for (const auto& [setIndex, layout] : pass.setLayouts) {
                VkDescriptorSetAllocateInfo allocSetInfo{};
                allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
                allocSetInfo.descriptorPool = m_ImGuiDescriptorPool;
                allocSetInfo.descriptorSetCount = 1;
                allocSetInfo.pSetLayouts = &layout;
                VkDescriptorSet ds = VK_NULL_HANDLE;
                if (vkAllocateDescriptorSets(device, &allocSetInfo, &ds) != VK_SUCCESS) return false;
                pass.descriptorSets.emplace_back(setIndex, ds);
            }

            auto findDescriptorSet = [&](uint32_t set) -> VkDescriptorSet {
                for (const auto& [idx, ds] : pass.descriptorSets) if (idx == set) return ds;
                return VK_NULL_HANDLE;
            };
            auto writeEngineBuffer = [&](const char* name, VkBuffer buffer, VkDeviceSize range) {
                const RHI::RHI_BindingInfo* info = reflForVk.FindBindingByName(name);
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
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            };
            writeEngineBuffer(RHI::EngineResourceName::Frame, m_BufGlobals, sizeof(Renderer::RHI::FrameUniforms));
            writeEngineBuffer(RHI::EngineResourceName::Mvp, m_BufMvp, sizeof(Renderer::RHI::MVP));
            writeEngineBuffer(RHI::EngineResourceName::Instances, m_BufInstances, m_BufInstancesSize);
            writeEngineBuffer(RHI::EngineResourceName::Material, m_BufMaterials, sizeof(Renderer::RHI::Material));
        }

        std::vector<VkDescriptorSetLayout> setLayouts;
        setLayouts.reserve(pass.setLayouts.size());
        for (const auto& [setIndex, layout] : pass.setLayouts) setLayouts.push_back(layout);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &pass.pipelineLayout) != VK_SUCCESS) {
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        VkRenderPass renderPassForPipeline = m_BackBufferRenderPass;
        if (renderPassForPipeline == VK_NULL_HANDLE)
            renderPassForPipeline = m_ViewportColorDepthClearPass;

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
        pipe.layout = pass.pipelineLayout;
        pipe.renderPass = renderPassForPipeline != VK_NULL_HANDLE ? renderPassForPipeline : VK_NULL_HANDLE;
        pipe.subpass = 0;

        if (vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipe, nullptr, &pass.pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, pass.pipelineLayout, nullptr);
            pass.pipelineLayout = VK_NULL_HANDLE;
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        vertModule.Destroy();
        fragModule.Destroy();

        pass.shader = std::make_unique<VK_Shaders>();
        pass.shader->SetPipeline(pass.pipeline, pass.pipelineLayout);
        const VkDeviceSize mvpBufferSize = m_MvpDynamicStride * static_cast<VkDeviceSize>(MAX_MODEL_DRAWS);
        const VkDeviceSize materialBufferSize = m_MaterialDynamicStride * static_cast<VkDeviceSize>(MAX_MODEL_DRAWS);
        pass.shader->SetSceneBuffers(device,
            m_BufGlobals, m_BufGlobalsMemory,
            m_BufMvp, m_BufMvpMemory, m_MvpDynamicStride, mvpBufferSize,
            m_BufMaterials, m_BufMaterialsMemory, m_MaterialDynamicStride, materialBufferSize,
            m_BufInstances, m_BufInstancesMemory, m_BufInstancesSize,
            &m_MvpDynamicOffset, &m_MaterialDynamicOffset,
            pass.descriptorSets);
        pass.shader->SetReflection(reflForVk);

        pass.shaderDesc = shaderDesc;
        pass.hasShaderDesc = true;
        pass.vertWriteTime = GetFileWriteTime(shaderDesc.m_VertexShader);
        pass.fragWriteTime = GetFileWriteTime(shaderDesc.m_FragmentShader);

        return true;
    }

    void VK_RenderGraph::ResetFrameDynamicUBOs() {
        m_MvpDynamicOffset = 0;
        m_MaterialDynamicOffset = 0;
        m_SwapchainColorWritten = false;
    }

    void VK_RenderGraph::DestroyPassPipeline(PassPipeline& pass) {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();

        if (m_ImGuiDescriptorPool != VK_NULL_HANDLE) {
            for (auto& [setIndex, ds] : pass.descriptorSets) {
                (void)setIndex;
                if (ds != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(device, m_ImGuiDescriptorPool, 1, &ds);
            }
        }
        pass.descriptorSets.clear();

        pass.shader.reset();
        if (pass.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pass.pipeline, nullptr);
            pass.pipeline = VK_NULL_HANDLE;
        }
        if (pass.pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, pass.pipelineLayout, nullptr);
            pass.pipelineLayout = VK_NULL_HANDLE;
        }
        for (auto& [setIndex, layout] : pass.setLayouts) {
            if (layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
        pass.setLayouts.clear();

        // Note: per-pass render passes are swapchain-lifetime resources, created by
        // AssignPassRenderPasses() and destroyed by DestroySwapchainResources().
        // They are intentionally NOT destroyed here (pipeline-lifetime) so that
        // rebuilding a pipeline (e.g. shader hot-reload) keeps the render passes valid.
    }

    // --- Swapchain resources (moved from VK_RendererResources) ---

    bool VK_RenderGraph::CreateBackBufferRenderPass() {
        if (m_BackBufferRenderPass != VK_NULL_HANDLE) return true;
        m_BackBufferRenderPass = CreateColorDepthRenderPass(
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        m_BackBufferLoadRenderPass = CreateColorDepthRenderPass(
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        return m_BackBufferRenderPass != VK_NULL_HANDLE && m_BackBufferLoadRenderPass != VK_NULL_HANDLE;
    }

    void VK_RenderGraph::DestroyBackBufferRenderPass() {
        if (m_Renderer) {
            VkDevice device = m_Renderer->GetDevice();
            if (m_BackBufferLoadRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, m_BackBufferLoadRenderPass, nullptr);
                m_BackBufferLoadRenderPass = VK_NULL_HANDLE;
            }
            if (m_BackBufferRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, m_BackBufferRenderPass, nullptr);
                m_BackBufferRenderPass = VK_NULL_HANDLE;
            }
        }
    }

    bool VK_RenderGraph::CreateSwapchainFramebuffers() {
        const auto& images = m_Renderer->GetSwapchainImageViews();
        m_SwapchainFramebuffers.assign(images.size(), VK_NULL_HANDLE);

        if (m_SwapchainDepthImages.size() != images.size()) {
            NV_LOG_ERROR("VK_RenderGraph::CreateSwapchainFramebuffers - depth image count mismatch");
            return false;
        }

        for (size_t i = 0; i < images.size(); ++i) {
            std::array<VkImageView, 2> attachments = { images[i], m_SwapchainDepthImages[i].m_View };

            VkFramebufferCreateInfo framebufferInfo{};
            framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            framebufferInfo.renderPass = m_BackBufferRenderPass;
            framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
            framebufferInfo.pAttachments = attachments.data();
            framebufferInfo.width = m_Renderer->GetSwapchainWidth();
            framebufferInfo.height = m_Renderer->GetSwapchainHeight();
            framebufferInfo.layers = 1;

            if (vkCreateFramebuffer(m_Renderer->GetDevice(), &framebufferInfo, nullptr, &m_SwapchainFramebuffers[i]) != VK_SUCCESS)
                return false;
        }
        return true;
    }

    void VK_RenderGraph::DestroySwapchainFramebuffers() {
        if (!m_Renderer) return;
        for (auto& fb : m_SwapchainFramebuffers) {
            if (fb != VK_NULL_HANDLE) {
                vkDestroyFramebuffer(m_Renderer->GetDevice(), fb, nullptr);
                fb = VK_NULL_HANDLE;
            }
        }
        m_SwapchainFramebuffers.clear();
    }

    bool VK_RenderGraph::CreateImGuiDescriptorPool() {
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

        return vkCreateDescriptorPool(m_Renderer->GetDevice(), &poolInfo, nullptr, &m_ImGuiDescriptorPool) == VK_SUCCESS;
    }

    void VK_RenderGraph::DestroyImGuiDescriptorPool() {
        if (m_ImGuiDescriptorPool != VK_NULL_HANDLE && m_Renderer) {
            VkDevice device = m_Renderer->GetDevice();
            for (auto& pass : m_PassPipelines) {
                for (auto& [setIndex, ds] : pass.descriptorSets) {
                    (void)setIndex;
                    if (ds != VK_NULL_HANDLE)
                        vkFreeDescriptorSets(device, m_ImGuiDescriptorPool, 1, &ds);
                }
                pass.descriptorSets.clear();
            }

            if (m_BufInstances != VK_NULL_HANDLE) { vkDestroyBuffer(device, m_BufInstances, nullptr); m_BufInstances = VK_NULL_HANDLE; }
            if (m_BufInstancesMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Renderer->GetDevice(), m_BufInstancesMemory, nullptr); m_BufInstancesMemory = VK_NULL_HANDLE; }
            if (m_BufMaterials != VK_NULL_HANDLE) { vkDestroyBuffer(m_Renderer->GetDevice(), m_BufMaterials, nullptr); m_BufMaterials = VK_NULL_HANDLE; }
            if (m_BufMaterialsMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Renderer->GetDevice(), m_BufMaterialsMemory, nullptr); m_BufMaterialsMemory = VK_NULL_HANDLE; }
            if (m_BufMvp != VK_NULL_HANDLE) { vkDestroyBuffer(m_Renderer->GetDevice(), m_BufMvp, nullptr); m_BufMvp = VK_NULL_HANDLE; }
            if (m_BufMvpMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Renderer->GetDevice(), m_BufMvpMemory, nullptr); m_BufMvpMemory = VK_NULL_HANDLE; }
            if (m_BufGlobals != VK_NULL_HANDLE) { vkDestroyBuffer(m_Renderer->GetDevice(), m_BufGlobals, nullptr); m_BufGlobals = VK_NULL_HANDLE; }
            if (m_BufGlobalsMemory != VK_NULL_HANDLE) { vkFreeMemory(m_Renderer->GetDevice(), m_BufGlobalsMemory, nullptr); m_BufGlobalsMemory = VK_NULL_HANDLE; }

            vkDestroyDescriptorPool(m_Renderer->GetDevice(), m_ImGuiDescriptorPool, nullptr);
            m_ImGuiDescriptorPool = VK_NULL_HANDLE;
        }
    }

    bool VK_RenderGraph::CreateDepthResources() {
        const std::vector<VkFormat> candidates = {
            VK_FORMAT_D32_SFLOAT,
            VK_FORMAT_D32_SFLOAT_S8_UINT,
            VK_FORMAT_D24_UNORM_S8_UINT
        };
        m_DepthFormat = VK_FORMAT_UNDEFINED;

        for (VkFormat format : candidates) {
            VkFormatProperties props{};
            vkGetPhysicalDeviceFormatProperties(m_Renderer->GetPhysicalDevice(), format, &props);
            if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                m_DepthFormat = format;
                break;
            }
        }
        if (m_DepthFormat == VK_FORMAT_UNDEFINED) return false;

        const uint32_t imageCount = m_Renderer->GetSwapchainImageCount();
        if (imageCount == 0) return false;

        m_SwapchainDepthImages.clear();
        m_SwapchainDepthImages.resize(imageCount);

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { m_Renderer->GetSwapchainWidth(), m_Renderer->GetSwapchainHeight(), 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = m_DepthFormat;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        VkDevice device = m_Renderer->GetDevice();
        const auto& memProps = m_Renderer->GetMemoryProperties();

        for (uint32_t i = 0; i < imageCount; ++i) {
            auto& depth = m_SwapchainDepthImages[i];
            if (vkCreateImage(device, &imageInfo, nullptr, &depth.m_Image) != VK_SUCCESS)
                return false;

            VkMemoryRequirements memReq{};
            vkGetImageMemoryRequirements(device, depth.m_Image, &memReq);
            uint32_t memTypeIndex = UINT32_MAX;
            for (uint32_t j = 0; j < memProps.memoryTypeCount; ++j) {
                if ((memReq.memoryTypeBits & (1u << j)) &&
                    (memProps.memoryTypes[j].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) {
                    memTypeIndex = j;
                    break;
                }
            }
            if (memTypeIndex == UINT32_MAX) return false;

            VkMemoryAllocateInfo allocInfo{};
            allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            allocInfo.allocationSize = memReq.size;
            allocInfo.memoryTypeIndex = memTypeIndex;
            if (vkAllocateMemory(device, &allocInfo, nullptr, &depth.m_Memory) != VK_SUCCESS)
                return false;
            vkBindImageMemory(device, depth.m_Image, depth.m_Memory, 0);

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depth.m_Image;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = m_DepthFormat;
            viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
            viewInfo.subresourceRange.levelCount = 1;
            viewInfo.subresourceRange.layerCount = 1;

            if (vkCreateImageView(device, &viewInfo, nullptr, &depth.m_View) != VK_SUCCESS)
                return false;
        }

        return true;
    }

    void VK_RenderGraph::DestroyDepthResources() {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();
        for (auto& depth : m_SwapchainDepthImages) {
            if (depth.m_View != VK_NULL_HANDLE) { vkDestroyImageView(device, depth.m_View, nullptr); depth.m_View = VK_NULL_HANDLE; }
            if (depth.m_Image != VK_NULL_HANDLE) { vkDestroyImage(device, depth.m_Image, nullptr); depth.m_Image = VK_NULL_HANDLE; }
            if (depth.m_Memory != VK_NULL_HANDLE) { vkFreeMemory(device, depth.m_Memory, nullptr); depth.m_Memory = VK_NULL_HANDLE; }
        }
        m_SwapchainDepthImages.clear();
    }

    void VK_RenderGraph::CreateFullscreenQuadBuffer() {
        VkDevice device = m_Renderer->GetDevice();
        if (device == VK_NULL_HANDLE || m_FullscreenQuadBuffer != VK_NULL_HANDLE) return;

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
        if (vkCreateBuffer(device, &bufInfo, nullptr, &m_FullscreenQuadBuffer) != VK_SUCCESS) return;

        VkMemoryRequirements memReq{};
        vkGetBufferMemoryRequirements(device, m_FullscreenQuadBuffer, &memReq);
        const auto& memProps = m_Renderer->GetMemoryProperties();
        uint32_t memTypeIndex = UINT32_MAX;
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((memReq.memoryTypeBits & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))) {
                memTypeIndex = i;
                break;
            }
        }
        if (memTypeIndex == UINT32_MAX) return;

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memTypeIndex;
        if (vkAllocateMemory(device, &allocInfo, nullptr, &m_FullscreenQuadMemory) != VK_SUCCESS) return;
        vkBindBufferMemory(device, m_FullscreenQuadBuffer, m_FullscreenQuadMemory, 0);

        void* data = nullptr;
        if (vkMapMemory(device, m_FullscreenQuadMemory, 0, sizeof(kQuadVerts), 0, &data) == VK_SUCCESS) {
            std::memcpy(data, kQuadVerts, sizeof(kQuadVerts));
            vkUnmapMemory(device, m_FullscreenQuadMemory);
        }
    }

    void VK_RenderGraph::DestroyFullscreenQuadBuffer() {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();
        if (m_FullscreenQuadBuffer != VK_NULL_HANDLE) { vkDestroyBuffer(device, m_FullscreenQuadBuffer, nullptr); m_FullscreenQuadBuffer = VK_NULL_HANDLE; }
        if (m_FullscreenQuadMemory != VK_NULL_HANDLE) { vkFreeMemory(device, m_FullscreenQuadMemory, nullptr); m_FullscreenQuadMemory = VK_NULL_HANDLE; }
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan