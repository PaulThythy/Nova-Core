#ifndef VK_MESH_H
#define VK_MESH_H

#include <vulkan/vulkan.h>
#include "Renderer/Graphics/Mesh.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	struct VK_Mesh : public Renderer::Graphics::Mesh {
		
		VK_Mesh() = default;
		explicit VK_Mesh(const Renderer::Graphics::Mesh& mesh);
		~VK_Mesh() override;

		bool Init(VkDevice device, VkPhysicalDevice physicalDevice, VkCommandPool commandPool, VkQueue graphicsQueue) {
			m_Device = device;
			m_PhysicalDevice = physicalDevice;
			m_CommandPool = commandPool;
			m_GraphicsQueue = graphicsQueue;
			return true;
		}

		void Upload(const Renderer::Graphics::Mesh& mesh) override;
		void Release() override;

		void Bind()   const override;
		void Unbind() const override; // no-op
		void Draw()   const override;

		VkBuffer GetVertexBuffer() const { return m_VertexBuffer; }
		VkBuffer GetIndexBuffer()  const { return m_IndexBuffer; }
		int      GetIndexCount()   const { return m_IndexCount; }

		void SetCommandBuffer(VkCommandBuffer cmd) { m_ActiveCmd = cmd; }

		VkDevice         m_Device = VK_NULL_HANDLE;
		VkPhysicalDevice m_PhysicalDevice = VK_NULL_HANDLE;
		VkCommandPool    m_CommandPool = VK_NULL_HANDLE;
		VkQueue          m_GraphicsQueue = VK_NULL_HANDLE;

		VkBuffer       m_VertexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_VertexBufferMemory = VK_NULL_HANDLE;

		VkBuffer       m_IndexBuffer = VK_NULL_HANDLE;
		VkDeviceMemory m_IndexBufferMemory = VK_NULL_HANDLE;

		int m_IndexCount = 0;

		mutable VkCommandBuffer m_ActiveCmd = VK_NULL_HANDLE;

		uint32_t FindMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) const;

		bool CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& outBuffer, VkDeviceMemory& outMemory) const;

		bool CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
	
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_MESH_H