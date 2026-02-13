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
        ~VK_Renderer() = default;

        bool Create() override;
        void Destroy() override;

        bool Resize(int w, int h) override;
        void Update(float dt) override;

        void BeginFrame() override;
        void Render() override;
        void EndFrame() override;

    private:
        // Core Vulkan objects (wrappers)
        VK_Instance m_VKInstance;
        VK_Device   m_VKDevice;
        VK_Swapchain m_VKSwapchain;

        bool m_FramebufferResized = false;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H