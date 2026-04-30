#ifndef RHI_SHADER_REFLECTION_H
#define RHI_SHADER_REFLECTION_H

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    // Forward-declared to avoid include cycles (see RHI_ShaderCompiler.h).
    enum class RHI_ShaderStage : int;

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

    enum class RHI_ShaderStageMask : uint32_t {
        None     = 0,
        Vertex   = 1u << 0,
        Fragment = 1u << 1,
        Geometry = 1u << 2,
        TessCtrl = 1u << 3,
        TessEval = 1u << 4,
        Compute  = 1u << 5,
        RayGen   = 1u << 6,
        RayMiss  = 1u << 7,
        RayCHit  = 1u << 8,
        RayAHit  = 1u << 9,
        RayISect = 1u << 10,
        RayCall  = 1u << 11,
    };

    inline constexpr RHI_ShaderStageMask operator|(RHI_ShaderStageMask a, RHI_ShaderStageMask b) {
        return static_cast<RHI_ShaderStageMask>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline constexpr RHI_ShaderStageMask& operator|=(RHI_ShaderStageMask& a, RHI_ShaderStageMask b) {
        a = a | b;
        return a;
    }

    struct NV_API RHI_BindingKey {
        uint32_t set = 0;
        uint32_t binding = 0;

        friend bool operator==(const RHI_BindingKey& a, const RHI_BindingKey& b) = default;
    };

    struct NV_API RHI_BindingInfo {
        RHI_BindingKey key{};
        RHI_ResourceKind kind = RHI_ResourceKind::Unknown;
        uint32_t arrayCount = 1;              // 1 for non-arrays; 0 can mean unknown/runtime sized
        size_t byteSizeIfKnown = 0;           // for constant buffers / structured buffers when reflectable
        std::string fullName;                 // e.g. "nova.frame" or "user.albedo"
        RHI_ShaderStageMask stages = RHI_ShaderStageMask::None;
        bool isDynamicUniformBuffer = false;  // Vulkan: descriptorType = UNIFORM_BUFFER_DYNAMIC
    };

    struct NV_API RHI_DescriptorSetLayoutInfo {
        uint32_t set = 0;
        std::vector<RHI_BindingInfo> bindings; // unique by binding index
    };

    struct NV_API RHI_PushConstantInfo {
        size_t sizeBytes = 0;
        RHI_ShaderStageMask stages = RHI_ShaderStageMask::None;
    };

    struct NV_API RHI_ProgramReflection {
        std::vector<RHI_DescriptorSetLayoutInfo> sets; // sorted by set index
        std::optional<RHI_PushConstantInfo> pushConstants;

        // Maps a stable reflection name to a binding key.
        // Convention: we use dot-separated paths, e.g. "nova.frame", "user.myCBuffer".
        std::unordered_map<std::string, RHI_BindingKey> nameToBinding;

        const RHI_DescriptorSetLayoutInfo* FindSet(uint32_t setIndex) const;
        const RHI_BindingInfo* FindBinding(uint32_t setIndex, uint32_t binding) const;
    };

    /** Merge multiple stage reflections (e.g. VS+FS) into a single program reflection. */
    NV_API RHI_ProgramReflection MergeProgramReflections(const std::vector<RHI_ProgramReflection>& perStage);

    /** Map `RHI_ShaderStage` (single stage) to stage mask. */
    NV_API RHI_ShaderStageMask ToStageMask(RHI_ShaderStage stage);

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_REFLECTION_H