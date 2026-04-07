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
        VkBuffer bufMvp, VkDeviceMemory bufMvpMemory,
        VkBuffer bufMaterials, VkDeviceMemory bufMaterialsMemory,
        VkBuffer bufInstances, VkDeviceMemory bufInstancesMemory, VkDeviceSize bufInstancesSize,
        VkDescriptorSet sceneDescriptorSet)
    {
        m_Device = device;
        m_BufFrameUniforms = bufFrameUniforms;
        m_BufFrameUniformsMemory = bufFrameUniformsMemory;
        m_BufMvp = bufMvp;
        m_BufMvpMemory = bufMvpMemory;
        m_BufMaterials = bufMaterials;
        m_BufMaterialsMemory = bufMaterialsMemory;
        m_BufInstances = bufInstances;
        m_BufInstancesMemory = bufInstancesMemory;
        m_BufInstancesSize = bufInstancesSize;
        m_SceneDescriptorSet = sceneDescriptorSet;
    }

    void VK_Shaders::Bind(void* apiContext) {
        if (!apiContext || m_Pipeline == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    }

    void VK_Shaders::ApplyParameters(void* apiContext) {
        if (!apiContext || m_PipelineLayout == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);

        // Per-draw UBOs must be updated inside the command buffer (vkCmdUpdateBuffer), not only via
        // host memcpy before vkQueueSubmit. Otherwise every draw in the buffer sees the last CPU write.

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
                    else if constexpr (std::is_same_v<T, glm::vec3>) *reinterpret_cast<glm::vec3*>(dst) = v;
                    else if constexpr (std::is_same_v<T, glm::vec4>) *reinterpret_cast<glm::vec4*>(dst) = v;
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
                    else if constexpr (std::is_same_v<T, glm::vec4>) *reinterpret_cast<glm::vec4*>(dst) = v;
                    else if constexpr (std::is_same_v<T, float>) *reinterpret_cast<float*>(dst) = v;
                    else if constexpr (std::is_same_v<T, int>) *reinterpret_cast<int*>(dst) = v;
                }, it->second);
            }
        }

        bool wroteUniforms = false;
        if (m_BufFrameUniforms != VK_NULL_HANDLE) {
            vkCmdUpdateBuffer(cmd, m_BufFrameUniforms, 0, sizeof(RHI::FrameUniforms), &frameUniforms);
            wroteUniforms = true;
        }
        if (m_BufMvp != VK_NULL_HANDLE) {
            vkCmdUpdateBuffer(cmd, m_BufMvp, 0, sizeof(RHI::MVP), &mvp);
            wroteUniforms = true;
        }
        if (m_BufMaterials != VK_NULL_HANDLE) {
            vkCmdUpdateBuffer(cmd, m_BufMaterials, 0, sizeof(RHI::Material), &material);
            wroteUniforms = true;
        }

        if (wroteUniforms) {
            VkMemoryBarrier barrier{};
            barrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
            barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_UNIFORM_READ_BIT;
            vkCmdPipelineBarrier(cmd,
                VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
                0, 1, &barrier, 0, nullptr, 0, nullptr);
        }

        if (m_SceneDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                static_cast<uint32_t>(RHI::kEngineDescriptorSet), 1, &m_SceneDescriptorSet, 0, nullptr);
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