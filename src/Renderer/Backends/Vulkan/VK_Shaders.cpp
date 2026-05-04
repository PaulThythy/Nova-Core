#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstring>

namespace Nova::Core::Renderer::Backends::Vulkan {

    // --- VK_ShaderModule ---
    bool VK_ShaderModule::Create(VkDevice device, const std::vector<uint32_t>& spirv) {
        Destroy();

        if (device == VK_NULL_HANDLE || spirv.empty())
            return false;

        VkShaderModuleCreateInfo createInfo{};
        createInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        createInfo.codeSize = spirv.size() * sizeof(uint32_t);
        createInfo.pCode = spirv.data();

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
        VkDescriptorSet sceneDescriptorSet,
        VkDescriptorSet userDescriptorSet)
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
        m_SceneDescriptorSet = sceneDescriptorSet;
        m_UserDescriptorSet = userDescriptorSet;
    }

    void VK_Shaders::ResetDynamicUBOs() {
        m_MvpDynamicOffset = 0;
        m_MaterialDynamicOffset = 0;
    }

    void VK_Shaders::WriteUserDescriptor(uint32_t binding, VkDescriptorType type,
        const VkDescriptorBufferInfo* bufferInfo,
        const VkDescriptorImageInfo* imageInfo)
    {
        if (m_Device == VK_NULL_HANDLE || m_UserDescriptorSet == VK_NULL_HANDLE) return;

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = m_UserDescriptorSet;
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
        // This class only supports updating the user descriptor set (set 1).
        if (info.m_Key.m_Set != RHI::kUserDescriptorSet) return false;
        if (m_Device == VK_NULL_HANDLE || m_UserDescriptorSet == VK_NULL_HANDLE) return false;

        const VkDescriptorType type = ToVkDescriptorType(info.m_Kind);
        if (type == VK_DESCRIPTOR_TYPE_MAX_ENUM) return false;

        const uint32_t binding = info.m_Key.m_Binding;

        if (std::holds_alternative<RHI::RHI_BufferBinding>(value)) {
            const auto b = std::get<RHI::RHI_BufferBinding>(value);
            VkDescriptorBufferInfo bi{};
            bi.buffer = reinterpret_cast<VkBuffer>(b.m_Handle);
            bi.offset = static_cast<VkDeviceSize>(b.m_Offset);
            bi.range = (b.m_Range == 0) ? VK_WHOLE_SIZE : static_cast<VkDeviceSize>(b.m_Range);
            WriteUserDescriptor(binding, type, &bi, nullptr);
            return true;
        }

        if (std::holds_alternative<RHI::RHI_TextureBinding>(value)) {
            const auto t = std::get<RHI::RHI_TextureBinding>(value);
            VkDescriptorImageInfo ii{};
            ii.imageView = reinterpret_cast<VkImageView>(t.m_TextureHandle);
            ii.imageLayout = (t.m_ImageLayout == 0) ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : static_cast<VkImageLayout>(t.m_ImageLayout);
            WriteUserDescriptor(binding, type, nullptr, &ii);
            return true;
        }

        if (std::holds_alternative<RHI::RHI_SamplerBinding>(value)) {
            const auto s = std::get<RHI::RHI_SamplerBinding>(value);
            VkDescriptorImageInfo ii{};
            ii.sampler = reinterpret_cast<VkSampler>(s.m_SamplerHandle);
            WriteUserDescriptor(binding, type, nullptr, &ii);
            return true;
        }

        return false;
    }

    void VK_Shaders::Bind(void* apiContext) {
        if (!apiContext || m_Pipeline == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    }

    void VK_Shaders::UploadFrameUniforms() {
        RHI::FrameUniforms frameUniforms{};
        if (m_BufFrameUniforms != VK_NULL_HANDLE) {
            const auto& frameUniformsLayout = RHI::GetFrameUniformsLayout();
            for (const auto& [name, offset] : frameUniformsLayout) {
                auto it = m_Parameters.find(name);
                if (it == m_Parameters.end()) continue;
                char* dst = reinterpret_cast<char*>(&frameUniforms) + offset;
                std::visit([dst](auto&& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, int>) *reinterpret_cast<int*>(dst) = v;
                    else if constexpr (std::is_same_v<T, float>) *reinterpret_cast<float*>(dst) = v;
                    if constexpr (std::is_same_v<T, glm::vec3>)
                        std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 3);
                    if constexpr (std::is_same_v<T, glm::vec4>)
                        std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 4);
                }, it->second);
            }
        }

        if (m_BufFrameUniformsMemory != VK_NULL_HANDLE) {
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_BufFrameUniformsMemory, 0, sizeof(RHI::FrameUniforms), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &frameUniforms, sizeof(RHI::FrameUniforms));
                vkUnmapMemory(m_Device, m_BufFrameUniformsMemory);
            }
        }
    }

    void VK_Shaders::UploadMvpUniforms(VkDeviceSize& outDynamicOffsetThisDraw) {
        outDynamicOffsetThisDraw = 0;

        RHI::MVP mvp{};
        if (m_BufMvp != VK_NULL_HANDLE) {
            auto itM = m_Parameters.find("model"), itV = m_Parameters.find("view"), itP = m_Parameters.find("proj"), itVP = m_Parameters.find("viewProj"), itInvVP = m_Parameters.find("invViewProj");
            if (itM != m_Parameters.end() && std::holds_alternative<glm::mat4>(itM->second)) mvp.model = std::get<glm::mat4>(itM->second);
            if (itV != m_Parameters.end() && std::holds_alternative<glm::mat4>(itV->second)) mvp.view = std::get<glm::mat4>(itV->second);
            if (itP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itP->second)) mvp.proj = std::get<glm::mat4>(itP->second);
            if (itVP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itVP->second)) mvp.viewProj = std::get<glm::mat4>(itVP->second);
            if (itInvVP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itInvVP->second)) mvp.invViewProj = std::get<glm::mat4>(itInvVP->second);
        }

        if (m_BufMvpMemory != VK_NULL_HANDLE && m_MvpDynamicStride != 0) {
            outDynamicOffsetThisDraw = m_MvpDynamicOffset;
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_BufMvpMemory, outDynamicOffsetThisDraw, sizeof(RHI::MVP), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &mvp, sizeof(RHI::MVP));
                vkUnmapMemory(m_Device, m_BufMvpMemory);
            }
            m_MvpDynamicOffset += m_MvpDynamicStride;
        }
    }

    void VK_Shaders::UploadMaterialUniforms(VkDeviceSize& outDynamicOffsetThisDraw) {
        outDynamicOffsetThisDraw = 0;

        RHI::Material material{};
        if (m_BufMaterials != VK_NULL_HANDLE) {
            const auto& layout = RHI::GetMaterialParameterLayout();
            for (const auto& [name, offset] : layout) {
                auto it = m_Parameters.find(name);
                if (it == m_Parameters.end()) continue;
                char* dst = reinterpret_cast<char*>(&material) + offset;
                std::visit([dst](auto&& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, glm::vec3>)
                        std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 3);
                    else if constexpr (std::is_same_v<T, glm::vec4>)
                        std::memcpy(dst, glm::value_ptr(v), sizeof(float) * 4);
                    else if constexpr (std::is_same_v<T, float>) *reinterpret_cast<float*>(dst) = v;
                    else if constexpr (std::is_same_v<T, int>) *reinterpret_cast<int*>(dst) = v;
                }, it->second);
            }
        }

        if (m_BufMaterialsMemory != VK_NULL_HANDLE && m_MaterialDynamicStride != 0) {
            outDynamicOffsetThisDraw = m_MaterialDynamicOffset;
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_BufMaterialsMemory, outDynamicOffsetThisDraw, sizeof(RHI::Material), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &material, sizeof(RHI::Material));
                vkUnmapMemory(m_Device, m_BufMaterialsMemory);
            }
            m_MaterialDynamicOffset += m_MaterialDynamicStride;
        }
    }

    void VK_Shaders::BindDescriptorSets(VkCommandBuffer cmd, VkDeviceSize mvpDynamicOffset, VkDeviceSize materialDynamicOffset) {
        if (m_SceneDescriptorSet != VK_NULL_HANDLE) {
            uint32_t dynOffsets[2] = {
                static_cast<uint32_t>(mvpDynamicOffset),
                static_cast<uint32_t>(materialDynamicOffset)
            };
            const uint32_t dynCount =
                (m_MvpDynamicStride != 0 && m_MaterialDynamicStride != 0) ? 2u :
                (m_MvpDynamicStride != 0 || m_MaterialDynamicStride != 0) ? 1u : 0u;

            if (dynCount == 2) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                    static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 2, dynOffsets);
            } else if (dynCount == 1) {
                uint32_t one = (m_MvpDynamicStride != 0) ? dynOffsets[0] : dynOffsets[1];
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                    static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 1, &one);
            } else {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                    static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 0, nullptr);
            }
        }

        if (m_UserDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                static_cast<uint32_t>(RHI::kUserDescriptorSet), 1, &m_UserDescriptorSet, 0, nullptr);
        }
    }

    void VK_Shaders::ApplyParameters(void* apiContext) {
        if (!apiContext || m_PipelineLayout == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);

        UploadFrameUniforms();

        VkDeviceSize mvpOffsetThisDraw = 0;
        VkDeviceSize materialOffsetThisDraw = 0;
        UploadMvpUniforms(mvpOffsetThisDraw);
        UploadMaterialUniforms(materialOffsetThisDraw);

        BindDescriptorSets(cmd, mvpOffsetThisDraw, materialOffsetThisDraw);
    }

    void VK_Shaders::UploadInstanceBuffer(const std::vector<RHI::Instance>& instances) {
        if (m_Device == VK_NULL_HANDLE || m_BufInstancesMemory == VK_NULL_HANDLE || instances.empty())
            return;

        const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(instances.size() * sizeof(RHI::Instance));
        if (requiredSize > m_BufInstancesSize)
            return;

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, m_BufInstancesMemory, 0, requiredSize, 0, &mapped) != VK_SUCCESS)
            return;

        std::memcpy(mapped, instances.data(), static_cast<size_t>(requiredSize));
        vkUnmapMemory(m_Device, m_BufInstancesMemory);
    }

    void* VK_Shaders::GetNativeHandle() const {
        return reinterpret_cast<void*>(m_Pipeline);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan