#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_Renderer.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Core/Log.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>
#include <algorithm>
#include <utility>

namespace Nova::Core::Renderer::Backends::Vulkan {

    // --- VK_ShaderModule ---
    bool VK_ShaderModule::Create(VkDevice device, const std::vector<uint8_t>& spirvBytes) {
        Destroy();

        if (device == VK_NULL_HANDLE || spirvBytes.empty() || (spirvBytes.size() % 4) != 0)
            return false;

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirvBytes.size();
        createInfo.pCode = reinterpret_cast<const uint32_t*>(spirvBytes.data());

        VkResult res = vkCreateShaderModule(device, &createInfo, nullptr, &m_Module);
        if (res != VK_SUCCESS) {
            m_Module = VK_NULL_HANDLE;
            m_Device = VK_NULL_HANDLE;
            return false;
        }

        m_Device = device;
        return true;
    }

    void VK_ShaderModule::Destroy() {
        if (m_Module != VK_NULL_HANDLE) {
            vkDestroyShaderModule(m_Device, m_Module, nullptr);
            m_Module = VK_NULL_HANDLE;
            m_Device = VK_NULL_HANDLE;
        }
    }

    // --- VK_Shaders ---
    void VK_Shaders::SetPipeline(VkPipeline pipeline, VkPipelineLayout layout) {
        m_Pipeline = pipeline;
        m_PipelineLayout = layout;
    }

    void VK_Shaders::SetEngineBuffers(VK_Renderer* renderer, const RHI::RHI_EngineParameterBlock& engine,
        const std::vector<std::pair<uint32_t, VkDescriptorSet>>& descriptorSets)
    {
        m_Renderer = renderer;
        m_Engine = engine;
        m_DescriptorSets = descriptorSets;
        std::sort(m_DescriptorSets.begin(), m_DescriptorSets.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    VkDescriptorSet VK_Shaders::FindDescriptorSet(uint32_t set) const {
        for (const auto& [idx, ds] : m_DescriptorSets) {
            if (idx == set) return ds;
        }
        return VK_NULL_HANDLE;
    }

    VkDevice VK_Shaders::GetDevice() const {
        return m_Renderer ? m_Renderer->GetDevice() : VK_NULL_HANDLE;
    }

    void VK_Shaders::WriteDescriptor(uint32_t set, uint32_t binding, VkDescriptorType type,
        const VkDescriptorBufferInfo* bufferInfo,
        const VkDescriptorImageInfo* imageInfo)
    {
        VkDescriptorSet dstSet = FindDescriptorSet(set);
        VkDevice device = GetDevice();
        if (device == VK_NULL_HANDLE || dstSet == VK_NULL_HANDLE) return;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = dstSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = bufferInfo;
        write.pImageInfo = imageInfo;

        vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    }

    static VkDescriptorType ToVkDescriptorType(RHI::RHI_ResourceKind kind) {
        using RK = RHI::RHI_ResourceKind;
        switch (kind) {
            case RK::ConstantBuffer:     return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case RK::StructuredBuffer:   return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case RK::RWStructuredBuffer: return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case RK::Texture:        return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
            case RK::Sampler:        return VK_DESCRIPTOR_TYPE_SAMPLER;
            case RK::CombinedTextureSampler: return VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
            case RK::RWTexture:      return VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            default:                 return VK_DESCRIPTOR_TYPE_MAX_ENUM;
        }
    }

    bool VK_Shaders::ApplyResourceBinding(const RHI::RHI_BindingInfo& info, const RHI::RHI_ResourceBinding& value) {
        // Write to whatever (set, binding) Slang reflection assigned to this resource.
        const uint32_t set = info.m_Key.m_Set;
        const uint32_t binding = info.m_Key.m_Binding;
        if (m_Renderer == nullptr || FindDescriptorSet(set) == VK_NULL_HANDLE) return false;

        const VkDescriptorType type = ToVkDescriptorType(info.m_Kind);
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) return false;

        if (std::holds_alternative<RHI::RHI_BufferBinding>(value)) {
            const auto b = std::get<RHI::RHI_BufferBinding>(value);
            VkDescriptorBufferInfo bi{};
            bi.buffer = reinterpret_cast<VkBuffer>(b.m_Handle);
            bi.offset = static_cast<VkDeviceSize>(b.m_Offset);
            bi.range = (b.m_Range == 0) ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(b.m_Range);
            WriteDescriptor(set, binding, type, &bi, nullptr);
            return true;
        }

        if (std::holds_alternative<RHI::RHI_TextureBinding>(value)) {
            const auto t = std::get<RHI::RHI_TextureBinding>(value);
            VkDescriptorImageInfo ii{};
            ii.imageView = reinterpret_cast<VkImageView>(t.m_TextureHandle);
            ii.imageLayout = (t.m_ImageLayout == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : static_cast<VkImageLayout>(t.m_ImageLayout);
            WriteDescriptor(set, binding, type, nullptr, &ii);
            return true;
        }

        if (std::holds_alternative<RHI::RHI_SamplerBinding>(value)) {
            const auto s = std::get<RHI::RHI_SamplerBinding>(value);
            VkDescriptorImageInfo ii{};
            ii.sampler = reinterpret_cast<VkSampler>(s.m_SamplerHandle);
            WriteDescriptor(set, binding, type, nullptr, &ii);
            return true;
        }

        return false;
    }

    void VK_Shaders::Bind(void* apiContext) {
        if (!apiContext || m_Pipeline == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    }

    // Pack the SetParameter() values listed in `layout` (name -> byte offset) into `dstBase`,
    // which already holds the struct's defaults. Used identically for every engine uniform block.
    static void CopyParametersIntoStruct(const std::unordered_map<std::string, RHI::UniformValue>& params, const std::unordered_map<std::string, size_t>& layout, void* dstBase) {
        for (const auto& [name, offset] : layout) {
            auto it = params.find(name);
            if (it == params.end()) continue;
            char* dst = static_cast<char*>(dstBase) + offset;
            std::visit([dst](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int>)        *reinterpret_cast<int*>(dst) = v;
                else if constexpr (std::is_same_v<T, float>) *reinterpret_cast<float*>(dst) = v;
                else if constexpr (std::is_same_v<T, glm::vec2>) std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 2);
                else if constexpr (std::is_same_v<T, glm::vec3>) std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 3);
                else if constexpr (std::is_same_v<T, glm::vec4>) std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 4);
                else if constexpr (std::is_same_v<T, glm::mat2>) std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 4);
                else if constexpr (std::is_same_v<T, glm::mat3>) std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 9);
                else if constexpr (std::is_same_v<T, glm::mat4>) std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 16);
            }, it->second);
        }
    }

    void VK_Shaders::BindDescriptorSets(VkCommandBuffer cmd,
        VkDeviceSize frameDynamicOffset, VkDeviceSize mvpDynamicOffset,
        VkDeviceSize materialDynamicOffset)
    {
        if (m_DescriptorSets.empty()) return;

        // Every engine buffer (frame, MVP, material) is a dynamic descriptor: each
        // frame-in-flight owns a distinct region and the dynamic offset selects it. Vulkan requires
        // one dynamic offset per dynamic descriptor in the set, in ascending binding order.
        struct EngineDynamic { const char* name; VkDeviceSize offset; };
        const EngineDynamic engineDynamics[] = {
            { RHI::EngineResourceName::Frame,    frameDynamicOffset },
            { RHI::EngineResourceName::Mvp,      mvpDynamicOffset },
            { RHI::EngineResourceName::Material, materialDynamicOffset },
        };

        // m_DescriptorSets is sorted by set index in SetEngineBuffers.
        for (const auto& [setIndex, ds] : m_DescriptorSets) {
            if (ds == VK_NULL_HANDLE) continue;

            std::vector<std::pair<uint32_t, uint32_t>> dyn; // (binding, offset)
            for (const auto& ed : engineDynamics) {
                const RHI::RHI_BindingInfo* info = m_Reflection.FindBindingByName(ed.name);
                if (info && info->m_IsDynamicUniformBuffer && info->m_Key.m_Set == setIndex)
                    dyn.emplace_back(info->m_Key.m_Binding, static_cast<uint32_t>(ed.offset));
            }
            std::sort(dyn.begin(), dyn.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

            std::vector<uint32_t> offsets;
            offsets.reserve(dyn.size());
            for (const auto& [binding, offset] : dyn) offsets.push_back(offset);

            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                setIndex, 1, &ds,
                static_cast<uint32_t>(offsets.size()), offsets.empty() ? nullptr : offsets.data());
        }
    }

    void VK_Shaders::ApplyParameters(void* apiContext) {
        if (!apiContext || m_PipelineLayout == VK_NULL_HANDLE || !m_Renderer) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);

        // Every engine buffer is dynamic: it is written into this frame-in-flight's private region
        // so concurrent in-flight frames never clobber each other's uniforms — the root cause of
        // per-object flickering with multiple frames in flight.
        const uint32_t frameIdx = m_Renderer->GetCurrentFrameInFlight();
        VK_GpuBufferPool& pool = m_Renderer->GetGpuBufferPool();

        // FrameUniforms: one region per frame, addressed by its dynamic offset.
        RHI::FrameUniforms frame{};
        CopyParametersIntoStruct(m_Parameters, RHI::GetFrameLayout(), &frame);
        pool.Update(m_Engine.m_Frame, &frame, sizeof frame, /*elementIndex*/ 0, frameIdx);
        const VkDeviceSize frameOffsetThisFrame = static_cast<VkDeviceSize>(
            pool.ResolveBinding(m_Engine.m_Frame, 0, frameIdx).m_Offset);

        // MVP / Material: one ring element per draw call this frame (auto-incrementing cursor).
        RHI::MVP mvp{};
        CopyParametersIntoStruct(m_Parameters, RHI::GetMvpLayout(), &mvp);
        const VkDeviceSize mvpOffsetThisDraw = pool.WriteNextDynamicElement(m_Engine.m_Mvp, &mvp, sizeof mvp, frameIdx);

        RHI::Material material{};
        CopyParametersIntoStruct(m_Parameters, RHI::GetMaterialLayout(), &material);
        const VkDeviceSize materialOffsetThisDraw = pool.WriteNextDynamicElement(m_Engine.m_Material, &material, sizeof material, frameIdx);

        BindDescriptorSets(cmd, frameOffsetThisFrame, mvpOffsetThisDraw, materialOffsetThisDraw);
    }

    void* VK_Shaders::GetNativeHandle() const {
        return reinterpret_cast<void*>(m_Pipeline);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan