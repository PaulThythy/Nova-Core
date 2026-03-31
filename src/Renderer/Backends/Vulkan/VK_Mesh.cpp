#include "Renderer/Backends/Vulkan/VK_Mesh.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"
#include "Renderer/Graphics/Vertex.h"

#include "Core/Log.h"

#include <cstdint>
#include <cstring>
#include <iostream>

namespace Nova::Core::Renderer::Backends::Vulkan {

    VK_Mesh::VK_Mesh(const Renderer::Graphics::Mesh& mesh) : Renderer::Graphics::Mesh(mesh.GetVertices(), mesh.GetIndices()) {}

    VK_Mesh::~VK_Mesh() { Release(); }

    void VK_Mesh::Upload(const Renderer::Graphics::Mesh& mesh) {
        Release();

        if (m_Device == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_Mesh::Upload - device not initialized. Call Init() first.");
            return;
        }

        const auto& vertices = mesh.GetVertices();
        const auto& indices = mesh.GetIndices();
        m_IndexCount = static_cast<int>(indices.size());

        // ---- Vertex buffer ----
        {
            const VkDeviceSize size = vertices.size() * sizeof(Renderer::Graphics::Vertex);

            VkBuffer       stagingBuf;
            VkDeviceMemory stagingMem;
            if (!CreateBuffer(size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuf, stagingMem))
            {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create vertex staging buffer");
                return;
            }

            void* data;
            vkMapMemory(m_Device, stagingMem, 0, size, 0, &data);
            std::memcpy(data, vertices.data(), static_cast<size_t>(size));
            vkUnmapMemory(m_Device, stagingMem);

            if (!CreateBuffer(size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_VertexBuffer, m_VertexBufferMemory))
            {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create device-local vertex buffer");
                vkDestroyBuffer(m_Device, stagingBuf, nullptr);
                vkFreeMemory(m_Device, stagingMem, nullptr);
                return;
            }

            CopyBuffer(stagingBuf, m_VertexBuffer, size);

            vkDestroyBuffer(m_Device, stagingBuf, nullptr);
            vkFreeMemory(m_Device, stagingMem, nullptr);
        }

        // ---- Index buffer ----
        {
            const VkDeviceSize size = indices.size() * sizeof(uint32_t);

            VkBuffer       stagingBuf;
            VkDeviceMemory stagingMem;
            if (!CreateBuffer(size,
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                stagingBuf, stagingMem))
            {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create index staging buffer");
                return;
            }

            void* data;
            vkMapMemory(m_Device, stagingMem, 0, size, 0, &data);
            std::memcpy(data, indices.data(), static_cast<size_t>(size));
            vkUnmapMemory(m_Device, stagingMem);

            if (!CreateBuffer(size,
                VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
                VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                m_IndexBuffer, m_IndexBufferMemory))
            {
                NV_LOG_ERROR("VK_Mesh::Upload - failed to create device-local index buffer");
                vkDestroyBuffer(m_Device, stagingBuf, nullptr);
                vkFreeMemory(m_Device, stagingMem, nullptr);
                return;
            }

            CopyBuffer(stagingBuf, m_IndexBuffer, size);

            vkDestroyBuffer(m_Device, stagingBuf, nullptr);
            vkFreeMemory(m_Device, stagingMem, nullptr);
        }
    }

    void VK_Mesh::Release() {
        if (m_Device == VK_NULL_HANDLE) return;

        if (m_IndexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_IndexBuffer, nullptr);
            vkFreeMemory(m_Device, m_IndexBufferMemory, nullptr);
            m_IndexBuffer = VK_NULL_HANDLE;
            m_IndexBufferMemory = VK_NULL_HANDLE;
        }
        if (m_VertexBuffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(m_Device, m_VertexBuffer, nullptr);
            vkFreeMemory(m_Device, m_VertexBufferMemory, nullptr);
            m_VertexBuffer = VK_NULL_HANDLE;
            m_VertexBufferMemory = VK_NULL_HANDLE;
        }
        m_IndexCount = 0;
    }

    void VK_Mesh::Bind() const {
        if (m_ActiveCmd == VK_NULL_HANDLE) return;
        VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(m_ActiveCmd, 0, 1, &m_VertexBuffer, &offset);
        vkCmdBindIndexBuffer(m_ActiveCmd, m_IndexBuffer, 0, VK_INDEX_TYPE_UINT32);
    }

    void VK_Mesh::Unbind() const {
        // No-op in Vulkan (state lives in the command buffer)
    }

    void VK_Mesh::Draw() const {
        if (m_ActiveCmd == VK_NULL_HANDLE) return;
        vkCmdDrawIndexed(m_ActiveCmd, static_cast<uint32_t>(m_IndexCount), 1, 0, 0, 0);
    }

    uint32_t VK_Mesh::FindMemoryType(uint32_t typeFilter,
        VkMemoryPropertyFlags properties) const
    {
        VkPhysicalDeviceMemoryProperties memProps;
        vkGetPhysicalDeviceMemoryProperties(m_PhysicalDevice, &memProps);

        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((typeFilter & (1u << i)) &&
                (memProps.memoryTypes[i].propertyFlags & properties) == properties)
            {
                return i;
            }
        }

        NV_LOG_ERROR("VK_Mesh::FindMemoryType - no suitable memory type found");
        return UINT32_MAX;
    }

    bool VK_Mesh::CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags properties, VkBuffer& outBuffer, VkDeviceMemory& outMemory) const {
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = usage;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

        VkResult res = vkCreateBuffer(m_Device, &bufferInfo, nullptr, &outBuffer);
        CheckVkResult(res);
        if (res != VK_SUCCESS) return false;

        VkMemoryRequirements memReq;
        vkGetBufferMemoryRequirements(m_Device, outBuffer, &memReq);

        const uint32_t memTypeIndex = FindMemoryType(memReq.memoryTypeBits, properties);
        if (memTypeIndex == UINT32_MAX) {
            vkDestroyBuffer(m_Device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        VkMemoryAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocInfo.allocationSize = memReq.size;
        allocInfo.memoryTypeIndex = memTypeIndex;

        res = vkAllocateMemory(m_Device, &allocInfo, nullptr, &outMemory);
        CheckVkResult(res);
        if (res != VK_SUCCESS) {
            vkDestroyBuffer(m_Device, outBuffer, nullptr);
            outBuffer = VK_NULL_HANDLE;
            return false;
        }

        vkBindBufferMemory(m_Device, outBuffer, outMemory, 0);
        return true;
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