#ifndef VK_MESH_H
#define VK_MESH_H

#include <vulkan/vulkan.h>
#include "Api.h"
#include "Renderer/RHI/RHI_Mesh.h"
#include "Renderer/Backends/Vulkan/VK_MemoryAllocator.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

	struct NV_API VK_Mesh : public Renderer::RHI::RHI_Mesh {
		
		VK_Mesh() = default;
		explicit VK_Mesh(const Renderer::RHI::RHI_Mesh& mesh);
		~VK_Mesh() override;

		bool Init(VK_MemoryAllocator* allocator, VkDevice device, VkCommandPool commandPool, VkQueue graphicsQueue) {
			m_Allocator = allocator;
			m_Device = device;
			m_CommandPool = commandPool;
			m_GraphicsQueue = graphicsQueue;
			return m_Allocator != nullptr && m_Device != VK_NULL_HANDLE;
		}

		void Upload(const Renderer::RHI::RHI_Mesh& mesh) override;
		void Release() override;

		void Bind()   const override;
		void Unbind() const override; // no-op
		void Draw()   const override;

		VkBuffer GetVertexBuffer() const { return m_VertexBuffer.buffer; }
		VkBuffer GetIndexBuffer()  const { return m_IndexBuffer.buffer; }
		int      GetIndexCount()   const { return m_IndexCount; }

		void SetCommandBuffer(VkCommandBuffer cmd) { m_ActiveCmd = cmd; }

		VK_MemoryAllocator* m_Allocator = nullptr;
		VkDevice         m_Device = VK_NULL_HANDLE;
		VkCommandPool    m_CommandPool = VK_NULL_HANDLE;
		VkQueue          m_GraphicsQueue = VK_NULL_HANDLE;

		VK_BufferAllocation m_VertexBuffer{};
		VK_BufferAllocation m_IndexBuffer{};

		int m_IndexCount = 0;

		mutable VkCommandBuffer m_ActiveCmd = VK_NULL_HANDLE;

		bool CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;
	
	};

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_MESH_H