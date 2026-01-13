#ifndef VK_RENDERER_H
#define VK_RENDERER_H

#include <cstdint>

#include <vulkan/vulkan.h>

#include "Renderer/RHI/RHI_Renderer.h"

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
		bool CreateInstance();
		bool CreateSurface();
		bool PickPhysicalDevice();
		bool CreateDevice();

		VkInstance m_Instance = VK_NULL_HANDLE;
		VkSurfaceKHR m_Surface = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkDevice m_Device = VK_NULL_HANDLE;
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_RENDERER_H