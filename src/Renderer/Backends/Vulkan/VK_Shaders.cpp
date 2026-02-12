#include "Renderer/Backends/Vulkan/VK_Shaders.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

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

} // namespace Nova::Core::Renderer::Backends::Vulkan

