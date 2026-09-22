#ifndef VK_PIPELINECACHE_H
#define VK_PIPELINECACHE_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <utility>
#include <vector>

#include <volk.h>

#include "Api.h"
#include "Renderer/RHI/RHI_RenderGraph.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Renderer/Backends/Vulkan/VK_Shaders.h"

namespace Nova::Core::Renderer::Backends::Vulkan {

    class VK_Renderer;

    /** Vulkan pipeline cache with hot-reload support, indexed by RHI_ShaderHandle. */
    class NV_API VK_PipelineCache {
    public:
        static constexpr uint32_t MAX_MODEL_DRAWS = 4096;

        VK_PipelineCache() = default;
        ~VK_PipelineCache() { Destroy(); }

        bool Create(VK_Renderer& renderer, const std::vector<RHI::RHI_ShaderDesc>& shaders, VkFormat colorFormat, VkFormat depthFormat);
        void Destroy();

        /** Resolve a declared shader, building its pipeline on first use. */
        RHI::IShaders* Get(RHI::RHI_ShaderHandle handle);
        bool ReloadChangedShaders();

        void ResetFrameDynamicUBOs();

        VkDescriptorPool GetDescriptorPool() const { return m_DescriptorPool; }
        const RHI::RHI_EngineParameterBlock& GetEngine() const { return m_Engine; }
        bool BindEngineShadowMaps(VkImageView arrayView, VkSampler comparisonSampler);

        friend class VK_RenderGraph;

    private:
        struct PipelineEntry {
            RHI::RHI_ShaderDesc desc{};
            VkPipeline pipeline = VK_NULL_HANDLE;
            VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
            std::vector<std::pair<uint32_t, VkDescriptorSetLayout>> setLayouts;
            std::vector<std::pair<uint32_t, VkDescriptorSet>> descriptorSets;
            std::unique_ptr<VK_Shaders> shader;
            std::filesystem::file_time_type vertWriteTime{};
            std::filesystem::file_time_type fragWriteTime{};
        };

        bool BuildPipeline(PipelineEntry& entry);
        void DestroyEntry(PipelineEntry& entry);
        bool CreateEngineBuffers();
        void DestroyEngineBuffers();
        bool CreateDescriptorPool();
        void WriteEngineBuffersToEntry(PipelineEntry& entry);
        void WriteShadowMapsToEntry(PipelineEntry& entry);

        VK_Renderer* m_Renderer = nullptr;
        VkPipelineCache m_VkPipelineCache = VK_NULL_HANDLE;
        VkDescriptorPool m_DescriptorPool = VK_NULL_HANDLE;
        VkFormat m_ColorFormat = VK_FORMAT_UNDEFINED;
        VkFormat m_DepthFormat = VK_FORMAT_D32_SFLOAT;

        RHI::RHI_EngineParameterBlock m_Engine{};
        VkImageView m_ShadowMapsView = VK_NULL_HANDLE;
        VkSampler m_ShadowSampler = VK_NULL_HANDLE;

        std::vector<PipelineEntry> m_Entries;
    };

} // namespace Nova::Core::Renderer::Backends::Vulkan

#endif // VK_PIPELINECACHE_H