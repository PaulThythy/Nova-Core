#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

#include <glm/glm.hpp>
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

    void VK_Shaders::SetSceneUBOs(VkDevice device,
        VkBuffer globalsUBOBuffer, VkDeviceMemory globalsUBOMemory,
        VkBuffer mvpUBOBuffer, VkDeviceMemory mvpUBOMemory,
        VkBuffer materialUBOBuffer, VkDeviceMemory materialUBOMemory,
        VkBuffer instanceBuffer, VkDeviceMemory instanceBufferMemory, VkDeviceSize instanceBufferSize,
        VkDescriptorSet sceneDescriptorSet)
    {
        m_Device = device;
        m_GlobalsUBOBuffer = globalsUBOBuffer;
        m_GlobalsUBOMemory = globalsUBOMemory;
        m_MVPUBOBuffer = mvpUBOBuffer;
        m_MVPUBOMemory = mvpUBOMemory;
        m_MaterialUBOBuffer = materialUBOBuffer;
        m_MaterialUBOMemory = materialUBOMemory;
        m_InstanceBuffer = instanceBuffer;
        m_InstanceBufferMemory = instanceBufferMemory;
        m_InstanceBufferSize = instanceBufferSize;
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

        // ---- Globals UBO (binding 0) ----
        if (m_GlobalsUBOMemory != VK_NULL_HANDLE) {
            RHI::Globals globals{};
            const auto globalsLayout = RHI::GetGlobalsLayout();
            for (const auto& [name, offset] : globalsLayout) {
                auto it = m_Parameters.find(name);
                if (it == m_Parameters.end()) continue;
                char* dst = reinterpret_cast<char*>(&globals) + offset;
                std::visit([dst](auto&& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, int>) *reinterpret_cast<int*>(dst) = v;
                    else if constexpr (std::is_same_v<T, float>) *reinterpret_cast<float*>(dst) = v;
                    else if constexpr (std::is_same_v<T, glm::vec3>) *reinterpret_cast<glm::vec3*>(dst) = v;
                    else if constexpr (std::is_same_v<T, glm::vec4>) *reinterpret_cast<glm::vec4*>(dst) = v;
                }, it->second);
            }
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_GlobalsUBOMemory, 0, sizeof(RHI::Globals), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &globals, sizeof(RHI::Globals));
                vkUnmapMemory(m_Device, m_GlobalsUBOMemory);
            }
        }

        // ---- MVP UBO (binding 1) ----
        if (m_MVPUBOMemory != VK_NULL_HANDLE) {
            RHI::UBO_MVP mvp{};
            auto itM = m_Parameters.find("model"), itV = m_Parameters.find("view"), itP = m_Parameters.find("proj");
            if (itM != m_Parameters.end() && std::holds_alternative<glm::mat4>(itM->second)) mvp.model = std::get<glm::mat4>(itM->second);
            if (itV != m_Parameters.end() && std::holds_alternative<glm::mat4>(itV->second)) mvp.view = std::get<glm::mat4>(itV->second);
            if (itP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itP->second)) mvp.proj = std::get<glm::mat4>(itP->second);
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_MVPUBOMemory, 0, sizeof(RHI::UBO_MVP), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &mvp, sizeof(RHI::UBO_MVP));
                vkUnmapMemory(m_Device, m_MVPUBOMemory);
            }
        }

        // ---- Material UBO (binding 3) ----
        if (m_MaterialUBOMemory != VK_NULL_HANDLE) {
            RHI::UBO_Material material{};
            const auto layout = RHI::GetMaterialUBOLayout();
            for (const auto& [name, offset] : layout) {
                auto it = m_Parameters.find(name);
                if (it == m_Parameters.end()) continue;
                char* dst = reinterpret_cast<char*>(&material) + offset;
                std::visit([dst](auto&& v) {
                    using T = std::decay_t<decltype(v)>;
                    if constexpr (std::is_same_v<T, glm::vec3>) *reinterpret_cast<glm::vec4*>(dst) = glm::vec4(v, 1.0f);
                    else if constexpr (std::is_same_v<T, glm::vec4>) *reinterpret_cast<glm::vec4*>(dst) = v;
                }, it->second);
            }
            void* mapped = nullptr;
            if (vkMapMemory(m_Device, m_MaterialUBOMemory, 0, sizeof(RHI::UBO_Material), 0, &mapped) == VK_SUCCESS) {
                std::memcpy(mapped, &material, sizeof(RHI::UBO_Material));
                vkUnmapMemory(m_Device, m_MaterialUBOMemory);
            }
        }

        // ---- Bind descriptor set (set 0 = Globals + MVP + Instances + Material) ----
        if (m_SceneDescriptorSet != VK_NULL_HANDLE) {
            vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_PipelineLayout,
                0, 1, &m_SceneDescriptorSet, 0, nullptr);
        }
    }

    void VK_Shaders::SetInstanceData(const std::vector<RHI::SSBO_InstanceData>& instances) {
        if (m_Device == VK_NULL_HANDLE || m_InstanceBufferMemory == VK_NULL_HANDLE || instances.empty())
            return;

        const VkDeviceSize requiredSize = static_cast<VkDeviceSize>(instances.size() * sizeof(RHI::SSBO_InstanceData));
        if (requiredSize > m_InstanceBufferSize)
            return;

        void* mapped = nullptr;
        if (vkMapMemory(m_Device, m_InstanceBufferMemory, 0, requiredSize, 0, &mapped) != VK_SUCCESS)
            return;

        std::memcpy(mapped, instances.data(), static_cast<size_t>(requiredSize));
        vkUnmapMemory(m_Device, m_InstanceBufferMemory);
    }

    void* VK_Shaders::GetNativeHandle() const {
        return reinterpret_cast<void*>(m_Pipeline);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan