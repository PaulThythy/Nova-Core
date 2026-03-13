#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>
#include <unordered_map>

#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/Vulkan/VK_Extensions.h"
#include "Renderer/Backends/Vulkan/VK_ValidationLayers.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Backends/Vulkan/VK_Instance.h"
#include "Renderer/Backends/Vulkan/VK_Device.h"
#include "Renderer/Backends/Vulkan/VK_Swapchain.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_Mesh.h"

#include <memory>

namespace Nova::Core::Renderer::Backends::Vulkan {

	class VK_Renderer final : public RHI::IRenderer {
    public:
        VK_Renderer() = default;
        ~VK_Renderer() = default;

        bool Create() override;
        void Destroy() override;

        bool Resize(int w, int h) override;
        void Update(float dt) override;

        void BeginFrame() override;
        void EndFrame() override;

        void BeginScene(const glm::mat4& view, const glm::mat4& proj) override;
        void SetModelMatrix(const glm::mat4& model) override;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        // ImGui viewport: returns VkDescriptorSet for the offscreen viewport texture.
        void* GetViewportTextureID() const override;

        RHI::RHI_Shaders* GetShader() override { return m_Shader.get(); }

    private:
        void BeginImGuiRenderPass();
        void CreateViewportFramebuffer(int w, int h);
        void DestroyViewportFramebuffer();

    private:
        // Core Vulkan objects (wrappers)
        VK_Instance m_VKInstance;
        VK_Device   m_VKDevice;
        VK_Swapchain m_VKSwapchain;

        std::unique_ptr<VK_Shaders> m_Shader;
        std::unordered_map<const Renderer::Graphics::Mesh*, std::shared_ptr<VK_Mesh>> m_MeshCache;

        std::shared_ptr<VK_Mesh> GetOrUploadMesh(const std::shared_ptr<Renderer::Graphics::Mesh>& cpuMesh);

        bool m_FramebufferResized = false;

        // Viewport offscreen target for ImGui (when size > 0 we render scene here, then show in ImGui)
        int m_ViewportWidth = 0;
        int m_ViewportHeight = 0;
        VkImage m_ViewportImage = VK_NULL_HANDLE;
        VkImageView m_ViewportImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_ViewportImageMemory = VK_NULL_HANDLE;
        VkImage m_ViewportDepthImage = VK_NULL_HANDLE;
        VkImageView m_ViewportDepthImageView = VK_NULL_HANDLE;
        VkDeviceMemory m_ViewportDepthImageMemory = VK_NULL_HANDLE;
        VkFramebuffer m_ViewportFramebuffer = VK_NULL_HANDLE;
        VkSampler m_ViewportSampler = VK_NULL_HANDLE;
        VkDescriptorSet m_ViewportDescriptorSet = VK_NULL_HANDLE;
        bool m_RenderedToViewportThisFrame = false;
        bool m_ViewportImageFirstUse = true; // true until first viewport pass (image in UNDEFINED)
        bool m_FrameActive = false;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H