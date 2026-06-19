#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

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

    void VK_Shaders::SetSceneBuffers(VkDevice device,
        VkBuffer bufFrameUniforms, VkDeviceMemory bufFrameUniformsMemory,
        VkBuffer bufMvp, VkDeviceMemory bufMvpMemory, VkDeviceSize mvpDynamicStride,
        VkBuffer bufMaterials, VkDeviceMemory bufMaterialsMemory, VkDeviceSize materialDynamicStride,
        VkBuffer bufInstances, VkDeviceMemory bufInstancesMemory, VkDeviceSize bufInstancesSize,
        std::vector<std::pair<uint32_t, VkDescriptorSet>> descriptorSets)
    {
        m_Device = device;
        m_BufFrameUniforms = bufFrameUniforms;
        m_BufFrameUniformsMemory = bufFrameUniformsMemory;
        m_BufMvp = bufMvp;
        m_BufMvpMemory = bufMvpMemory;
        m_MvpDynamicStride = mvpDynamicStride;
        m_MvpDynamicOffset = 0;
        m_BufMaterials = bufMaterials;
        m_BufMaterialsMemory = bufMaterialsMemory;
        m_MaterialDynamicStride = materialDynamicStride;
        m_MaterialDynamicOffset = 0;
        m_BufInstances = bufInstances;
        m_BufInstancesMemory = bufInstancesMemory;
        m_BufInstancesSize = bufInstancesSize;
        m_DescriptorSets = std::move(descriptorSets);
        std::sort(m_DescriptorSets.begin(), m_DescriptorSets.end(),
            [](const auto& a, const auto& b) { return a.first < b.first; });
    }

    void VK_Shaders::ResetDynamicUBOs() {
        m_MvpDynamicOffset = 0;
        m_MaterialDynamicOffset = 0;
    }

    VkDescriptorSet VK_Shaders::FindDescriptorSet(uint32_t set) const {
        for (const auto& [idx, ds] : m_DescriptorSets) {
            if (idx == set) return ds;
        }
        return VK_NULL_HANDLE;
    }

    void VK_Shaders::WriteDescriptor(uint32_t set, uint32_t binding, VkDescriptorType type,
        const VkDescriptorBufferInfo* bufferInfo,
        const VkDescriptorImageInfo* imageInfo)
    {
        VkDescriptorSet dstSet = FindDescriptorSet(set);
        if (m_Device == VK_NULL_HANDLE || dstSet == VK_NULL_HANDLE) return;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = dstSet;
        write.dstBinding = binding;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = type;
        write.pBufferInfo = bufferInfo;
        write.pImageInfo = imageInfo;

        vkUpdateDescriptorSets(m_Device, 1, &write, 0, nullptr);
    }

    static VkDescriptorType ToVkDescriptorType(RHI::RHI_ResourceKind kind) {
        using RK = RHI::RHI_ResourceKind;
        switch (kind) {
            case RK::ConstantBuffer: return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            case RK::StorageBuffer:  return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            case RK::RWBuffer:       return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
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
        if (m_Device == VK_NULL_HANDLE || FindDescriptorSet(set) == VK_NULL_HANDLE) return false;

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

    void VK_Shaders::MapAndCopy(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, const void* src) {
        if (m_Device == VK_NULL_HANDLE || memory == VK_NULL_HANDLE) return;
        void* mapped = nullptr;
        if (vkMapMemory(m_Device, memory, offset, size, 0, &mapped) == VK_SUCCESS) {
            std::memcpy(mapped, src, static_cast<size_t>(size));
            vkUnmapMemory(m_Device, memory);
        }
    }

    VkDeviceSize VK_Shaders::UploadDynamic(VkDeviceMemory memory, VkDeviceSize size, const void* src, VkDeviceSize stride, VkDeviceSize& offsetCursor) {
        if (memory == VK_NULL_HANDLE || stride == 0) return 0;
        const VkDeviceSize offsetThisDraw = offsetCursor;
        MapAndCopy(memory, offsetThisDraw, size, src);
        offsetCursor += stride;
        return offsetThisDraw;
    }

    void VK_Shaders::BindDescriptorSets(VkCommandBuffer cmd, VkDeviceSize mvpDynamicOffset, VkDeviceSize materialDynamicOffset) {
        if (m_DescriptorSets.empty()) return;

        // The MVP and Material constant buffers are dynamic UBOs; resolve their (set, binding) from
        // reflection so dynamic offsets are provided in ascending binding order, per the Vulkan spec.
        const RHI::RHI_BindingKey* mvpKey = m_Reflection.FindBindingKeyByName(RHI::EngineResourceName::Mvp);
        const RHI::RHI_BindingKey* materialKey = m_Reflection.FindBindingKeyByName(RHI::EngineResourceName::Material);

        // m_DescriptorSets is sorted by set index in SetSceneBuffers.
        for (const auto& [setIndex, ds] : m_DescriptorSets) {
            if (ds == VK_NULL_HANDLE) continue;

            // Gather the dynamic offsets that belong to this set, ordered by binding.
            std::vector<std::pair<uint32_t, uint32_t>> dyn; // (binding, offset)
            if (m_MvpDynamicStride != 0 && mvpKey && mvpKey->m_Set == setIndex)
                dyn.emplace_back(mvpKey->m_Binding, static_cast<uint32_t>(mvpDynamicOffset));
            if (m_MaterialDynamicStride != 0 && materialKey && materialKey->m_Set == setIndex)
                dyn.emplace_back(materialKey->m_Binding, static_cast<uint32_t>(materialDynamicOffset));
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
        if (!apiContext || m_PipelineLayout == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);

        // Every engine uniform block is packed and uploaded the same way: start from the struct
        // defaults, overlay the SetParameter() values, then copy into its GPU buffer.
        RHI::FrameUniforms frame{};
        CopyParametersIntoStruct(m_Parameters, RHI::GetFrameLayout(), &frame);
        MapAndCopy(m_BufFrameUniformsMemory, 0, sizeof frame, &frame);

        RHI::MVP mvp{};
        CopyParametersIntoStruct(m_Parameters, RHI::GetMvpLayout(), &mvp);
        const VkDeviceSize mvpOffsetThisDraw =
            UploadDynamic(m_BufMvpMemory, sizeof mvp, &mvp, m_MvpDynamicStride, m_MvpDynamicOffset);

        RHI::Material material{};
        CopyParametersIntoStruct(m_Parameters, RHI::GetMaterialLayout(), &material);
        const VkDeviceSize materialOffsetThisDraw =
            UploadDynamic(m_BufMaterialsMemory, sizeof material, &material, m_MaterialDynamicStride, m_MaterialDynamicOffset);

        // Per-instance data for GPU instancing (read by the shader as `nova.instances[instanceID]`
        // when `u_UseInstancing != 0`). Clamp to the buffer capacity and upload at offset 0.
        if (!m_Instances.empty() && m_BufInstancesSize >= sizeof(RHI::Instance)) {
            const VkDeviceSize capacity = m_BufInstancesSize / sizeof(RHI::Instance);
            const VkDeviceSize count = std::min<VkDeviceSize>(m_Instances.size(), capacity);
            MapAndCopy(m_BufInstancesMemory, 0, count * sizeof(RHI::Instance), m_Instances.data());
        }

        BindDescriptorSets(cmd, mvpOffsetThisDraw, materialOffsetThisDraw);
    }

    void* VK_Shaders::GetNativeHandle() const {
        return reinterpret_cast<void*>(m_Pipeline);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan