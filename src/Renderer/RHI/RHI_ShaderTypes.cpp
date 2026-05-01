#include "Renderer/RHI/RHI_ShaderTypes.h"

namespace Nova::Core::Renderer::RHI {

    RHI_ShaderStageMask ToStageMask(RHI_ShaderStage stage) {
        switch (stage) {
            case RHI_ShaderStage::Vertex:          return RHI_ShaderStageMask::Vertex;
            case RHI_ShaderStage::Fragment:        return RHI_ShaderStageMask::Fragment;
            case RHI_ShaderStage::Geometry:        return RHI_ShaderStageMask::Geometry;
            case RHI_ShaderStage::TessControl:     return RHI_ShaderStageMask::TessCtrl;
            case RHI_ShaderStage::TessEvaluation:  return RHI_ShaderStageMask::TessEval;
            case RHI_ShaderStage::Compute:         return RHI_ShaderStageMask::Compute;
            case RHI_ShaderStage::RayGen:          return RHI_ShaderStageMask::RayGen;
            case RHI_ShaderStage::RayMiss:         return RHI_ShaderStageMask::RayMiss;
            case RHI_ShaderStage::RayClosestHit:   return RHI_ShaderStageMask::RayCHit;
            case RHI_ShaderStage::RayAnyHit:       return RHI_ShaderStageMask::RayAHit;
            case RHI_ShaderStage::RayIntersection: return RHI_ShaderStageMask::RayISect;
            case RHI_ShaderStage::RayCallable:     return RHI_ShaderStageMask::RayCall;
            default:                               return RHI_ShaderStageMask::None;
        }
    }

    const char* ShaderStageToString(RHI_ShaderStage stage) {
        switch (stage) {
            case RHI_ShaderStage::Vertex:          return "Vertex";
            case RHI_ShaderStage::Fragment:        return "Fragment";
            case RHI_ShaderStage::Geometry:        return "Geometry";
            case RHI_ShaderStage::TessControl:     return "Tessellation Control";
            case RHI_ShaderStage::TessEvaluation:  return "Tessellation Evaluation";
            case RHI_ShaderStage::Compute:         return "Compute";
            case RHI_ShaderStage::RayGen:          return "Ray Generation";
            case RHI_ShaderStage::RayMiss:         return "Ray Miss";
            case RHI_ShaderStage::RayClosestHit:   return "Ray Closest Hit";
            case RHI_ShaderStage::RayAnyHit:       return "Ray Any Hit";
            case RHI_ShaderStage::RayIntersection: return "Ray Intersection";
            case RHI_ShaderStage::RayCallable:     return "Ray Callable";
            default:                               return "Unknown";
        }
    }

} // namespace Nova::Core::Renderer::RHI