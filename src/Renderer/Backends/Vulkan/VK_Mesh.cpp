#include "Renderer/Backends/Vulkan/VK_Mesh.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Math/Vertex.h"

#include "Core/Log.h"

#include <cstdint>
#include <cstring>

namespace Nova::Core::Renderer::Backends::Vulkan {

    VK_Mesh::VK_Mesh(const Renderer::RHI::RHI_Mesh& mesh)
        : Renderer::RHI::RHI_Mesh(
            Core::Buffer::Copy(mesh.GetVertexBuffer()),
            Core::Buffer::Copy(mesh.GetIndexBuffer())) {}

    VK_Mesh::~VK_Mesh() { Release(); }

    void VK_Mesh::Upload(const Renderer::RHI::RHI_Mesh& mesh) {
        Release();

        if (m_Allocator == nullptr || !m_Allocator->IsValid() || m_Device == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_Mesh::Upload - memory allocator not initialized. Call Init() first.");
            return;
        }

        const Core::Buffer& vertices = mesh.GetVertexBuffer();
        const Core::Buffer& indices = mesh.GetIndexBuffer();
        m_IndexCount = static_cast<int>(mesh.GetIndexCount());

        // ---- Vertex buffer ----
        if (vertices.Data && vertices.Size > 0) {
            const VkDeviceSize size = vertices.Size;

            VK_BufferAllocation staging;
            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MemoryLocation::CpuReadWrite, staging)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create vertex staging buffer");
                return;
            }

            m_Allocator->WriteToBuffer(staging, 0, size, vertices.Data);

            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MemoryLocation::GpuOnly, m_GpuVertexBuffer)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create device-local vertex buffer");
                m_Allocator->DestroyBuffer(staging);
                return;
            }

            CopyBuffer(staging.buffer, m_GpuVertexBuffer.buffer, size);
            m_Allocator->DestroyBuffer(staging);
        }

        // ---- Index buffer ----
        if (indices.Data && indices.Size > 0) {
            const VkDeviceSize size = indices.Size;

            VK_BufferAllocation staging;
            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MemoryLocation::CpuReadWrite, staging)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create index staging buffer");
                return;
            }

            m_Allocator->WriteToBuffer(staging, 0, size, indices.Data);

            if (!m_Allocator->CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MemoryLocation::GpuOnly, m_GpuIndexBuffer)) {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create device-local index buffer");
                m_Allocator->DestroyBuffer(staging);
                return;
            }

            CopyBuffer(staging.buffer, m_GpuIndexBuffer.buffer, size);
            m_Allocator->DestroyBuffer(staging);
        }
    }

    void VK_Mesh::Release() {
        if (m_Allocator == nullptr || !m_Allocator->IsValid())
            return;

        m_Allocator->DestroyBuffer(m_GpuIndexBuffer);
        m_Allocator->DestroyBuffer(m_GpuVertexBuffer);
        m_IndexCount = 0;
    }

    void VK_Mesh::Bind() const {
        if (m_ActiveCmd == VK_NULL_HANDLE) return;
        VkDeviceSize offset = 0;
        const VkBuffer vertexBuffer = m_GpuVertexBuffer.buffer;
        vkCmdBindVertexBuffers(m_ActiveCmd, 0, 1, &vertexBuffer, &offset);
        vkCmdBindIndexBuffer(m_ActiveCmd, m_GpuIndexBuffer.buffer, 0, VK_INDEX_TYPE_UINT32);
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