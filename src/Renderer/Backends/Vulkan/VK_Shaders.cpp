#include "Renderer/Backends/Vulkan/VK_Shaders.h"

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

    void VK_Shaders::Bind(void* apiContext) {
        if (!apiContext || m_Pipeline == VK_NULL_HANDLE) return;
        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_Pipeline);
    }

    void VK_Shaders::ApplyParameters(void* apiContext) {
        if (!apiContext || m_PipelineLayout == VK_NULL_HANDLE) return;

        glm::mat4 model(1.0f), view(1.0f), proj(1.0f);
        auto itM = m_Parameters.find("model");
        auto itV = m_Parameters.find("view");
        auto itP = m_Parameters.find("proj");
        if (itM != m_Parameters.end() && std::holds_alternative<glm::mat4>(itM->second))
            model = std::get<glm::mat4>(itM->second);
        if (itV != m_Parameters.end() && std::holds_alternative<glm::mat4>(itV->second))
            view = std::get<glm::mat4>(itV->second);
        if (itP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itP->second))
            proj = std::get<glm::mat4>(itP->second);

        struct MVPPushConstants {
            glm::mat4 model;
            glm::mat4 view;
            glm::mat4 proj;
        } mvp{ model, view, proj };

        VkCommandBuffer cmd = static_cast<VkCommandBuffer>(apiContext);
        vkCmdPushConstants(cmd,
            m_PipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(MVPPushConstants),
            &mvp);
    }

    void* VK_Shaders::GetNativeHandle() const {
        return reinterpret_cast<void*>(m_Pipeline);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan