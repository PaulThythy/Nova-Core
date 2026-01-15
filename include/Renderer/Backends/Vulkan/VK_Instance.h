#ifndef VK_INSTANCE_H
#define VK_INSTANCE_H

#include <cstdint>
#include <vector>
#include <vulkan/vulkan.h>

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Instance {
    public:
        VK_Instance() = default;
        ~VK_Instance() = default;

        // Initialization
        bool CreateInstance();
        void DestroyInstance();

        // Surface management
        bool CreateSurface();
        void DestroySurface();

        // Getters
        VkInstance GetInstance() const { return m_Instance; }
        VkSurfaceKHR GetSurface() const { return m_Surface; }

    private:
        VkInstance   m_Instance = VK_NULL_HANDLE;
        VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_INSTANCE_H
