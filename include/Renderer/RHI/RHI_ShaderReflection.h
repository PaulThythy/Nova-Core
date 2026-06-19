#ifndef RHI_SHADER_REFLECTION_H
#define RHI_SHADER_REFLECTION_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Api.h"
#include "Renderer/RHI/RHI_ShaderTypes.h"

namespace Nova::Core::Renderer::RHI {

    enum class RHI_ResourceKind : uint8_t {
        Unknown = 0,
        ConstantBuffer,
        StorageBuffer,
        Texture,
        Sampler,
        CombinedTextureSampler,
        RWTexture,
        RWBuffer,
        AccelStruct,
    };

    struct NV_API RHI_BindingKey {
        uint32_t m_Set = 0;
        uint32_t m_Binding = 0;

        friend bool operator==(const RHI_BindingKey& a, const RHI_BindingKey& b) = default;
    };

    struct NV_API RHI_BindingInfo {
        RHI_BindingKey m_Key{};
        RHI_ResourceKind m_Kind = RHI_ResourceKind::Unknown;
        uint32_t m_ArrayCount = 1;              // 1 for non-arrays; 0 can mean unknown/runtime sized
        size_t m_ByteSizeIfKnown = 0;           // for constant buffers / structured buffers when reflectable
        std::string m_FullName;                 // e.g. "nova.frame" or "user.albedo"
        RHI_ShaderStageMask m_Stages = RHI_ShaderStageMask::None;
        bool m_IsDynamicUniformBuffer = false;  // Vulkan: descriptorType = UNIFORM_BUFFER_DYNAMIC
    };

    struct NV_API RHI_DescriptorSetLayoutInfo {
        uint32_t m_Set = 0;
        std::vector<RHI_BindingInfo> m_Bindings; // unique by binding index
    };

    struct NV_API RHI_PushConstantInfo {
        size_t m_SizeBytes = 0;
        RHI_ShaderStageMask m_Stages = RHI_ShaderStageMask::None;
    };

    struct NV_API RHI_ProgramReflection {
        std::vector<RHI_DescriptorSetLayoutInfo> m_Sets; // sorted by set index
        std::optional<RHI_PushConstantInfo> m_PushConstants;

        // Maps a stable reflection name to a binding key.
        // Convention: we use dot-separated paths, e.g. "nova.frame", "user.myCBuffer".
        std::unordered_map<std::string, RHI_BindingKey> m_NameToBinding;

        const RHI_DescriptorSetLayoutInfo* FindSet(uint32_t setIndex) const;
        const RHI_BindingInfo* FindBinding(uint32_t setIndex, uint32_t binding) const;

        // Resolve a reflection name (e.g. "nova.frame") to its (set, binding) as assigned by Slang.
        const RHI_BindingKey* FindBindingKeyByName(const std::string& name) const;
        // Convenience: resolve a reflection name directly to its binding info.
        const RHI_BindingInfo* FindBindingByName(const std::string& name) const;
    };

    /** Merge multiple stage reflections (e.g. VS+FS) into a single program reflection. */
    NV_API RHI_ProgramReflection MergeProgramReflections(const std::vector<RHI_ProgramReflection>& perStage);

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_REFLECTION_H
