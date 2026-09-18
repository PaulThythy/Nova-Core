#include "Renderer/Backends/Vulkan/VK_PipelineCache.h"

#include "Renderer/Backends/Vulkan/VK_Renderer.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Math/Vertex.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Core/Log.h"

#include <algorithm>
#include <array>
#include <filesystem>

namespace Nova::Core::Renderer::Backends::Vulkan {

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
            RHI::EngineResourceName::SceneUniforms,
            RHI::EngineResourceName::Model,
            RHI::EngineResourceName::Material,
            RHI::EngineResourceName::Lights,
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
        if (!desc.m_Vertex) {
            NV_LOG_WARN("VK_PipelineCache: RegisterShader requires a vertex ShaderAsset.");
            return false;
        }
        if (!desc.m_DepthOnly && !desc.m_Fragment) {
            NV_LOG_WARN("VK_PipelineCache: RegisterShader requires vertex and fragment ShaderAssets.");
            return false;
        }

        if (!desc.m_Vertex->Compile()) {
            NV_LOG_WARN(("VK_PipelineCache: vertex compile failed:\n" + desc.m_Vertex->GetLastLog()).c_str());
            return false;
        }

        vertOut.m_Success = true;
        vertOut.m_Binary = desc.m_Vertex->GetBinary();
        vertOut.m_Format = desc.m_Vertex->GetBinaryFormat();
        vertOut.m_Source = desc.m_Vertex->GetSource();
        vertOut.m_Reflection = desc.m_Vertex->GetReflection();
        vertOut.m_Stage = desc.m_Vertex->GetStage();

        if (desc.m_Fragment) {
            if (!desc.m_Fragment->Compile()) {
                NV_LOG_WARN(("VK_PipelineCache: fragment compile failed:\n" + desc.m_Fragment->GetLastLog()).c_str());
                return false;
            }
            fragOut.m_Success = true;
            fragOut.m_Binary = desc.m_Fragment->GetBinary();
            fragOut.m_Format = desc.m_Fragment->GetBinaryFormat();
            fragOut.m_Source = desc.m_Fragment->GetSource();
            fragOut.m_Reflection = desc.m_Fragment->GetReflection();
            fragOut.m_Stage = desc.m_Fragment->GetStage();
        }
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

        if (!CreateCompatibleRenderPasses(m_ColorFormat, m_DepthFormat))
            return false;

        // One entry per declared shader; pipelines are built lazily on first use.
        m_Entries.resize(shaders.size());
        for (size_t i = 0; i < shaders.size(); ++i)
            m_Entries[i].desc = shaders[i];

        return true;
    }

    bool VK_PipelineCache::CreateCompatibleRenderPasses(VkFormat colorFormat, VkFormat depthFormat) {
        if (!m_Renderer || colorFormat == VK_FORMAT_UNDEFINED || depthFormat == VK_FORMAT_UNDEFINED)
            return false;

        VkDevice device = m_Renderer->GetDevice();

        // Color + depth compatible RP (must match VK_RenderGraph::CreateColorDepthRenderPass).
        {
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
            if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_RenderPass) != VK_SUCCESS)
                return false;
        }

        // Depth-only compatible RP for shadow pipelines.
        {
            VkAttachmentDescription depthAttachment{};
            depthAttachment.format = depthFormat;
            depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
            depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
            depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
            depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
            depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
            depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            depthAttachment.finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

            VkAttachmentReference depthRef{ 0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL };
            VkSubpassDescription subpass{};
            subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
            subpass.colorAttachmentCount = 0;
            subpass.pDepthStencilAttachment = &depthRef;

            VkSubpassDependency dependency{};
            dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
            dependency.dstSubpass = 0;
            dependency.srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
            dependency.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

            VkRenderPassCreateInfo rpInfo{};
            rpInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
            rpInfo.attachmentCount = 1;
            rpInfo.pAttachments = &depthAttachment;
            rpInfo.subpassCount = 1;
            rpInfo.pSubpasses = &subpass;
            rpInfo.dependencyCount = 1;
            rpInfo.pDependencies = &dependency;
            if (vkCreateRenderPass(device, &rpInfo, nullptr, &m_DepthOnlyRenderPass) != VK_SUCCESS)
                return false;
        }

        return true;
    }

    void VK_PipelineCache::Destroy() {
        for (auto& entry : m_Entries)
            DestroyEntry(entry);
        m_Entries.clear();

        DestroyEngineBuffers();

        m_ShadowMapsView = VK_NULL_HANDLE;
        m_ShadowSampler = VK_NULL_HANDLE;

        if (m_Renderer) {
            VkDevice device = m_Renderer->GetDevice();
            if (m_DepthOnlyRenderPass != VK_NULL_HANDLE) {
                vkDestroyRenderPass(device, m_DepthOnlyRenderPass, nullptr);
                m_DepthOnlyRenderPass = VK_NULL_HANDLE;
            }
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

    // Creates the buffer fields of `ParameterBlock<NovaEngine> nova;` through the RHI buffer API.
    bool VK_PipelineCache::CreateEngineBuffers() {
        if (m_Engine.IsValid())
            return true;

        m_Engine.m_Scene.m_Uniforms = RHI::CreateConstantBuffer<RHI::SceneUniforms>(*m_Renderer, 1, RHI::EngineResourceName::SceneUniforms);
        m_Engine.m_Scene.m_Lights = RHI::CreateStructuredBuffer<RHI::LightGPU>(*m_Renderer, RHI::MAX_LIGHTS, RHI::EngineResourceName::Lights);
        m_Engine.m_Model = RHI::CreateConstantBuffer<RHI::ModelUniforms>(*m_Renderer, MAX_MODEL_DRAWS, RHI::EngineResourceName::Model);
        m_Engine.m_Material = RHI::CreateConstantBuffer<RHI::Material>(*m_Renderer, MAX_MODEL_DRAWS, RHI::EngineResourceName::Material);

        return m_Engine.IsValid();
    }

    void VK_PipelineCache::DestroyEngineBuffers() {
        if (!m_Renderer) return;
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Material);
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Model);
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Scene.m_Lights);
        m_Renderer->DestroyGpuBuffer(m_Engine.m_Scene.m_Uniforms);
        m_Engine = RHI::RHI_EngineParameterBlock{};
    }

    // Resets this frame-in-flight's per-draw ring cursor for `nova.model` / `nova.material`, so the
    // first draw of the frame writes at the start of its region instead of continuing from
    // wherever the previous frame using this slot left off.
    void VK_PipelineCache::ResetFrameDynamicUBOs() {
        if (!m_Renderer) return;
        m_Renderer->GetGpuBufferPool().ResetDynamicCursors(m_Renderer->GetCurrentFrameInFlight());
    }

    RHI::IShaders* VK_PipelineCache::Get(RHI::RHI_ShaderHandle handle) {
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
            if (!entry.shader || !entry.desc.m_Vertex)
                continue;
            if (!entry.desc.m_DepthOnly && !entry.desc.m_Fragment)
                continue;

            const auto vertTime = GetFileWriteTime(entry.desc.m_Vertex->GetPath());
            const auto fragTime = entry.desc.m_Fragment
                ? GetFileWriteTime(entry.desc.m_Fragment->GetPath())
                : entry.fragWriteTime;
            if (vertTime == entry.vertWriteTime && fragTime == entry.fragWriteTime)
                continue;

            entry.desc.m_Vertex->Recompile();
            if (entry.desc.m_Fragment)
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

    bool VK_PipelineCache::BindEngineShadowMaps(VkImageView arrayView, VkSampler comparisonSampler) {
        m_ShadowMapsView = arrayView;
        m_ShadowSampler = comparisonSampler;
        if (m_ShadowMapsView == VK_NULL_HANDLE || m_ShadowSampler == VK_NULL_HANDLE)
            return false;
        for (auto& entry : m_Entries) {
            if (entry.shader)
                WriteShadowMapsToEntry(entry);
        }
        return true;
    }

    void VK_PipelineCache::WriteEngineBuffersToEntry(PipelineEntry& entry) {
        if (!m_Renderer || !entry.shader) return;
        const RHI::RHI_ProgramReflection& reflForVk = entry.shader->GetReflection();
        auto findDescriptorSet = [&](uint32_t set) -> VkDescriptorSet {
            for (const auto& [idx, ds] : entry.descriptorSets) if (idx == set) return ds;
            return VK_NULL_HANDLE;
        };
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
            vkUpdateDescriptorSets(m_Renderer->GetDevice(), 1, &write, 0, nullptr);
        };
        writeEngineBuffer(RHI::EngineResourceName::SceneUniforms, m_Engine.m_Scene.m_Uniforms);
        writeEngineBuffer(RHI::EngineResourceName::Lights, m_Engine.m_Scene.m_Lights);
        writeEngineBuffer(RHI::EngineResourceName::Model, m_Engine.m_Model);
        writeEngineBuffer(RHI::EngineResourceName::Material, m_Engine.m_Material);
    }

    void VK_PipelineCache::WriteShadowMapsToEntry(PipelineEntry& entry) {
        if (!m_Renderer || !entry.shader || m_ShadowMapsView == VK_NULL_HANDLE || m_ShadowSampler == VK_NULL_HANDLE)
            return;
        const RHI::RHI_ProgramReflection& refl = entry.shader->GetReflection();
        auto findDescriptorSet = [&](uint32_t set) -> VkDescriptorSet {
            for (const auto& [idx, ds] : entry.descriptorSets) if (idx == set) return ds;
            return VK_NULL_HANDLE;
        };

        if (const RHI::RHI_BindingInfo* info = refl.FindBindingByName(RHI::EngineResourceName::ShadowMaps)) {
            VkDescriptorSet ds = findDescriptorSet(info->m_Key.m_Set);
            if (ds != VK_NULL_HANDLE) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.imageView = m_ShadowMapsView;
                imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = ds;
                write.dstBinding = info->m_Key.m_Binding;
                write.descriptorCount = 1;
                write.descriptorType = ToVkDescriptorType(*info);
                write.pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(m_Renderer->GetDevice(), 1, &write, 0, nullptr);
            }
        }
        if (const RHI::RHI_BindingInfo* info = refl.FindBindingByName(RHI::EngineResourceName::ShadowSampler)) {
            VkDescriptorSet ds = findDescriptorSet(info->m_Key.m_Set);
            if (ds != VK_NULL_HANDLE) {
                VkDescriptorImageInfo imageInfo{};
                imageInfo.sampler = m_ShadowSampler;
                VkWriteDescriptorSet write{};
                write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                write.dstSet = ds;
                write.dstBinding = info->m_Key.m_Binding;
                write.descriptorCount = 1;
                write.descriptorType = ToVkDescriptorType(*info);
                write.pImageInfo = &imageInfo;
                vkUpdateDescriptorSets(m_Renderer->GetDevice(), 1, &write, 0, nullptr);
            }
        }
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

        const bool depthOnly = entry.desc.m_DepthOnly;

        RHI::RHI_ShaderCompileResult vertOut{}, fragOut{};
        if (!CompileGraphicsShaders(entry.desc, vertOut, fragOut))
            return false;

        if (!CreateEngineBuffers())
            return false;

        VkDevice device = m_Renderer->GetDevice();

        VK_ShaderModule vertModule, fragModule;
        if (!vertModule.Create(device, vertOut.m_Binary))
            return false;
        if (!depthOnly) {
            if (!fragModule.Create(device, fragOut.m_Binary))
                return false;
        }

        VkPipelineShaderStageCreateInfo stages[2]{};
        uint32_t stageCount = 1;
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertModule.GetModule();
        stages[0].pName = "main";
        if (!depthOnly) {
            stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fragModule.GetModule();
            stages[1].pName = "main";
            stageCount = 2;
        }

        VkPipelineVertexInputStateCreateInfo vertexInput{};
        VkPipelineInputAssemblyStateCreateInfo inputAsm{};
        inputAsm.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        switch (entry.desc.m_PrimitiveTopology) {
        case RHI::RHI_PrimitiveTopology::Lines:
            inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
            break;
        case RHI::RHI_PrimitiveTopology::Points:
            inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
            break;
        case RHI::RHI_PrimitiveTopology::Triangles:
        default:
            inputAsm.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            break;
        }

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
            vertexBinding.stride = sizeof(Math::Vertex);
            vertexBinding.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
            vertexAttrs[0] = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Math::Vertex, m_Position) };
            vertexAttrs[1] = { 1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Math::Vertex, m_Normal) };
            vertexAttrs[2] = { 2, 0, VK_FORMAT_R32G32_SFLOAT,    offsetof(Math::Vertex, m_TexCoord) };
            vertexAttrs[3] = { 3, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Math::Vertex, m_Color) };
            vertexAttrs[4] = { 4, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Math::Vertex, m_Tangent) };
            vertexAttrs[5] = { 5, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Math::Vertex, m_Bitangent) };
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
        switch (entry.desc.m_PolygonMode) {
            case RHI::RHI_PolygonMode::Line:  raster.polygonMode = VK_POLYGON_MODE_LINE; break;
            case RHI::RHI_PolygonMode::Fill:
            default:                            raster.polygonMode = VK_POLYGON_MODE_FILL; break;
        }
        switch (entry.desc.m_CullMode) {
            case RHI::RHI_CullMode::None:  raster.cullMode = VK_CULL_MODE_NONE; break;
            case RHI::RHI_CullMode::Front: raster.cullMode = VK_CULL_MODE_FRONT_BIT; break;
            case RHI::RHI_CullMode::Back:
            default:                       raster.cullMode = isFullscreen ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT; break;
        }
        raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
        raster.lineWidth = 1.0f;
        const bool useDepthBias = depthOnly
            || entry.desc.m_DepthBiasConstant != 0.0f
            || entry.desc.m_DepthBiasSlope != 0.0f;
        if (useDepthBias) {
            raster.depthBiasEnable = VK_TRUE;
            raster.depthBiasConstantFactor = entry.desc.m_DepthBiasConstant;
            raster.depthBiasSlopeFactor = entry.desc.m_DepthBiasSlope;
        }

        VkPipelineMultisampleStateCreateInfo msaa{};
        msaa.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        msaa.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo depthStencil{};
        depthStencil.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depthStencil.depthTestEnable = entry.desc.m_DepthTest ? VK_TRUE : VK_FALSE;
        depthStencil.depthWriteEnable = entry.desc.m_DepthWrite ? VK_TRUE : VK_FALSE;
        if (isFullscreen) {
            depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
        } else {
            switch (entry.desc.m_DepthCompare) {
                case RHI::RHI_DepthCompare::LessOrEqual:    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL; break;
                case RHI::RHI_DepthCompare::Greater:         depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER; break;
                case RHI::RHI_DepthCompare::GreaterOrEqual:  depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL; break;
                case RHI::RHI_DepthCompare::Always:          depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS; break;
                case RHI::RHI_DepthCompare::Never:           depthStencil.depthCompareOp = VK_COMPARE_OP_NEVER; break;
                case RHI::RHI_DepthCompare::Equal:           depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL; break;
                case RHI::RHI_DepthCompare::Less:
                default:                                    depthStencil.depthCompareOp = VK_COMPARE_OP_LESS; break;
            }
        }

        VkPipelineColorBlendAttachmentState blendAttachment{};
        blendAttachment.colorWriteMask =
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        if (entry.desc.m_AlphaBlend) {
            blendAttachment.blendEnable = VK_TRUE;
            if (entry.desc.m_AdditiveBlend) {
                blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            } else {
                blendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
                blendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
                blendAttachment.colorBlendOp = VK_BLEND_OP_ADD;
                blendAttachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
                blendAttachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
                blendAttachment.alphaBlendOp = VK_BLEND_OP_ADD;
            }
        }

        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = depthOnly ? 0u : 1u;
        blend.pAttachments = depthOnly ? nullptr : &blendAttachment;

        std::vector<VkDynamicState> dynamicStates = {
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        };
        if (useDepthBias)
            dynamicStates.push_back(VK_DYNAMIC_STATE_DEPTH_BIAS);
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();

        std::vector<RHI::RHI_ProgramReflection> reflParts = { vertOut.m_Reflection };
        if (!depthOnly)
            reflParts.push_back(fragOut.m_Reflection);
        RHI::RHI_ProgramReflection reflForVk = RHI::MergeProgramReflections(reflParts);
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
        writeEngineBuffer(RHI::EngineResourceName::SceneUniforms, m_Engine.m_Scene.m_Uniforms);
        writeEngineBuffer(RHI::EngineResourceName::Lights, m_Engine.m_Scene.m_Lights);
        writeEngineBuffer(RHI::EngineResourceName::Model, m_Engine.m_Model);
        writeEngineBuffer(RHI::EngineResourceName::Material, m_Engine.m_Material);

        std::vector<VkDescriptorSetLayout> setLayouts;
        setLayouts.reserve(entry.setLayouts.size());
        for (const auto& [setIndex, layout] : entry.setLayouts) setLayouts.push_back(layout);

        VkPushConstantRange pushRange{};
        const bool hasPushConstants = reflForVk.m_PushConstants.has_value()
            && reflForVk.m_PushConstants->m_SizeBytes > 0;
        if (hasPushConstants) {
            pushRange.stageFlags = ToVkStageFlags(reflForVk.m_PushConstants->m_Stages);
            if (pushRange.stageFlags == 0)
                pushRange.stageFlags = VK_SHADER_STAGE_ALL_GRAPHICS;
            pushRange.offset = 0;
            pushRange.size = static_cast<uint32_t>(reflForVk.m_PushConstants->m_SizeBytes);
        }

        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
        layoutInfo.pSetLayouts = setLayouts.data();
        if (hasPushConstants) {
            layoutInfo.pushConstantRangeCount = 1;
            layoutInfo.pPushConstantRanges = &pushRange;
        }
        if (vkCreatePipelineLayout(device, &layoutInfo, nullptr, &entry.pipelineLayout) != VK_SUCCESS) {
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        VkRenderPass compatiblePass = depthOnly ? m_DepthOnlyRenderPass : m_RenderPass;
        if (compatiblePass == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_PipelineCache::BuildPipeline - compatible render pass is missing");
            vertModule.Destroy();
            fragModule.Destroy();
            return false;
        }

        VkGraphicsPipelineCreateInfo pipe{};
        pipe.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipe.stageCount = stageCount;
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
        pipe.renderPass = compatiblePass;
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
        WriteShadowMapsToEntry(entry);

        entry.vertWriteTime = GetFileWriteTime(entry.desc.m_Vertex->GetPath());
        entry.fragWriteTime = entry.desc.m_Fragment
            ? GetFileWriteTime(entry.desc.m_Fragment->GetPath())
            : entry.vertWriteTime;
        return true;
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan