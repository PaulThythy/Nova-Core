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

    void VK_Shaders::Bind(void* apiContext) {
        if (!apiContext || m_Pipeline == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    }

    void VK_Shaders::ApplyParameters(void* apiContext) {
        if (!apiContext || m_PipelineLayout == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);

        RHI::FrameUniforms frameUniforms{};
        if (m_BufFrameUniforms != VK_NULL_HANDLE) {
            const auto frameUniformsLayout = RHI::GetFrameUniformsLayout();
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

        RHI::MVP mvp{};
        if (m_BufMvp != VK_NULL_HANDLE) {
            auto itM = m_Parameters.find("model"), itV = m_Parameters.find("view"), itP = m_Parameters.find("proj"), itVP = m_Parameters.find("viewProj"), itInvVP = m_Parameters.find("invViewProj");
            if (itM != m_Parameters.end() && std::holds_alternative<glm::mat4>(itM->second)) mvp.model = std::get<glm::mat4>(itM->second);
            if (itV != m_Parameters.end() && std::holds_alternative<glm::mat4>(itV->second)) mvp.view = std::get<glm::mat4>(itV->second);
            if (itP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itP->second)) mvp.proj = std::get<glm::mat4>(itP->second);
            if (itVP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itVP->second)) mvp.viewProj = std::get<glm::mat4>(itVP->second);
            if (itInvVP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itInvVP->second)) mvp.invViewProj = std::get<glm::mat4>(itInvVP->second);
        }

        RHI::Material material{};
        if (m_BufMaterials != VK_NULL_HANDLE) {
            const auto layout = RHI::GetMaterialParameterLayout();
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

        // Upload frame data once per draw (cheap) via host-visible coherent memory.
        // For per-draw data (MVP, Material), use *dynamic* uniform buffers and advance offsets
        // so each draw sees its own snapshot.
        if (m_BufFrameUniformsMemory != VK_NULL_HANDLE) {
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_BufFrameUniformsMemory, 0, sizeof(RHI::FrameUniforms), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &frameUniforms, sizeof(RHI::FrameUniforms));
                vkUnmapMemory(m_Device, m_BufFrameUniformsMemory);
            }
        }

        VkDeviceSize mvpOffsetThisDraw = 0;
        if (m_BufMvpMemory != VK_NULL_HANDLE && m_MvpDynamicStride != 0) {
            mvpOffsetThisDraw = m_MvpDynamicOffset;
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_BufMvpMemory, mvpOffsetThisDraw, sizeof(RHI::MVP), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &mvp, sizeof(RHI::MVP));
                vkUnmapMemory(m_Device, m_BufMvpMemory);
            }
            m_MvpDynamicOffset += m_MvpDynamicStride;
        }

        VkDeviceSize materialOffsetThisDraw = 0;
        if (m_BufMaterialsMemory != VK_NULL_HANDLE && m_MaterialDynamicStride != 0) {
            materialOffsetThisDraw = m_MaterialDynamicOffset;
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_BufMaterialsMemory, materialOffsetThisDraw, sizeof(RHI::Material), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &material, sizeof(RHI::Material));
                vkUnmapMemory(m_Device, m_BufMaterialsMemory);
            }
            m_MaterialDynamicOffset += m_MaterialDynamicStride;
        }

        if (m_SceneDescriptorSet != VK_NULL_HANDLE) {
            // Dynamic offsets must be provided in the order of dynamic bindings in the set layout.
            // Here: MVP then Material.
            uint32_t dynOffsets[2] = {
                static_cast<uint32_t>(mvpOffsetThisDraw),
                static_cast<uint32_t>(materialOffsetThisDraw)
            };
            const uint32_t dynCount =
                (m_MvpDynamicStride != 0 && m_MaterialDynamicStride != 0) ? 2u :
                (m_MvpDynamicStride != 0 || m_MaterialDynamicStride != 0) ? 1u : 0u;

            if (dynCount == 2) {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                    static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 2, dynOffsets);
            } else if (dynCount == 1) {
                // If only one dynamic binding is enabled, choose the matching offset.
                uint32_t one = (m_MvpDynamicStride != 0) ? dynOffsets[0] : dynOffsets[1];
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                    static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 1, &one);
            } else {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                    static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 0, nullptr);
            }
        }

        // Bind user descriptor set (set 1) if present. Keep it separate so we don't have to
        // compute a merged dynamic offset list yet (user set is typically non-dynamic).
        if (m_UserDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                static_cast<uint32_t>(RHI::kUserDescriptorSet), 1, &m_UserDescriptorSet, 0, nullptr);
        }
    }

    void VK_Shaders::SetInstanceData(const std::vector<RHI::Instance>& instances) {
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