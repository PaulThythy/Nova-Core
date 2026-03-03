#ifndef VK_SHADERS_H
#define VK_SHADERS_H

#include <vulkan/vulkan.h>
#include <vector>

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Shaders {
    public:
        VK_Shaders() = default;
        ~VK_Shaders() { Destroy(); }

        VK_Shaders(const VK_Shaders&) = delete;
        VK_Shaders& operator=(const VK_Shaders&) = delete;

        VK_Shaders(VK_Shaders&& other) noexcept { MoveFrom(other); }
        VK_Shaders& operator=(VK_Shaders&& other) noexcept {
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
        void MoveFrom(VK_Shaders& other) noexcept {
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