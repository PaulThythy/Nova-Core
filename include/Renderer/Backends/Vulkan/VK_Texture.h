#ifndef VK_TEXTURE_H
#define VK_TEXTURE_H

#include <vector>

#include <vulkan/vulkan.h>

#include "Api.h"
#include "Renderer/RHI/RHI_Texture.h"
#include "Renderer/RHI/RHI_RenderGraph.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    /**
     * Vulkan GPU texture: asset uploads (CPU pixels) and render-graph render targets.
     */
    class NV_API VK_Texture final : public Renderer::RHI::RHI_Texture {
    public:
        VK_Texture() = default;
        explicit VK_Texture(const Renderer::RHI::RHI_Texture& src);
        ~VK_Texture() override;

        VK_Texture(const VK_Texture&) = delete;
        VK_Texture& operator=(const VK_Texture&) = delete;
        VK_Texture(VK_Texture&& other) noexcept;
        VK_Texture& operator=(VK_Texture&& other) noexcept;

        bool Init(VK_MemoryAllocator* allocator, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue);

        void Upload(const Renderer::RHI::RHI_Texture& src) override;
        void Release() override;
        void* GetImGuiID() const override { return m_ImGuiID; }
        /** Unregister from ImGui without destroying GPU resources (backend shutdown). */
        void UnregisterImGui();

        /**
         * Create a render-graph GPU image (color/depth attachment, optional sampling).
         * `format` must already be resolved (e.g. depth format chosen by the device).
         */
        bool CreateAsRenderTarget(
            VK_MemoryAllocator& allocator,
            VkDevice device,
            const RHI::RHI_TextureDesc& desc,
            uint32_t width,
            uint32_t height,
            VkFormat format);

        VkImage GetImage() const { return m_Image.image; }
        VK_ImageAllocation& GetImageAllocation() { return m_Image; }
        const VK_ImageAllocation& GetImageAllocation() const { return m_Image; }

        VkImageView GetImageView() const { return m_View; }
        /** Sampling view (may differ from attachment view for depth swizzle / arrays). */
        VkImageView GetSampledView() const {
            return m_SampledView != VK_NULL_HANDLE ? m_SampledView : m_View;
        }
        VkSampler GetSampler() const { return m_Sampler; }

        VkFramebuffer GetFramebuffer() const { return m_Framebuffer; }
        void SetFramebuffer(VkFramebuffer fb) { m_Framebuffer = fb; }

        std::vector<VkImageView>& GetLayerViews() { return m_LayerViews; }
        const std::vector<VkImageView>& GetLayerViews() const { return m_LayerViews; }
        std::vector<VkFramebuffer>& GetLayerFramebuffers() { return m_LayerFramebuffers; }
        const std::vector<VkFramebuffer>& GetLayerFramebuffers() const { return m_LayerFramebuffers; }

        RHI::RHI_ResourceState GetResourceState() const { return m_State; }
        void SetResourceState(RHI::RHI_ResourceState state) { m_State = state; }

        RHI::RHI_RenderGraphTextureResource& GetGraphDesc() { return m_GraphDesc; }
        const RHI::RHI_RenderGraphTextureResource& GetGraphDesc() const { return m_GraphDesc; }

    private:
        bool CopyBufferToImage(VkBuffer staging, uint32_t width, uint32_t height);
        void DestroyGpuObjects();

        VK_MemoryAllocator* m_Allocator = nullptr;
        VkDevice m_Device = VK_NULL_HANDLE;
        VkCommandPool m_CommandPool = VK_NULL_HANDLE;
        VkQueue m_GraphicsQueue = VK_NULL_HANDLE;

        RHI::RHI_RenderGraphTextureResource m_GraphDesc{};
        VK_ImageAllocation m_Image{};
        VkImageView m_View = VK_NULL_HANDLE;
        VkImageView m_SampledView = VK_NULL_HANDLE;
        std::vector<VkImageView> m_LayerViews;
        std::vector<VkFramebuffer> m_LayerFramebuffers;
        VkSampler m_Sampler = VK_NULL_HANDLE;
        void* m_ImGuiID = nullptr;
        VkFramebuffer m_Framebuffer = VK_NULL_HANDLE;
        RHI::RHI_ResourceState m_State = RHI::RHI_ResourceState::Undefined;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_TEXTURE_H