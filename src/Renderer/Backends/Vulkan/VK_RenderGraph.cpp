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
            case RK::ConstantBuffer:     return b.m_IsDynamicUniformBuffer ? VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case RK::StructuredBuffer:   return b.m_IsDynamicUniformBuffer ? VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case RK::Texture:            return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case RK::Sampler:            return VK_DESCRIPTOR_TYPE_SAMPLER;
            case RK::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case RK::RWTexture:          return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            case RK::RWStructuredBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            default:                     return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }

    void MarkEngineDynamicBuffers(RHI::RHI_ProgramReflection& refl) {
        const char* dynamicNames[] = {
            RHI::EngineResourceName::Frame,
            RHI::EngineResourceName::Mvp,
            RHI::EngineResourceName::Material,
        };
        for (const char* name : dynamicNames) {
            const RHI::RHI_BindingKey* key = refl.FindBindingKeyByName(name);
            if (!key) continue;
            if (auto* set = const_cast<RHI::RHI_DescriptorSetLayoutInfo*>(refl.FindSet(key->m_Set))) {
                for (auto& b : set->m_Bindings) {
                    if (b.m_Key.m_Binding == key->m_Binding &&
                        (b.m_Kind == RHI::RHI_ResourceKind::ConstantBuffer || b.m_Kind == RHI::RHI_ResourceKind::StructuredBuffer))
                        b.m_IsDynamicUniformBuffer = true;
                }
            }
        }
    }

    bool CreateDescriptorSetLayoutFromReflection(
        VkDevice device, const RHI::RHI_ProgramReflection& refl, uint32_t setIndex, VkDescriptorSetLayout& outLayout)
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

    std::filesystem::file_time_type GetFileWriteTime(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::last_write_time(path, ec);
    }

    bool CompileGraphicsShaders(
        const RHI::RHI_ShaderDesc& desc,
        RHI::RHI_ShaderCompileResult& vertOut,
        RHI::RHI_ShaderCompileResult& fragOut)
    {
        if (!desc.m_Vertex || !desc.m_Fragment) {
            NV_LOG_WARN("VK_PipelineCache: RegisterShader requires vertex and fragment ShaderAssets.");
            return false;
        }

        if (!desc.m_Vertex->Compile()) {
            NV_LOG_WARN(("VK_PipelineCache: vertex compile failed:\n" + desc.m_Vertex->GetLastLog()).c_str());
            return false;
        }
        if (!desc.m_Fragment->Compile()) {
            NV_LOG_WARN(("VK_PipelineCache: fragment compile failed:\n" + desc.m_Fragment->GetLastLog()).c_str());
            return false;
        }

        vertOut.m_Success = true;
        vertOut.m_Binary = desc.m_Vertex->GetBinary();
        vertOut.m_Format = desc.m_Vertex->GetBinaryFormat();
        vertOut.m_Source = desc.m_Vertex->GetSource();
        vertOut.m_Reflection = desc.m_Vertex->GetReflection();
        vertOut.m_Stage = desc.m_Vertex->GetStage();

        fragOut.m_Success = true;
        fragOut.m_Binary = desc.m_Fragment->GetBinary();
        fragOut.m_Format = desc.m_Fragment->GetBinaryFormat();
        fragOut.m_Source = desc.m_Fragment->GetSource();
        fragOut.m_Reflection = desc.m_Fragment->GetReflection();
        fragOut.m_Stage = desc.m_Fragment->GetStage();
        return true;
    }

    // -------------------------------------------------------------------------
    // VK_PipelineCache
    // -------------------------------------------------------------------------

    bool VK_PipelineCache::Create(VK_Renderer& renderer, const std::vector<RHI::RHI_ShaderDesc>& shaders, VkFormat colorFormat, VkFormat depthFormat) {
        Destroy();
        m_Renderer = &renderer;
        m_ColorFormat = colorFormat;
        m_DepthFormat = depthFormat != VK_FORMAT_UNDEFINED ? depthFormat : VK_FORMAT_D32_SFLOAT;

        VkPipelineCacheCreateInfo cacheInfo{};
        cacheInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
        if (vkCreatePipelineCache(renderer.GetDevice(), &cacheInfo, nullptr, &m_VkPipelineCache) != VK_SUCCESS)
            return false;

        if (!CreateDescriptorPool())
            return false;

        if (!CreateRenderPass(m_ColorFormat, m_DepthFormat))
            return false;

        // One entry per declared shader; pipelines are built lazily on first use.
        m_Entries.resize(shaders.size());
        for (size_t i = 0; i < shaders.size(); ++i)
            m_Entries[i].desc = shaders[i];

        return true;
    }

    bool VK_PipelineCache::CreateRenderPass(VkFormat colorFormat, VkFormat depthFormat) {
        if (!m_Renderer || colorFormat == VK_FORMAT_UNDEFINED)
            return false;

        VkDevice device = m_Renderer->GetDevice();

        VkAttachmentDescription attachments[2]{};
        attachments[0].format = colorFormat;
        attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

        attachments[1].format = depthFormat;
        attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

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
        // Must match CreateColorDepthRenderPass — dependency masks are part of
        // render-pass compatibility with pipelines created against this RP.
        dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

        VkRenderPassCreateInfo rpInfo{};
        rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
        rpInfo.attachmentCount = 2;
        rpInfo.pAttachments = attachments;
        rpInfo.subpassCount = 1;
        rpInfo.pSubpasses = &subpass;
        rpInfo.dependencyCount = 1;
        rpInfo.pDependencies = &dependency;
        return vkCreateRenderPass(device, &rpInfo, nullptr, &m_RenderPass) == VK_SUCCESS;
    }

    void VK_PipelineCache::Destroy() {
        for (auto& entry : m_Entries)
            DestroyEntry(entry);
        m_Entries.clear();

        DestroyEngineBuffers();

        if (m_Renderer) {
            VkDevice device = m_Renderer->GetDevice();
            if (m_RenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, m_RenderPass, nullptr);
                m_RenderPass = VK_NULL_HANDLE;
            }
            if (m_VkPipelineCache != VK_NULL_HANDLE) {
                vkDestroyPipelineCache(device, m_VkPipelineCache, nullptr);
                m_VkPipelineCache = VK_NULL_HANDLE;
            }
            if (m_DescriptorPool != VK_NULL_HANDLE) {
                vkDestroyDescriptorPool(device, m_DescriptorPool, nullptr);
                m_DescriptorPool = VK_NULL_HANDLE;
            }
        }
        m_Renderer = nullptr;
    }

    bool VK_PipelineCache::CreateDescriptorPool() {
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

        return vkCreateDescriptorPool(m_Renderer->GetDevice(), &poolInfo, nullptr, &m_DescriptorPool) == VK_SUCCESS;
    }

    // Creates the 3 ConstantBuffer<T> fields of `ParameterBlock<NovaEngine> nova;` through the RHI
    // buffer API — the same API available to App code for its own custom ConstantBuffer/StructuredBuffer/
    // RWStructuredBuffer resources (see IRenderer::CreateConstantBuffer and friends).
    bool VK_PipelineCache::CreateEngineBuffers() {
        if (m_Engine.IsValid())
            return true;

        m_Engine.m_Frame = RHI::CreateConstantBuffer<RHI::FrameUniforms>(*m_Renderer, 1, RHI::EngineResourceName::Frame);
        m_Engine.m_Mvp = RHI::CreateConstantBuffer<RHI::MVP>(*m_Renderer, MAX_MODEL_DRAWS, RHI::EngineResourceName::Mvp);
        m_Engine.m_Material = RHI::CreateConstantBuffer<RHI::Material>(*m_Renderer, MAX_MODEL_DRAWS, RHI::EngineResourceName::Material);

        return m_Engine.IsValid();
    }

    void VK_PipelineCache::DestroyEngineBuffers() {
        if (!m_Renderer) return;
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Material);
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Mvp);
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Frame);
        m_Engine = RHI::RHI_EngineParameterBlock{};
    }

    // Resets this frame-in-flight's per-draw ring cursor for `nova.mvp` / `nova.material`, so the
    // first draw of the frame writes at the start of its region instead of continuing from
    // wherever the previous frame using this slot left off.
    void VK_PipelineCache::ResetFrameDynamicUBOs() {
        if (!m_Renderer) return;
        m_Renderer->GetGpuBufferPool().ResetDynamicCursors(m_Renderer->GetCurrentFrameInFlight());
    }

    RHI::RHI_Shaders* VK_PipelineCache::Get(RHI::RHI_ShaderHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Entries.size())
            return nullptr;

        PipelineEntry& entry = m_Entries[handle.m_Index];
        if (entry.shader)
            return entry.shader.get();

        // Retry on every call: a shader that failed to compile recovers once it is fixed.
        if (!BuildPipeline(entry))
            return nullptr;

        return entry.shader.get();
    }

    bool VK_PipelineCache::ReloadChangedShaders() {
        if (!m_Renderer) return false;

        bool anyChanged = false;
        for (auto& entry : m_Entries) {
            if (!entry.shader || !entry.desc.m_Vertex || !entry.desc.m_Fragment)
                continue;

            const auto vertTime = GetFileWriteTime(entry.desc.m_Vertex->GetPath());
            const auto fragTime = GetFileWriteTime(entry.desc.m_Fragment->GetPath());
            if (vertTime == entry.vertWriteTime && fragTime == entry.fragWriteTime)
                continue;

            // Force recompilation through the asset layer before rebuilding the pipeline.
            entry.desc.m_Vertex->Recompile();
            entry.desc.m_Fragment->Recompile();

            PipelineEntry rebuilt{};
            rebuilt.desc = entry.desc;
            if (BuildPipeline(rebuilt)) {
                DestroyEntry(entry);
                entry = std::move(rebuilt);
                anyChanged = true;
                NV_LOG_INFO(("VK_PipelineCache: hot-reloaded pipeline '" + entry.desc.m_Name + "'").c_str());
            }
        }
        return anyChanged;
    }

    void VK_PipelineCache::DestroyEntry(PipelineEntry& entry) {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();

        if (m_DescriptorPool != VK_NULL_HANDLE) {
            for (auto& [setIndex, ds] : entry.descriptorSets) {
                (void)setIndex;
                if (ds != VK_NULL_HANDLE)
                    vkFreeDescriptorSets(device, m_DescriptorPool, 1, &ds);
            }
        }
        entry.descriptorSets.clear();
        entry.shader.reset();

        if (entry.pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, entry.pipeline, nullptr);
            entry.pipeline = VK_NULL_HANDLE;
        }
        if (entry.pipelineLayout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, entry.pipelineLayout, nullptr);
            entry.pipelineLayout = VK_NULL_HANDLE;
        }
        for (auto& [setIndex, layout] : entry.setLayouts) {
            (void)setIndex;
            if (layout != VK_NULL_HANDLE)
                vkDestroyDescriptorSetLayout(device, layout, nullptr);
        }
        entry.setLayouts.clear();
    }

    bool VK_PipelineCache::BuildPipeline(PipelineEntry& entry) {
        if (!m_Renderer) return false;

        RHI::RHI_ShaderCompileResult vertOut{}, fragOut{};
        if (!CompileGraphicsShaders(entry.desc, vertOut, fragOut))
            return false;

        if (!CreateEngineBuffers())
            return false;

        VkDevice device = m_Renderer->GetDevice();

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

        const bool isFullscreen = entry.desc.m_VertexLayout == RHI::RHI_VertexLayout::FullscreenQuad;
        const bool isMesh = entry.desc.m_VertexLayout == RHI::RHI_VertexLayout::Mesh;

        if (isFullscreen) {
            vertexBinding.binding = 0;
            vertexBinding.stride = sizeof(float) * 4;
            vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            vertexAttrs[0] = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
            vertexAttrs[1] = { 1, 0, VK_FORMAT_R32G32_SFLOAT, sizeof(float) * 2 };
            vertexAttrCount = 2;
        } else if (isMesh) {
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
        depthStencil.depthTestEnable = entry.desc.m_DepthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = entry.desc.m_DepthWrite ? VK_TRUE : VK_FALSE;
        depthStencil.depthCompareOp = isFullscreen ? VK_COMPARE_OP_LESS_OR_EQUAL : VK_COMPARE_OP_LESS;

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (entry.desc.m_AlphaBlend) {
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

        entry.setLayouts.clear();
        for (const auto& setInfo : reflForVk.m_Sets) {
            VkDescriptorSetLayout layout = VK_NULL_HANDLE;
            if (CreateDescriptorSetLayoutFromReflection(device, reflForVk, setInfo.m_Set, layout))
                entry.setLayouts.emplace_back(setInfo.m_Set, layout);
        }
        std::sort(entry.setLayouts.begin(), entry.setLayouts.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });

        if (entry.setLayouts.empty()) {
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        entry.descriptorSets.clear();
        for (const auto& [setIndex, layout] : entry.setLayouts) {
            VkDescriptorSetAllocateInfo allocSetInfo{};
            allocSetInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocSetInfo.descriptorPool = m_DescriptorPool;
            allocSetInfo.descriptorSetCount = 1;
            allocSetInfo.pSetLayouts = &layout;
            VkDescriptorSet ds = VK_NULL_HANDLE;
            if (vkAllocateDescriptorSets(device, &allocSetInfo, &ds) != VK_SUCCESS) return false;
            entry.descriptorSets.emplace_back(setIndex, ds);
        }

        auto findDescriptorSet = [&](uint32_t set) -> VkDescriptorSet {
            for (const auto& [idx, ds] : entry.descriptorSets) if (idx == set) return ds;
            return VK_NULL_HANDLE;
        };
        // One-time descriptor setup for the engine's ConstantBuffer<T> fields: always dynamic
        // (offset = 0, range = one element), the per-frame/per-draw region is selected later via
        // the dynamic offset supplied to vkCmdBindDescriptorSets (see VK_Shaders::BindDescriptorSets).
        auto writeEngineBuffer = [&](const char* name, RHI::RHI_GpuBufferHandle handle) {
            const RHI::RHI_BindingInfo* info = reflForVk.FindBindingByName(name);
            VkBuffer buffer = VK_NULL_HANDLE;
            VkDeviceSize range = 0;
            if (!info || !m_Renderer->GetGpuBufferPool().GetDescriptorInfo(handle, buffer, range))
                return;
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
        writeEngineBuffer(RHI::EngineResourceName::Frame, m_Engine.m_Frame);
        writeEngineBuffer(RHI::EngineResourceName::Mvp, m_Engine.m_Mvp);
        writeEngineBuffer(RHI::EngineResourceName::Material, m_Engine.m_Material);

        std::vector<VkDescriptorSetLayout> setLayouts;
        setLayouts.reserve(entry.setLayouts.size());
        for (const auto& [setIndex, layout] : entry.setLayouts) setLayouts.push_back(layout);

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &entry.pipelineLayout) != VK_SUCCESS) {
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        // Compatible render pass is created up-front in Create() to match the
        // actual scene color target (swapchain or offscreen panel texture).
        if (m_RenderPass == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_PipelineCache::BuildPipeline - compatible render pass is missing");
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
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
        pipe.pDepthStencilState = &depthStencil;
        pipe.pColorBlendState = &blend;
        pipe.pDynamicState = &dynamic;
        pipe.layout = entry.pipelineLayout;
        pipe.renderPass = m_RenderPass;
        pipe.subpass = 0;

        if (vkCreateGraphicsPipelines(device, m_VkPipelineCache, 1, &pipe, nullptr, &entry.pipeline) != VK_SUCCESS) {
            vkDestroyPipelineLayout(device, entry.pipelineLayout, nullptr);
            entry.pipelineLayout = VK_NULL_HANDLE;
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        vertModule.Destroy();
        fragModule.Destroy();

        entry.shader = std::make_unique<VK_Shaders>();
        entry.shader->SetPipeline(entry.pipeline, entry.pipelineLayout);
        entry.shader->SetEngineBuffers(m_Renderer, m_Engine, entry.descriptorSets);
        entry.shader->SetReflection(reflForVk);

        entry.vertWriteTime = GetFileWriteTime(entry.desc.m_Vertex->GetPath());
        entry.fragWriteTime = GetFileWriteTime(entry.desc.m_Fragment->GetPath());
        return true;
    }

    // -------------------------------------------------------------------------
    // VK_RenderGraph
    // -------------------------------------------------------------------------

    VK_RenderGraph::VK_RenderGraph(RHI::RHI_RenderGraphData data) : IRenderGraph(std::move(data)) {}

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

        // Resolve a depth format early so the compatible pipeline render pass matches
        // the attachments created later for both swapchain and offscreen targets.
        VkFormat depthFormat = VK_FORMAT_UNDEFINED;
        {
            const std::vector<VkFormat> candidates = {
                VK_FORMAT_D32_SFLOAT,
                VK_FORMAT_D32_SFLOAT_S8_UINT,
                VK_FORMAT_D24_UNORM_S8_UINT
            };
            for (VkFormat format : candidates) {
                VkFormatProperties props{};
                vkGetPhysicalDeviceFormatProperties(renderer.GetPhysicalDevice(), format, &props);
                if (props.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
                    depthFormat = format;
                    break;
                }
            }
        }
        if (depthFormat == VK_FORMAT_UNDEFINED) {
            NV_LOG_ERROR("VK_RenderGraph::Create - no supported depth format");
            return false;
        }
        m_DepthFormat = depthFormat;

        if (!m_PipelineCache.Create(renderer, m_Data.m_Shaders, sceneColorFormat, depthFormat)) {
            NV_LOG_ERROR("VK_RenderGraph::Create - failed to create pipeline cache");
            return false;
        }

        if (!InitSwapchainResources()) {
            NV_LOG_ERROR("VK_RenderGraph::Create - failed to init swapchain resources");
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
        DestroySwapchainResources();
        m_PipelineCache.Destroy();
        m_Renderer = nullptr;
        m_Compiled = false;
    }

    void VK_RenderGraph::ReleaseImGuiTextures() {
        for (auto& texture : m_Textures) {
            if (texture.imguiTextureId == nullptr)
                continue;
            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr)
                ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(texture.imguiTextureId));
            texture.imguiTextureId = nullptr;
        }
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
            m_Textures[i].desc = m_Data.m_Textures[i];
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

            if (!CreateTexture(m_Textures[i], w, h))
                return false;
        }
        return true;
    }

    void VK_RenderGraph::DestroyTransientResources() {
        for (auto& tex : m_Textures)
            DestroyTexture(tex);
        m_Textures.clear();
    }

    bool VK_RenderGraph::CreateTexture(TextureResource& texture, uint32_t width, uint32_t height) {
        if (!m_Renderer) return false;

        const auto& desc = texture.desc.m_Desc;
        const VkFormat format = IsDepthFormat(desc.m_Format) ? m_DepthFormat : ToVkFormat(desc.m_Format);
        if (format == VK_FORMAT_UNDEFINED) return false;

        VkImageUsageFlags usage = 0;
        if (HasTextureUsage(desc.m_Usage, RHI::RHI_TextureUsage::ColorAttachment))
            usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (HasTextureUsage(desc.m_Usage, RHI::RHI_TextureUsage::DepthAttachment))
            usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        if (HasTextureUsage(desc.m_Usage, RHI::RHI_TextureUsage::Sampled))
            usage |= VK_IMAGE_USAGE_SAMPLED_BIT;
        if (HasTextureUsage(desc.m_Usage, RHI::RHI_TextureUsage::Storage))
            usage |= VK_IMAGE_USAGE_STORAGE_BIT;

        if (usage == 0)
            usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.extent = { width, height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.format = format;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = usage;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;

        if (!m_Renderer->GetMemoryAllocator().CreateImage(imageInfo, VK_MemoryLocation::GpuOnly, texture.image))
            return false;

        VkDevice device = m_Renderer->GetDevice();

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = texture.image.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format;
        viewInfo.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
        };
        viewInfo.subresourceRange.aspectMask = IsDepthFormat(desc.m_Format)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        // Framebuffer attachments require the identity swizzle.
        if (vkCreateImageView(device, &viewInfo, nullptr, &texture.view) != VK_SUCCESS)
            return false;

        texture.sampledView = texture.view;

        // Depth sampled in ImGui / shaders: separate view with R→RGB so grayscale
        // displays correctly without violating framebuffer identity-swizzle rules.
        if (IsDepthFormat(desc.m_Format) && HasTextureUsage(desc.m_Usage, RHI::RHI_TextureUsage::Sampled)) {
            viewInfo.components.r = VK_COMPONENT_SWIZZLE_R;
            viewInfo.components.g = VK_COMPONENT_SWIZZLE_R;
            viewInfo.components.b = VK_COMPONENT_SWIZZLE_R;
            viewInfo.components.a = VK_COMPONENT_SWIZZLE_ONE;
            if (vkCreateImageView(device, &viewInfo, nullptr, &texture.sampledView) != VK_SUCCESS)
                return false;
        }

        if (HasTextureUsage(desc.m_Usage, RHI::RHI_TextureUsage::Sampled)) {
            const bool isDepth = IsDepthFormat(desc.m_Format);
            VkSamplerCreateInfo samplerInfo{};
            samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            samplerInfo.magFilter = isDepth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            samplerInfo.minFilter = isDepth ? VK_FILTER_NEAREST : VK_FILTER_LINEAR;
            samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
            samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;

            if (vkCreateSampler(device, &samplerInfo, nullptr, &texture.sampler) != VK_SUCCESS)
                return false;

            texture.imguiTextureId = ImGui_ImplVulkan_AddTexture(
                texture.sampler, texture.sampledView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        texture.state = RHI::RHI_ResourceState::Undefined;
        return true;
    }

    void VK_RenderGraph::DestroyTexture(TextureResource& texture) {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();

        if (texture.imguiTextureId != nullptr) {
            // Backend may already be shut down during app teardown — only remove if alive.
            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr)
                ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(texture.imguiTextureId));
            texture.imguiTextureId = nullptr;
        }

        if (texture.framebuffer != VK_NULL_HANDLE) {
            vkDestroyFramebuffer(device, texture.framebuffer, nullptr);
            texture.framebuffer = VK_NULL_HANDLE;
        }
        if (texture.sampler != VK_NULL_HANDLE) {
            vkDestroySampler(device, texture.sampler, nullptr);
            texture.sampler = VK_NULL_HANDLE;
        }
        if (texture.sampledView != VK_NULL_HANDLE && texture.sampledView != texture.view) {
            vkDestroyImageView(device, texture.sampledView, nullptr);
            texture.sampledView = VK_NULL_HANDLE;
        }
        if (texture.view != VK_NULL_HANDLE) {
            vkDestroyImageView(device, texture.view, nullptr);
            texture.view = VK_NULL_HANDLE;
        }
        texture.sampledView = VK_NULL_HANDLE;
        m_Renderer->GetMemoryAllocator().DestroyImage(texture.image);
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
            texRes.m_Desc.m_Width = width;
            texRes.m_Desc.m_Height = height;
        }

        return CreateTransientResources();
    }

    void* VK_RenderGraph::GetTextureImGuiID(RHI::RHI_TextureHandle handle) const {
        if (!handle.IsValid() || handle.m_Index >= m_Textures.size())
            return nullptr;
        return m_Textures[handle.m_Index].imguiTextureId;
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
        return m_PipelineCache.ReloadChangedShaders();
    }

    bool VK_RenderGraph::EnsurePassRenderTarget(PassRenderTarget& rt, const RHI::RHI_RenderGraphPassDesc& pass) {
        if (!m_Renderer) return false;

        rt.colorAttachments.clear();
        rt.depthAttachment = {};

        for (RHI::RHI_TextureHandle handle : pass.m_WriteTextures) {
            if (!handle.IsValid() || handle.m_Index >= m_Textures.size())
                continue;
            const auto& texDesc = m_Textures[handle.m_Index].desc.m_Desc;
            if (m_Textures[handle.m_Index].desc.m_IsSwapchain)
                continue;

            if (IsDepthFormat(texDesc.m_Format))
                rt.depthAttachment = handle;
            else
                rt.colorAttachments.push_back(handle);
        }

        if (rt.colorAttachments.empty() && !rt.depthAttachment.IsValid())
            return true;

        const bool readsColor = [&]() {
            for (RHI::RHI_TextureHandle h : pass.m_ReadTextures) {
                if (!h.IsValid() || h.m_Index >= m_Textures.size()) continue;
                if (!IsDepthFormat(m_Textures[h.m_Index].desc.m_Desc.m_Format))
                    return true;
            }
            return false;
        }();

        const bool readsDepth = [&]() {
            for (RHI::RHI_TextureHandle h : pass.m_ReadTextures) {
                if (!h.IsValid() || h.m_Index >= m_Textures.size()) continue;
                if (IsDepthFormat(m_Textures[h.m_Index].desc.m_Desc.m_Format))
                    return true;
            }
            return false;
        }();

        VkFormat colorFormat = VK_FORMAT_R8G8B8A8_UNORM;
        if (!rt.colorAttachments.empty()) {
            const auto& first = m_Textures[rt.colorAttachments[0].m_Index].desc.m_Desc;
            colorFormat = ToVkFormat(first.m_Format);
        }

        const VkAttachmentLoadOp colorLoad = readsColor ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;
        const VkAttachmentLoadOp depthLoad = readsDepth ? VK_ATTACHMENT_LOAD_OP_LOAD : VK_ATTACHMENT_LOAD_OP_CLEAR;

        if (rt.renderPassClear == VK_NULL_HANDLE) {
            rt.renderPassClear = CreateColorDepthRenderPass(
                colorFormat, colorLoad, depthLoad, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            if (readsColor) {
                rt.renderPassLoad = CreateColorDepthRenderPass(
                    colorFormat, VK_ATTACHMENT_LOAD_OP_LOAD, depthLoad,
                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
            }
        }

        // Build framebuffer for the first color + optional depth attachment pair.
        if (!rt.colorAttachments.empty() && rt.colorAttachments[0].IsValid()) {
            TextureResource& colorTex = m_Textures[rt.colorAttachments[0].m_Index];
            if (colorTex.framebuffer == VK_NULL_HANDLE) {
                std::vector<VkImageView> views;
                views.push_back(colorTex.view);
                if (rt.depthAttachment.IsValid())
                    views.push_back(m_Textures[rt.depthAttachment.m_Index].view);

                VkFramebufferCreateInfo fbInfo{};
                fbInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
                fbInfo.renderPass = rt.renderPassClear;
                fbInfo.attachmentCount = static_cast<uint32_t>(views.size());
                fbInfo.pAttachments = views.data();
                fbInfo.width = colorTex.desc.m_Desc.m_Width;
                fbInfo.height = colorTex.desc.m_Desc.m_Height;
                fbInfo.layers = 1;

                vkCreateFramebuffer(m_Renderer->GetDevice(), &fbInfo, nullptr, &colorTex.framebuffer);
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

        uint32_t width = m_SceneWidth;
        uint32_t height = m_SceneHeight;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkRenderPass renderPass = VK_NULL_HANDLE;

        if (writesSwapchain) {
            width = m_Renderer->GetSwapchainWidth();
            height = m_Renderer->GetSwapchainHeight();
            framebuffer = m_Renderer->GetSwapchainFramebuffer(m_Renderer->GetAcquiredImageIndex());

            if (presentPhase) {
                renderPass = (m_SwapchainColorWritten && m_BackBufferLoadRenderPass != VK_NULL_HANDLE)
                    ? m_BackBufferLoadRenderPass
                    : m_BackBufferRenderPass;
            } else {
                renderPass = (m_SwapchainColorWritten && m_SwapchainSceneLoadPass != VK_NULL_HANDLE)
                    ? m_SwapchainSceneLoadPass
                    : m_SwapchainSceneClearPass;
            }
        } else if (!rt.colorAttachments.empty()) {
            const RHI::RHI_TextureHandle colorHandle = rt.colorAttachments[0];
            TextureResource& colorTex = m_Textures[colorHandle.m_Index];
            width = colorTex.desc.m_Desc.m_Width;
            height = colorTex.desc.m_Desc.m_Height;
            framebuffer = colorTex.framebuffer;

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
            PassContext ctx(*this, width, height);
            pass.m_Execute(ctx);
        }

        if (!leaveRenderPassOpen) {
            vkCmdEndRenderPass(cmd);
            m_InsideRenderPass = false;
        }

        // Transition sampled textures only after their last scene-pass write, so later
        // passes can still use them as attachments (e.g. Grid then Scene).
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
                TextureResource& tex = m_Textures[handle.m_Index];
                if (tex.desc.m_IsSwapchain) continue;
                if (!HasTextureUsage(tex.desc.m_Desc.m_Usage, RHI::RHI_TextureUsage::Sampled))
                    continue;
                if (!isLastWriter(handle))
                    continue;
                TransitionTextureForSampling(cmd, tex);
            }
        }

        return true;
    }

    void VK_RenderGraph::TransitionTextureForSampling(VkCommandBuffer cmd, TextureResource& texture) {
        if (texture.image.image == VK_NULL_HANDLE) return;

        const bool isDepth = IsDepthFormat(texture.desc.m_Desc.m_Format);

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
        barrier.image = texture.image.image;
        barrier.subresourceRange.aspectMask = isDepth
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;

        const VkPipelineStageFlags srcStage = isDepth
            ? (VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT)
            : VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

        vkCmdPipelineBarrier(cmd,
            srcStage,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &barrier);

        texture.state = RHI::RHI_ResourceState::ShaderRead;
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
        RHI::RHI_Shaders* shader = m_Graph.m_PipelineCache.Get(handle);
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
        RHI::RHI_Shaders* shader = m_Graph.m_PipelineCache.Get(handle);
        VkCommandBuffer cmd = m_Graph.GetCurrentCommandBuffer();
        if (!shader || cmd == VK_NULL_HANDLE) return;
        shader->Bind(cmd);
        shader->ApplyParameters(cmd);
    }

    VkRenderPass VK_RenderGraph::CreateColorDepthRenderPass(
        VkFormat colorFormat,
        VkAttachmentLoadOp colorLoad,
        VkAttachmentLoadOp depthLoad,
        VkImageLayout finalColorLayout,
        VkImageLayout colorInitialLayout) const
    {
        if (!m_Renderer) return VK_NULL_HANDLE;

        VkAttachmentDescription colorAttachment{};
        colorAttachment.format = colorFormat;
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
        // Preserve depth so it can be sampled (e.g. ImGui depth debug view).
        depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
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
        vkCreateRenderPass(m_Renderer->GetDevice(), &renderPassInfo, nullptr, &renderPass);
        return renderPass;
    }

    bool VK_RenderGraph::InitSwapchainResources() {
        if (!m_Renderer) return false;
        if (m_ResourcesInitialized) return true;

        if (!CreateDepthResources()) return false;
        if (!CreateBackBufferRenderPass()) return false;
        if (!CreateSwapchainFramebuffers()) return false;

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
        initInfo.PipelineInfoMain.RenderPass = m_BackBufferRenderPass;
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

    void VK_RenderGraph::DestroySwapchainResources() {
        DestroyFullscreenQuadBuffer();
        DestroySwapchainFramebuffers();
        DestroyBackBufferRenderPass();
        DestroyDepthResources();
        m_ResourcesInitialized = false;
    }

    bool VK_RenderGraph::RecreateSwapchainRenderTargets() {
        DestroySwapchainFramebuffers();
        DestroyDepthResources();
        if (!CreateDepthResources()) return false;
        return CreateSwapchainFramebuffers();
    }

    bool VK_RenderGraph::CreateBackBufferRenderPass() {
        if (m_BackBufferRenderPass != VK_NULL_HANDLE) return true;

        const VkFormat swapFormat = m_Renderer->GetSwapchainImageFormat();

        // Scene passes writing directly to the swapchain keep the image in COLOR_ATTACHMENT layout.
        m_SwapchainSceneClearPass = CreateColorDepthRenderPass(
            swapFormat,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        m_SwapchainSceneLoadPass = CreateColorDepthRenderPass(
            swapFormat,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        // Present pass transitions the swapchain image to PRESENT layout for ImGui/submit.
        m_BackBufferRenderPass = CreateColorDepthRenderPass(
            swapFormat,
            VK_ATTACHMENT_LOAD_OP_CLEAR, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
        m_BackBufferLoadRenderPass = CreateColorDepthRenderPass(
            swapFormat,
            VK_ATTACHMENT_LOAD_OP_LOAD, VK_ATTACHMENT_LOAD_OP_CLEAR,
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);

        return m_SwapchainSceneClearPass != VK_NULL_HANDLE
            && m_SwapchainSceneLoadPass != VK_NULL_HANDLE
            && m_BackBufferRenderPass != VK_NULL_HANDLE
            && m_BackBufferLoadRenderPass != VK_NULL_HANDLE;
    }

    void VK_RenderGraph::DestroyBackBufferRenderPass() {
        if (!m_Renderer) return;
        VkDevice device = m_Renderer->GetDevice();
        if (m_SwapchainSceneLoadPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_SwapchainSceneLoadPass, nullptr);
            m_SwapchainSceneLoadPass = VK_NULL_HANDLE;
        }
        if (m_SwapchainSceneClearPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_SwapchainSceneClearPass, nullptr);
            m_SwapchainSceneClearPass = VK_NULL_HANDLE;
        }
        if (m_BackBufferLoadRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_BackBufferLoadRenderPass, nullptr);
            m_BackBufferLoadRenderPass = VK_NULL_HANDLE;
        }
        if (m_BackBufferRenderPass != VK_NULL_HANDLE) {
            vkDestroyRenderPass(device, m_BackBufferRenderPass, nullptr);
            m_BackBufferRenderPass = VK_NULL_HANDLE;
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
        VK_MemoryAllocator& allocator = m_Renderer->GetMemoryAllocator();

        for (uint32_t i = 0; i < imageCount; ++i) {
            auto& depth = m_SwapchainDepthImages[i];
            if (!allocator.CreateImage(imageInfo, VK_MemoryLocation::GpuOnly, depth.m_Image))
                return false;

            VkImageViewCreateInfo viewInfo{};
            viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            viewInfo.image = depth.m_Image.image;
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
        VK_MemoryAllocator& allocator = m_Renderer->GetMemoryAllocator();

        for (auto& depth : m_SwapchainDepthImages) {
            if (depth.m_View != VK_NULL_HANDLE) {
                vkDestroyImageView(device, depth.m_View, nullptr);
                depth.m_View = VK_NULL_HANDLE;
            }
            allocator.DestroyImage(depth.m_Image);
        }
        m_SwapchainDepthImages.clear();
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