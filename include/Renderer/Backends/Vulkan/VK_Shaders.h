#ifndef VK_SHADERS_H
#define VK_SHADERS_H

#include <vulkan/vulkan.h>
#include <vector>
#include <utility>
#include <cstdint>
#include <glm/glm.hpp>

#include "Api.h"
#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    /** Single shader module (vertex or fragment). Used internally to build pipelines. */
    class NV_API VK_ShaderModule {
    public:
        VK_ShaderModule() = default;
        ~VK_ShaderModule() { Destroy(); }

        VK_ShaderModule(const VK_ShaderModule&) = delete;
        VK_ShaderModule& operator=(const VK_ShaderModule&) = delete;

        VK_ShaderModule(VK_ShaderModule&& other) noexcept { MoveFrom(other); }
        VK_ShaderModule& operator=(VK_ShaderModule&& other) noexcept {
            if (this != &other) {
                Destroy();
                MoveFrom(other);
            }
            return *this;
        }

        bool Create(VkDevice device, const std::vector<uint8_t>& spirvBytes);
        void Destroy();

        VkShaderModule GetModule() const { return m_Module; }
        bool IsValid() const { return m_Module != VK_NULL_HANDLE; }

    private:
        void MoveFrom(VK_ShaderModule& other) noexcept {
            m_Device = other.m_Device;
            m_Module = other.m_Module;
            other.m_Device = VK_NULL_HANDLE;
            other.m_Module = VK_NULL_HANDLE;
        }

        VkDevice m_Device = VK_NULL_HANDLE;
        VkShaderModule m_Module = VK_NULL_HANDLE;
    };

    /** Vulkan pipeline + layout wrapper; derives from RHI_Shaders for SetParameter / ApplyParameters. */
    class NV_API VK_Shaders final : public RHI::RHI_Shaders {
    public:
        VK_Shaders() = default;
        ~VK_Shaders() override = default;

        /** Set pipeline and layout (owned by swapchain/renderer). Call after pipeline creation. */
        void SetPipeline(VkPipeline pipeline, VkPipelineLayout layout);

        /**
         * Engine uniform buffers (frame, MVP, material, instances) + the descriptor sets to bind.
         * `descriptorSets` lists every descriptor set allocated for the pipeline as (set index, set)
         * pairs; the set indices and bindings come from Slang reflection.
         */
        void SetSceneBuffers(VkDevice device,
            VkBuffer bufFrameUniforms, VkDeviceMemory bufFrameUniformsMemory,
            VkBuffer bufMvp, VkDeviceMemory bufMvpMemory, VkDeviceSize mvpDynamicStride, VkDeviceSize mvpBufferSize,
            VkBuffer bufMaterials, VkDeviceMemory bufMaterialsMemory, VkDeviceSize materialDynamicStride, VkDeviceSize materialBufferSize,
            VkBuffer bufInstances, VkDeviceMemory bufInstancesMemory, VkDeviceSize bufInstancesSize,
            VkDeviceSize* mvpDynamicOffset, VkDeviceSize* materialDynamicOffset,
            const std::vector<std::pair<uint32_t, VkDescriptorSet>>& descriptorSets);

        void Bind(void* apiContext = nullptr) override;
        void ApplyParameters(void* apiContext = nullptr) override;
        void* GetNativeHandle() const override;

        /** Reset per-draw dynamic UBO offsets at frame start. */
        void ResetDynamicUBOs();

        /**
         * Update a single (set, binding) in one of the pipeline's descriptor sets.
         * This is a low-level helper used by RHI_ShaderResourceSet; the (set, binding) is whatever
         * Slang reflection assigned to the resource.
         */
        void WriteDescriptor(uint32_t set, uint32_t binding, VkDescriptorType type,
            const VkDescriptorBufferInfo* bufferInfo,
            const VkDescriptorImageInfo* imageInfo);

        VkPipeline GetPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        bool IsValid() const { return m_Pipeline != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE; }

    private:
        bool ApplyResourceBinding(const RHI::RHI_BindingInfo& info, const RHI::RHI_ResourceBinding& value) override;

        /** Map a host-visible region and copy `size` bytes from `src` into it. */
        void MapAndCopy(VkDeviceMemory memory, VkDeviceSize offset, VkDeviceSize size, const void* src);
        /**
         * Copy `src` into a dynamic uniform buffer at the current per-draw cursor, advance the cursor
         * by `stride`, and return the offset used for this draw (0 when the buffer isn't dynamic).
         */
        VkDeviceSize UploadDynamic(VkDeviceMemory memory, VkDeviceSize size, const void* src,
            VkDeviceSize stride, VkDeviceSize& offsetCursor, VkDeviceSize bufferSize);
        /** Bind all descriptor sets, supplying dynamic offsets in reflection (set, binding) order. */
        void BindDescriptorSets(VkCommandBuffer cmd, VkDeviceSize mvpDynamicOffset, VkDeviceSize materialDynamicOffset);
        /** Resolve the descriptor set allocated for a given reflection set index (VK_NULL_HANDLE if none). */
        VkDescriptorSet FindDescriptorSet(uint32_t set) const;

        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

        VkDevice m_Device = VK_NULL_HANDLE;
        VkBuffer m_BufFrameUniforms = VK_NULL_HANDLE;
        VkDeviceMemory m_BufFrameUniformsMemory = VK_NULL_HANDLE;
        VkBuffer m_BufMvp = VK_NULL_HANDLE;
        VkDeviceMemory m_BufMvpMemory = VK_NULL_HANDLE;
        VkDeviceSize m_MvpDynamicStride = 0;
        VkDeviceSize m_MvpBufferSize = 0;
        VkDeviceSize* m_MvpDynamicOffset = nullptr;
        VkBuffer m_BufMaterials = VK_NULL_HANDLE;
        VkDeviceMemory m_BufMaterialsMemory = VK_NULL_HANDLE;
        VkDeviceSize m_MaterialDynamicStride = 0;
        VkDeviceSize m_MaterialBufferSize = 0;
        VkDeviceSize* m_MaterialDynamicOffset = nullptr;
        VkBuffer m_BufInstances = VK_NULL_HANDLE;
        VkDeviceMemory m_BufInstancesMemory = VK_NULL_HANDLE;
        VkDeviceSize m_BufInstancesSize = 0;
        // All descriptor sets allocated for this pipeline, as (reflection set index, set) pairs.
        std::vector<std::pair<uint32_t, VkDescriptorSet>> m_DescriptorSets;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_SHADERS_H