#ifndef RHI_SHADERREFLECTION_H
#define RHI_SHADERREFLECTION_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Api.h"
#include "Renderer/RHI/RHI_GpuBuffer.h"
#include "Renderer/RHI/RHI_ShaderTypes.h"

namespace Nova::Core::Renderer::RHI {
    class IRenderer;
}

namespace slang {
    class IComponentType;
}

namespace Nova::Core::Renderer::RHI {

    // Mirrors the resource kinds available in Slang shaders (see NovaUniforms.slang):
    // ConstantBuffer<T>, StructuredBuffer<T>, RWStructuredBuffer<T>, Texture*, Sampler*, etc.
    enum class RHI_ResourceKind : uint8_t {
        Unknown = 0,
        ConstantBuffer,     // ConstantBuffer<T>
        StructuredBuffer,   // StructuredBuffer<T>
        Texture,
        Sampler,
        CombinedTextureSampler,
        RWTexture,
        RWStructuredBuffer, // RWStructuredBuffer<T>
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
        std::string m_FullName;                 // e.g. "nova.scene" or "user.albedo"
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
        // Convention: we use dot-separated paths, e.g. "nova.scene", "user.myCBuffer".
        std::unordered_map<std::string, RHI_BindingKey> m_NameToBinding;

        const RHI_DescriptorSetLayoutInfo* FindSet(uint32_t setIndex) const;
        const RHI_BindingInfo* FindBinding(uint32_t setIndex, uint32_t binding) const;

        // Resolve a reflection name (e.g. "nova.scene") to its (set, binding) as assigned by Slang.
        const RHI_BindingKey* FindBindingKeyByName(const std::string& name) const;
        // Convenience: resolve a reflection name directly to its binding info.
        const RHI_BindingInfo* FindBindingByName(const std::string& name) const;
    };

    /** Merge multiple stage reflections (e.g. VS+FS) into a single program reflection. */
    NV_API RHI_ProgramReflection MergeProgramReflections(const std::vector<RHI_ProgramReflection>& perStage);

    /** Extract backend-agnostic reflection from a linked Slang component. */
    NV_API void ExtractProgramReflection(slang::IComponentType* linked, RHI_ShaderStage stage, RHI_ProgramReflection& out);

    /** Append a truncated Slang reflection JSON excerpt to `log` (diagnostics). */
    NV_API void AppendSlangReflectionJsonExcerpt(std::string& log, slang::IComponentType* linked, size_t maxBytes = 4096);

    struct NV_API RHI_BufferBinding {
        uint64_t m_Handle = 0;
        uint64_t m_Offset = 0;
        uint64_t m_Range = 0;
    };

    struct NV_API RHI_TextureBinding {
        uint64_t m_TextureHandle = 0;
        uint32_t m_ImageLayout = 0;
    };

    struct NV_API RHI_SamplerBinding {
        uint64_t m_SamplerHandle = 0;
    };

    using RHI_ResourceBinding = std::variant<RHI_BufferBinding, RHI_TextureBinding, RHI_SamplerBinding>;

    /**
     * Backend-agnostic shader resource binder: set resources by reflection name.
     *
     * Names come from `RHI_ProgramReflection::m_NameToBinding`, e.g. "nova.scene" or "user.albedo".
     */
    class NV_API RHI_ShaderResourceSet {
    public:
        explicit RHI_ShaderResourceSet(const RHI_ProgramReflection* reflection = nullptr)
            : m_Reflection(reflection) {}

        void SetReflection(const RHI_ProgramReflection* reflection) { m_Reflection = reflection; }

        bool SetBuffer(const std::string& name, uint64_t handle, uint64_t offset = 0, uint64_t range = 0);
        bool SetBuffer(const std::string& name, RHI_GpuBufferHandle handle, const IRenderer& renderer, uint32_t elementIndex = 0);
        bool SetTexture(const std::string& name, uint64_t textureHandle, uint32_t imageLayout = 0);
        bool SetSampler(const std::string& name, uint64_t samplerHandle);

        bool Apply(void* shader) const;

    private:
        const RHI_BindingInfo* FindBindingInfo(const std::string& name) const;

        const RHI_ProgramReflection* m_Reflection = nullptr;
        std::unordered_map<std::string, RHI_ResourceBinding> m_Bindings;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADERREFLECTION_H