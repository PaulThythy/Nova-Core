#ifndef RHI_SHADER_UNIFORMS_H
#define RHI_SHADER_UNIFORMS_H

/**
 * CPU mirrors of engine shader types (NovaUniforms.slang).
 *
 * Shaders declare `ParameterBlock<NovaEngine> nova;` with no [[vk::binding]]: Slang
 * reflection assigns the descriptor set/space and the per-resource bindings. The engine
 * never hardcodes set/binding indices; it resolves them at runtime from the reflection
 * (RHI_ProgramReflection) using the stable reflection names below.
 */

#include <glm/glm.hpp>
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "Api.h"

namespace Nova::Core::Renderer::RHI {

    // Stable reflection names for the engine resources declared in NovaUniforms.slang
    // (`ParameterBlock<NovaEngine> nova;`). The descriptor set and binding for each are
    // assigned by Slang reflection and looked up by these names at runtime.
    namespace EngineResourceName {
        inline constexpr const char* Frame     = "nova.frame";
        inline constexpr const char* Mvp       = "nova.mvp";
        inline constexpr const char* Instances = "nova.instances";
        inline constexpr const char* Material  = "nova.material";
    }

    struct NV_API FrameUniforms {
        alignas(16) glm::vec3 m_IResolution{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     m_PadAfterRes{ 0.0f };
        alignas(4)  float     m_ITime{ 0.0f };
        alignas(4)  float     m_ITimeDelta{ 0.0f };
        alignas(4)  float     m_IFrameRate{ 0.0f };
        alignas(4)  int       m_IFrame{ 0 };
        alignas(4)  int       m_UUseInstancing{ 0 };
        alignas(8)  glm::ivec2 m_Offset0{ 0, 0 };
        alignas(16) glm::vec3 m_UCameraPos{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     m_PadAfterCameraPos{ 0.0f };
        alignas(16) glm::vec4 m_IMouse{ 0.0f, 0.0f, 0.0f, 0.0f };
        alignas(16) glm::vec4 m_IDate{ 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct NV_API MVP {
        alignas(16) glm::mat4 m_Model{ 1.0f };
        alignas(16) glm::mat4 m_View{ 1.0f };
        alignas(16) glm::mat4 m_Proj{ 1.0f };
        alignas(16) glm::mat4 m_ViewProj{ 1.0f };
        alignas(16) glm::mat4 m_InvViewProj{ 1.0f };
    };

    struct NV_API Instance {
        alignas(16) glm::mat4 m_Model{ 1.0f };
        alignas(16) glm::vec4 m_Color{ 1.0f, 1.0f, 1.0f, 1.0f };
    };

    struct NV_API Material {
        alignas(4)  float       m_Base{ 0.8f };
        alignas(16) glm::vec3   m_BaseColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_DiffuseRoughness{ 0.0f };
        alignas(4)  float       m_Metalness{ 0.0f };
        alignas(16) glm::vec3   m_MetalColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_Specular{ 1.0f };
        alignas(16) glm::vec3   m_SpecularColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_SpecularRoughness{ 0.2f };
        alignas(4)  float       m_SpecularIOR{ 1.5f };
        alignas(4)  float       m_SpecularAnisotropy{ 0.0f };
        alignas(4)  float       m_SpecularRotation{ 0.0f };
        alignas(4)  float       m_Transmission{ 0.0f };
        alignas(16) glm::vec3   m_TransmissionColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_Subsurface{ 0.0f };
        alignas(16) glm::vec3   m_SubsurfaceColor{ 1.0f, 1.0f, 1.0f };
        alignas(16) glm::vec3   m_SubsurfaceRadius{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_SubsurfaceScale{ 1.0f };
        alignas(4)  float       m_SubsurfaceAnisotropy{ 0.0f };
        alignas(4)  float       m_Sheen{ 0.0f };
        alignas(16) glm::vec3   m_SheenColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_SheenRoughness{ 0.3f };
        alignas(4)  float       m_Coat{ 0.0f };
        alignas(16) glm::vec3   m_CoatColor{ 1.0f, 1.0f, 1.0f };
        alignas(4)  float       m_CoatRoughness{ 0.1f };
        alignas(4)  float       m_CoatAnisotropy{ 0.0f };
        alignas(4)  float       m_CoatRotation{ 0.0f };
        alignas(4)  float       m_CoatIOR{ 1.5f };
        alignas(4)  float       m_CoatAffectColor{ 0.0f };
        alignas(4)  float       m_CoatAffectRoughness{ 0.0f };
        alignas(4)  float       m_Emission{ 0.0f };
        alignas(16) glm::vec3   m_EmissionColor{ 1.0f, 1.0f, 1.0f };
        alignas(16) glm::vec3   m_Opacity{ 1.0f, 1.0f, 1.0f };
        alignas(4)  int         m_ThinWalled{ 0 };
        alignas(4)  int         m_IsOpaque{ 1 };
        alignas(8)  glm::uvec2  m_PadCbufferAlign{ 0u, 0u };
    };

    // name -> byte offset maps describing how SetParameter() values are packed into the engine
    // CPU mirror structs above. Padding fields are intentionally absent: they are never set.
    inline const std::unordered_map<std::string, size_t>& GetMaterialLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "base",                 offsetof(Material, m_Base) },
            { "baseColor",            offsetof(Material, m_BaseColor) },
            { "diffuseRoughness",     offsetof(Material, m_DiffuseRoughness) },
            { "metalness",            offsetof(Material, m_Metalness) },
            { "metalColor",           offsetof(Material, m_MetalColor) },
            { "specular",             offsetof(Material, m_Specular) },
            { "specularColor",        offsetof(Material, m_SpecularColor) },
            { "specularRoughness",    offsetof(Material, m_SpecularRoughness) },
            { "specularIOR",          offsetof(Material, m_SpecularIOR) },
            { "specularAnisotropy",   offsetof(Material, m_SpecularAnisotropy) },
            { "specularRotation",     offsetof(Material, m_SpecularRotation) },
            { "transmission",         offsetof(Material, m_Transmission) },
            { "transmissionColor",    offsetof(Material, m_TransmissionColor) },
            { "subsurface",           offsetof(Material, m_Subsurface) },
            { "subsurfaceColor",      offsetof(Material, m_SubsurfaceColor) },
            { "subsurfaceRadius",     offsetof(Material, m_SubsurfaceRadius) },
            { "subsurfaceScale",      offsetof(Material, m_SubsurfaceScale) },
            { "subsurfaceAnisotropy", offsetof(Material, m_SubsurfaceAnisotropy) },
            { "sheen",                offsetof(Material, m_Sheen) },
            { "sheenColor",           offsetof(Material, m_SheenColor) },
            { "sheenRoughness",       offsetof(Material, m_SheenRoughness) },
            { "coat",                 offsetof(Material, m_Coat) },
            { "coatColor",            offsetof(Material, m_CoatColor) },
            { "coatRoughness",        offsetof(Material, m_CoatRoughness) },
            { "coatAnisotropy",       offsetof(Material, m_CoatAnisotropy) },
            { "coatRotation",         offsetof(Material, m_CoatRotation) },
            { "coatIOR",              offsetof(Material, m_CoatIOR) },
            { "coatAffectColor",      offsetof(Material, m_CoatAffectColor) },
            { "coatAffectRoughness",  offsetof(Material, m_CoatAffectRoughness) },
            { "emission",             offsetof(Material, m_Emission) },
            { "emissionColor",        offsetof(Material, m_EmissionColor) },
            { "opacity",              offsetof(Material, m_Opacity) },
            { "thinWalled",           offsetof(Material, m_ThinWalled) },
            { "isOpaque",             offsetof(Material, m_IsOpaque) },
        };
        return kLayout;
    }

    inline const std::unordered_map<std::string, size_t>& GetFrameLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "iResolution",     offsetof(FrameUniforms, m_IResolution) },
            { "iTime",           offsetof(FrameUniforms, m_ITime) },
            { "iTimeDelta",      offsetof(FrameUniforms, m_ITimeDelta) },
            { "iFrameRate",      offsetof(FrameUniforms, m_IFrameRate) },
            { "iFrame",          offsetof(FrameUniforms, m_IFrame) },
            { "u_UseInstancing", offsetof(FrameUniforms, m_UUseInstancing) },
            { "u_CameraPos",     offsetof(FrameUniforms, m_UCameraPos) },
            { "iMouse",          offsetof(FrameUniforms, m_IMouse) },
            { "iDate",           offsetof(FrameUniforms, m_IDate) },
        };
        return kLayout;
    }

    inline const std::unordered_map<std::string, size_t>& GetMvpLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "model",       offsetof(MVP, m_Model) },
            { "view",        offsetof(MVP, m_View) },
            { "proj",        offsetof(MVP, m_Proj) },
            { "viewProj",    offsetof(MVP, m_ViewProj) },
            { "invViewProj", offsetof(MVP, m_InvViewProj) },
        };
        return kLayout;
    }

    inline const std::unordered_map<std::string, size_t>& GetInstancesLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "model", offsetof(Instance, m_Model) },
            { "color", offsetof(Instance, m_Color) },
        };
        return kLayout;
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_UNIFORMS_H
