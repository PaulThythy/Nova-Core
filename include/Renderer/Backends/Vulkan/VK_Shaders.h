#ifndef VK_SHADERS_H
#define VK_SHADERS_H

#include <vulkan/vulkan.h>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_ShaderModule {
    public:
        VK_ShaderModule() = default;
        ~VK_ShaderModule() { Destroy(); }

        VK_ShaderModule(const VK_ShaderModule&) = delete;
        VK_ShaderModule& operator=(const VK_ShaderModule&) = delete;

        VK_ShaderModule(VK_ShaderModule&& other) noexcept { MoveFrom(other); }
        VK_ShaderModule& operator=(VK_ShaderModule&& other) noexcept {
            if (this != &other) {
                Destroy();
                MoveFrom(other);
            }
            return *this;
        }

        bool Create(VkDevice device, const std::vector<uint32_t>& spirv);
        void Destroy();

        VkShaderModule GetModule() const { return m_Module; }
        bool IsValid() const { return m_Module != VK_NULL_HANDLE; }

    private:
        void MoveFrom(VK_ShaderModule& other) noexcept {
            m_Device = other.m_Device;
            m_Module = other.m_Module;
            other.m_Device = VK_NULL_HANDLE;
            other.m_Module = VK_NULL_HANDLE;
        }

        VkDevice m_Device = VK_NULL_HANDLE;
        VkShaderModule m_Module = VK_NULL_HANDLE;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan 

#endif // VK_SHADERS_H