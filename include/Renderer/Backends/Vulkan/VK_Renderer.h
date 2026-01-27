#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>
#include <vector>

#include <vulkan/vulkan.h>

#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/Vulkan/VK_Extensions.h"
#include "Renderer/Backends/Vulkan/VK_ValidationLayers.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Backends/Vulkan/VK_Instance.h"
#include "Renderer/Backends/Vulkan/VK_Device.h"
#include "Renderer/Backends/Vulkan/VK_Swapchain.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	class VK_Renderer final : public RHI::IRenderer {
	public:
		VK_Renderer() = default;
		~VK_Renderer() override = default;

		bool Create();
		void Destroy();

		bool Resize(int w, int h) override;

		void Update(float dt) override;

		void BeginFrame() override;
		void Render() override;
		void EndFrame() override;

	private:
        // --- Helpers ---
		bool RecreateSwapchain();
		bool CreateImGuiDescriptorPool();
		void DestroyImGuiDescriptorPool();

        VK_Instance       m_VKInstance;
        VK_Device         m_VKDevice;
        VK_Swapchain      m_VKSwapchain;

        VkDescriptorPool  m_ImGuiDescriptorPool = VK_NULL_HANDLE;

		// Frame state
		bool     m_FramebufferResized = false;
		bool     m_FrameActive = false;
		uint32_t m_CurrentImageIndex = 0;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H