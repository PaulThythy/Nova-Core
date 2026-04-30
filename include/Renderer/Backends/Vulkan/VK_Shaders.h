#ifndef VK_SHADERS_H
#define VK_SHADERS_H

#include <vulkan/vulkan.h>
#include <vector>
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

        bool Create(VkDevice device, const std::vector<uint32_t>& spirv);
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

    /** Vulkan pipeline + layout wrapper; derives from RHI_Shaders for SetParameter / ApplyParameters (push constants). */
    class NV_API VK_Shaders final : public RHI::RHI_Shaders {
    public:
        VK_Shaders() = default;
        ~VK_Shaders() override = default;

        /** Set pipeline and layout (owned by swapchain/renderer). Call after pipeline creation. */
        void SetPipeline(VkPipeline pipeline, VkPipelineLayout layout);

        /** Set scene buffers (frame uniforms / MVP / materials / instances) and descriptor set for ApplyParameters. */
        void SetSceneBuffers(VkDevice device,
            VkBuffer bufFrameUniforms, VkDeviceMemory bufFrameUniformsMemory,
            VkBuffer bufMvp, VkDeviceMemory bufMvpMemory, VkDeviceSize mvpDynamicStride,
            VkBuffer bufMaterials, VkDeviceMemory bufMaterialsMemory, VkDeviceSize materialDynamicStride,
            VkBuffer bufInstances, VkDeviceMemory bufInstancesMemory, VkDeviceSize bufInstancesSize,
            VkDescriptorSet sceneDescriptorSet,
            VkDescriptorSet userDescriptorSet = VK_NULL_HANDLE);

        void Bind(void* apiContext = nullptr) override;
        void ApplyParameters(void* apiContext = nullptr) override;
        void SetInstanceData(const std::vector<RHI::Instance>& instances) override;
        void* GetNativeHandle() const override;

        /** Reset per-draw dynamic UBO offsets at frame start. */
        void ResetDynamicUBOs();

        /**
         * Update a single binding in the user descriptor set (set 1).
         * This is a low-level helper used by RHI_ShaderResourceSet.
         */
        void WriteUserDescriptor(uint32_t binding, VkDescriptorType type,
            const VkDescriptorBufferInfo* bufferInfo,
            const VkDescriptorImageInfo* imageInfo);

        VkPipeline GetPipeline() const { return m_Pipeline; }
        VkPipelineLayout GetPipelineLayout() const { return m_PipelineLayout; }
        bool IsValid() const { return m_Pipeline != VK_NULL_HANDLE && m_PipelineLayout != VK_NULL_HANDLE; }

    private:
        VkPipeline m_Pipeline = VK_NULL_HANDLE;
        VkPipelineLayout m_PipelineLayout = VK_NULL_HANDLE;

        VkDevice m_Device = VK_NULL_HANDLE;
        VkBuffer m_BufFrameUniforms = VK_NULL_HANDLE;
        VkDeviceMemory m_BufFrameUniformsMemory = VK_NULL_HANDLE;
        VkBuffer m_BufMvp = VK_NULL_HANDLE;
        VkDeviceMemory m_BufMvpMemory = VK_NULL_HANDLE;
        VkDeviceSize m_MvpDynamicStride = 0;
        VkDeviceSize m_MvpDynamicOffset = 0;
        VkBuffer m_BufMaterials = VK_NULL_HANDLE;
        VkDeviceMemory m_BufMaterialsMemory = VK_NULL_HANDLE;
        VkDeviceSize m_MaterialDynamicStride = 0;
        VkDeviceSize m_MaterialDynamicOffset = 0;
        VkBuffer m_BufInstances = VK_NULL_HANDLE;
        VkDeviceMemory m_BufInstancesMemory = VK_NULL_HANDLE;
        VkDeviceSize m_BufInstancesSize = 0;
        
        VkDescriptorSet m_SceneDescriptorSet = VK_NULL_HANDLE;
        VkDescriptorSet m_UserDescriptorSet = VK_NULL_HANDLE;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_SHADERS_H