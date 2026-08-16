#ifndef VK_TEXTURE_H
#define VK_TEXTURE_H

#include <vulkan/vulkan.h>

#include "Api.h"
#include "Renderer/RHI/RHI_Texture.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    struct NV_API VK_Texture final : public Renderer::RHI::RHI_Texture {
        VK_Texture() = default;
        explicit VK_Texture(const Renderer::RHI::RHI_Texture& src);
        ~VK_Texture() override;

        bool Init(VK_MemoryAllocator* allocator, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue);

        void Upload(const Renderer::RHI::RHI_Texture& src) override;
        void Release() override;
        void* GetImGuiID() const override { return m_ImGuiID; }

        VkImageView GetImageView() const { return m_View; }
        VkSampler GetSampler() const { return m_Sampler; }

    private:
        bool CopyBufferToImage(VkBuffer staging, uint32_t width, uint32_t height);

        VK_MemoryAllocator* m_Allocator = nullptr;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

        VK_ImageAllocation m_Image{};
        VkImageView m_View = VK_NULL_HANDLE;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        void* m_ImGuiID = nullptr;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_TEXTURE_H