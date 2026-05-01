// Shared shader types used across compiler, reflection, and backends.
#ifndef RHI_SHADER_TYPES_H
#define RHI_SHADER_TYPES_H

#include <cstdint>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    enum class RHI_ShaderStage : uint8_t {
        Unknown = 0,
        Vertex,
        Fragment,
        Geometry,
        TessControl,
        TessEvaluation,
        Compute,
        RayGen,
        RayMiss,
        RayClosestHit,
        RayAnyHit,
        RayIntersection,
        RayCallable
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

    /** Map `RHI_ShaderStage` (single stage) to stage mask. */
    NV_API RHI_ShaderStageMask ToStageMask(RHI_ShaderStage stage);

    NV_API const char* ShaderStageToString(RHI_ShaderStage stage);

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_TYPES_H
