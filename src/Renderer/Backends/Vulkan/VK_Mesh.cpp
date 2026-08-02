#include "Renderer/Backends/Vulkan/VK_Mesh.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Graphics/Vertex.h"

#include "Core/Log.h"

#include <cstdint>
#include <cstring>

namespace Nova::Core::Renderer::Backends::Vulkan {

    VK_Mesh::VK_Mesh(const Renderer::RHI::RHI_Mesh& mesh) : Renderer::RHI::RHI_Mesh(mesh.GetVertices(), mesh.GetIndices()) {}

    VK_Mesh::~VK_Mesh() { Release(); }

    void VK_Mesh::Upload(const Renderer::RHI::RHI_Mesh& mesh) {
        Release();

        if (m_Allocator == nullptr || !m_Allocator->IsValid() || m_Device == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_Mesh::Upload - memory allocator not initialized. Call Init() first.");
            return;
        }

        const auto& vertices = mesh.GetVertices();
        const auto& indices = mesh.GetIndices();
        m_IndexCount = static_cast<int>(indices.size());

        // ---- Vertex buffer ----
        {
            const VkDeviceSize size = vertices.size() * sizeof(Renderer::Graphics::Vertex);

            VK_BufferAllocation staging;
            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MemoryLocation::CpuReadWrite, staging)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create vertex staging buffer");
                return;
            }

            m_Allocator->WriteToBuffer(staging, 0, size, vertices.data());

            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MemoryLocation::GpuOnly, m_VertexBuffer)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create device-local vertex buffer");
                m_Allocator->DestroyBuffer(staging);
                return;
            }

            CopyBuffer(staging.buffer, m_VertexBuffer.buffer, size);
            m_Allocator->DestroyBuffer(staging);
        }

        // ---- Index buffer ----
        {
            const VkDeviceSize size = indices.size() * sizeof(uint32_t);

            VK_BufferAllocation staging;
            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MemoryLocation::CpuReadWrite, staging)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create index staging buffer");
                return;
            }

            m_Allocator->WriteToBuffer(staging, 0, size, indices.data());

            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MemoryLocation::GpuOnly, m_IndexBuffer)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create device-local index buffer");
                m_Allocator->DestroyBuffer(staging);
                return;
            }

            CopyBuffer(staging.buffer, m_IndexBuffer.buffer, size);
            m_Allocator->DestroyBuffer(staging);
        }
    }

    void VK_Mesh::Release() {
        if (m_Allocator == nullptr || !m_Allocator->IsValid())
            return;

        m_Allocator->DestroyBuffer(m_IndexBuffer);
        m_Allocator->DestroyBuffer(m_VertexBuffer);
        m_IndexCount = 0;
    }

    void VK_Mesh::Bind() const {
        if (m_ActiveCmd == VK_NULL_HANDLE) return;
        VkDeviceSize offset = 0;
        const VkBuffer vertexBuffer = m_VertexBuffer.buffer;
        vkCmdBindVertexBuffers(m_ActiveCmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(m_ActiveCmd, m_IndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
    }

    void VK_Mesh::Unbind() const {
        // No-op in Vulkan (state lives in the command buffer)
    }

    void VK_Mesh::Draw() const {
        if (m_ActiveCmd == VK_NULL_HANDLE) return;
        vkCmdDrawIndexed(m_ActiveCmd, static_cast<uint32_t>(m_IndexCount), 1, 0, 0, 0);
    }

    bool VK_Mesh::CopyBuffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const {
        // One-shot command buffer for the transfer
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd;
        VkResult res = vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkBufferCopy copyRegion{};
        copyRegion.size = size;
        vkCmdCopyBuffer(cmd, src, dst, 1, &copyRegion);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        // We wait idle here (upload path, not hot path)
        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_GraphicsQueue);

        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
        return true;
    }
} // namespace Nova::Core::Renderer::Backends::Vulkan