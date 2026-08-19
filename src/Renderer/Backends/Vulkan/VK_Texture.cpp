#include "Renderer/Backends/Vulkan/VK_Texture.h"

#include "Core/Log.h"
#include "Renderer/Backends/Vulkan/VK_Common.h"

#include "backends/imgui_impl_vulkan.h"
#include "imgui.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    VK_Texture::VK_Texture(const Renderer::RHI::RHI_Texture& src)
        : Renderer::RHI::RHI_Texture(src.m_Width, src.m_Height, src.m_Pixels, src.m_CreateImGuiID)
    {}

    VK_Texture::~VK_Texture() {
        Release();
    }

    bool VK_Texture::Init(VK_MemoryAllocator* allocator, VkDevice device,
                          VkCommandPool commandPool, VkQueue graphicsQueue) {
        m_Allocator = allocator;
        m_Device = device;
        m_CommandPool = commandPool;
        m_GraphicsQueue = graphicsQueue;
        return m_Allocator != nullptr && m_Device != VK_NULL_HANDLE;
    }

    bool VK_Texture::CopyBufferToImage(VkBuffer staging, uint32_t width, uint32_t height) {
        VkCommandBufferAllocateInfo allocInfo{};
        allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocInfo.commandPool = m_CommandPool;
        allocInfo.commandBufferCount = 1;

        VkCommandBuffer cmd = VK_NULL_HANDLE;
        if (vkAllocateCommandBuffers(m_Device, &allocInfo, &cmd) != VK_SUCCESS)
            return false;

        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(cmd, &beginInfo);

        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = m_Image.image;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.levelCount = 1;
        toTransfer.subresourceRange.layerCount = 1;
        toTransfer.srcAccessMask = 0;
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toTransfer);

        VkBufferImageCopy region{};
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.layerCount = 1;
        region.imageExtent = { width, height, 1 };

        vkCmdCopyBufferToImage(cmd, staging, m_Image.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        VkImageMemoryBarrier toShader{};
        toShader.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        toShader.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toShader.image = m_Image.image;
        toShader.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toShader.subresourceRange.levelCount = 1;
        toShader.subresourceRange.layerCount = 1;
        toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(cmd,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            0, 0, nullptr, 0, nullptr, 1, &toShader);

        vkEndCommandBuffer(cmd);

        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &cmd;

        vkQueueSubmit(m_GraphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
        vkQueueWaitIdle(m_GraphicsQueue);

        vkFreeCommandBuffers(m_Device, m_CommandPool, 1, &cmd);
        return true;
    }

    void VK_Texture::Upload(const Renderer::RHI::RHI_Texture& src) {
        Release();

        if (m_Allocator == nullptr || !m_Allocator->IsValid() || m_Device == VK_NULL_HANDLE) {
            NV_LOG_ERROR("VK_Texture::Upload - not initialized. Call Init() first.");
            return;
        }

        m_Width = src.m_Width;
        m_Height = src.m_Height;
        m_Pixels = src.m_Pixels;
        m_CreateImGuiID = src.m_CreateImGuiID;

        if (m_Width == 0 || m_Height == 0 || m_Pixels.empty()) {
            NV_LOG_ERROR("VK_Texture::Upload - empty texture");
            return;
        }

        const size_t expected = static_cast<size_t>(m_Width) * static_cast<size_t>(m_Height) * 4u;
        if (m_Pixels.size() < expected) {
            NV_LOG_ERROR("VK_Texture::Upload - pixel buffer too small (RGBA8 required)");
            return;
        }

        VkImageCreateInfo imageInfo{};
        imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        imageInfo.imageType = VK_IMAGE_TYPE_2D;
        imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        imageInfo.extent = { m_Width, m_Height, 1 };
        imageInfo.mipLevels = 1;
        imageInfo.arrayLayers = 1;
        imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
        imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
        imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        imageInfo.usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;

        if (!m_Allocator->CreateImage(imageInfo, VK_MemoryLocation::GpuOnly, m_Image)) {
            NV_LOG_ERROR("VK_Texture::Upload - CreateImage failed");
            return;
        }

        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = m_Image.image;
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;

        if (vkCreateImageView(m_Device, &viewInfo, nullptr, &m_View) != VK_SUCCESS) {
            m_Allocator->DestroyImage(m_Image);
            NV_LOG_ERROR("VK_Texture::Upload - CreateImageView failed");
            return;
        }

        VkSamplerCreateInfo samplerInfo{};
        samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        samplerInfo.magFilter = VK_FILTER_LINEAR;
        samplerInfo.minFilter = VK_FILTER_LINEAR;
        samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        samplerInfo.maxLod = 1.0f;

        if (vkCreateSampler(m_Device, &samplerInfo, nullptr, &m_Sampler) != VK_SUCCESS) {
            vkDestroyImageView(m_Device, m_View, nullptr);
            m_View = VK_NULL_HANDLE;
            m_Allocator->DestroyImage(m_Image);
            NV_LOG_ERROR("VK_Texture::Upload - CreateSampler failed");
            return;
        }

        VK_BufferAllocation staging{};
        if (!m_Allocator->CreateBuffer(expected, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                       VK_MemoryLocation::CpuReadWrite, staging)) {
            Release();
            NV_LOG_ERROR("VK_Texture::Upload - staging buffer failed");
            return;
        }

        m_Allocator->WriteToBuffer(staging, 0, expected, m_Pixels.data());

        if (!CopyBufferToImage(staging.buffer, m_Width, m_Height)) {
            m_Allocator->DestroyBuffer(staging);
            Release();
            NV_LOG_ERROR("VK_Texture::Upload - copy failed");
            return;
        }

        m_Allocator->DestroyBuffer(staging);

        if (m_CreateImGuiID) {
            m_ImGuiID = ImGui_ImplVulkan_AddTexture(
                m_Sampler, m_View, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    void VK_Texture::Release() {
        if (m_ImGuiID != nullptr) {
            if (ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendRendererUserData != nullptr)
                ImGui_ImplVulkan_RemoveTexture(static_cast<VkDescriptorSet>(m_ImGuiID));
            m_ImGuiID = nullptr;
        }

        if (m_Device != VK_NULL_HANDLE) {
            if (m_Sampler != VK_NULL_HANDLE) {
                vkDestroySampler(m_Device, m_Sampler, nullptr);
                m_Sampler = VK_NULL_HANDLE;
            }
            if (m_View != VK_NULL_HANDLE) {
                vkDestroyImageView(m_Device, m_View, nullptr);
                m_View = VK_NULL_HANDLE;
            }
        }

        if (m_Allocator)
            m_Allocator->DestroyImage(m_Image);
    }

} // namespace Nova::Core::Renderer::Backends::Vulkan