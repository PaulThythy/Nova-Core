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
#include "Renderer/RHI/RHI_GpuBuffer.h"

namespace Nova::Core::Renderer::RHI {

    static constexpr uint32_t MAX_LIGHTS = 16;
    static constexpr uint32_t MAX_SHADOW_MAPS = 4;
    static constexpr uint32_t SHADOW_MAP_RESOLUTION = 2048;
    /** Half-extent of the directional light ortho frustum (world units). Smaller = sharper shadows. */
    static constexpr float SHADOW_DIR_ORTHO_HALF_EXTENT = 10.0f;

    // Stable reflection names for the engine resources declared in NovaUniforms.slang
    // (`ParameterBlock<NovaEngine> nova;`). The descriptor set and binding for each are
    // assigned by Slang reflection and looked up by these names at runtime.
    // `frame` is a push constant (not a descriptor) — see FrameUniforms below.
    namespace EngineResourceName {
        inline constexpr const char* Scene         = "nova.scene";
        inline constexpr const char* Mvp           = "nova.mvp";
        inline constexpr const char* Material      = "nova.material";
        inline constexpr const char* Lights        = "nova.lights";
        inline constexpr const char* ShadowMaps    = "nova.shadowMaps";
        inline constexpr const char* ShadowSampler = "nova.shadowSampler";
    }

    /**
     * Per-frame globals (time, resolution, inputs). Vulkan push constants (std430 layout).
     * Uploaded via vkCmdPushConstants every ApplyParameters when the shader reflects them.
     * Must stay within the Vulkan minimum maxPushConstantsSize (128 bytes).
     */
    struct NV_API FrameUniforms {
        alignas(16) glm::vec3 m_Resolution{ 0.0f, 0.0f, 0.0f };
        alignas(4)  float     m_PadAfterRes{ 0.0f };
        alignas(4)  float     m_Time{ 0.0f };
        alignas(4)  float     m_TimeDelta{ 0.0f };
        alignas(4)  float     m_FrameRate{ 0.0f };
        alignas(4)  int       m_Frame{ 0 };
        alignas(4)  int       m_PadAfterFrame{ 0 };
        alignas(16) glm::vec4 m_Mouse{ 0.0f, 0.0f, 0.0f, 0.0f };
        alignas(16) glm::vec4 m_Date{ 0.0f, 0.0f, 0.0f, 0.0f };
    };
    static_assert(sizeof(FrameUniforms) <= 128, "FrameUniforms exceeds Vulkan min maxPushConstantsSize");

    /** Scene-level data (camera, light count). Set by the app via shader SetParameter. */
    struct NV_API SceneUniforms {
        alignas(16) glm::vec3 m_CameraPos{ 0.0f, 0.0f, 0.0f };
        alignas(4)  int       m_LightCount{ 0 };
        alignas(4)  float     m_PadAfterLightCount{ 0.0f };
    };

    struct NV_API MVP {
        alignas(16) glm::mat4 m_Model{ 1.0f };
        alignas(16) glm::mat4 m_View{ 1.0f };
        alignas(16) glm::mat4 m_Proj{ 1.0f };
        alignas(16) glm::mat4 m_ViewProj{ 1.0f };
        alignas(16) glm::mat4 m_InvViewProj{ 1.0f };
    };

    /** GPU light element for `StructuredBuffer<LightGPU> nova.lights` (std430). */
    struct NV_API LightGPU {
        alignas(4)  int       m_Type{ 0 };           // 0=Directional, 1=Point, 2=Spot
        alignas(4)  int       m_CastShadow{ 0 };
        alignas(4)  int       m_ShadowMapIndex{ -1 };
        alignas(4)  float     m_Intensity{ 1.0f };
        alignas(16) glm::vec3 m_Color{ 1.0f };
        alignas(4)  float     m_Range{ 10.0f };
        alignas(16) glm::vec3 m_Direction{ 0.0f, -1.0f, 0.0f };
        alignas(4)  float     m_InnerConeCos{ 0.0f };
        alignas(16) glm::vec3 m_Position{ 0.0f };
        alignas(4)  float     m_OuterConeCos{ 0.0f };
        alignas(4)  float     m_ShadowBiasConstant{ 0.5f };
        alignas(4)  float     m_ShadowBiasSlope{ 1.0f };
        alignas(4)  float     m_ShadowNormalBias{ 0.012f };
        alignas(4)  float     m_PadBias{ 0.0f };
        alignas(16) glm::mat4 m_LightViewProj{ 1.0f };
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

    /**
     * C++ mirror of `ParameterBlock<NovaEngine> nova;` (NovaUniforms.slang): one GPU buffer handle
     * per buffer field of `NovaEngine`. `FrameUniforms` is a push constant (not a descriptor
     * buffer). Shadow map texture/sampler are render-graph resources bound by reflection name
     * after texture creation.
     *
     * `m_Scene` holds a single value (one region per frame-in-flight). `m_Mvp` and `m_Material`
     * are arrays (one element per draw call this frame) — see MAX_MODEL_DRAWS in VK_PipelineCache.
     * `m_Lights` is a StructuredBuffer of up to MAX_LIGHTS elements.
     */
    struct NV_API RHI_EngineParameterBlock {
        RHI_GpuBufferHandle m_Scene;    // ConstantBuffer<SceneUniforms> scene;
        RHI_GpuBufferHandle m_Mvp;      // ConstantBuffer<MVP> mvp;
        RHI_GpuBufferHandle m_Material; // ConstantBuffer<Material> material;
        RHI_GpuBufferHandle m_Lights;   // StructuredBuffer<LightGPU> lights;

        bool IsValid() const {
            return m_Scene.IsValid() && m_Mvp.IsValid() && m_Material.IsValid() && m_Lights.IsValid();
        }
    };

    // name -> byte offset maps describing how SetParameter() values are packed into the engine
    // CPU mirror structs above. Padding fields are intentionally absent: they are never set.
    inline const std::unordered_map<std::string, size_t>& GetMaterialLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "m_Base",                 offsetof(Material, m_Base) },
            { "m_BaseColor",            offsetof(Material, m_BaseColor) },
            { "m_DiffuseRoughness",     offsetof(Material, m_DiffuseRoughness) },
            { "m_Metalness",            offsetof(Material, m_Metalness) },
            { "m_MetalColor",           offsetof(Material, m_MetalColor) },
            { "m_Specular",             offsetof(Material, m_Specular) },
            { "m_SpecularColor",        offsetof(Material, m_SpecularColor) },
            { "m_SpecularRoughness",    offsetof(Material, m_SpecularRoughness) },
            { "m_SpecularIOR",          offsetof(Material, m_SpecularIOR) },
            { "m_SpecularAnisotropy",   offsetof(Material, m_SpecularAnisotropy) },
            { "m_SpecularRotation",     offsetof(Material, m_SpecularRotation) },
            { "m_Transmission",         offsetof(Material, m_Transmission) },
            { "m_TransmissionColor",    offsetof(Material, m_TransmissionColor) },
            { "m_Subsurface",           offsetof(Material, m_Subsurface) },
            { "m_SubsurfaceColor",      offsetof(Material, m_SubsurfaceColor) },
            { "m_SubsurfaceRadius",     offsetof(Material, m_SubsurfaceRadius) },
            { "m_SubsurfaceScale",      offsetof(Material, m_SubsurfaceScale) },
            { "m_SubsurfaceAnisotropy", offsetof(Material, m_SubsurfaceAnisotropy) },
            { "m_Sheen",                offsetof(Material, m_Sheen) },
            { "m_SheenColor",           offsetof(Material, m_SheenColor) },
            { "m_SheenRoughness",       offsetof(Material, m_SheenRoughness) },
            { "m_Coat",                 offsetof(Material, m_Coat) },
            { "m_CoatColor",            offsetof(Material, m_CoatColor) },
            { "m_CoatRoughness",        offsetof(Material, m_CoatRoughness) },
            { "m_CoatAnisotropy",       offsetof(Material, m_CoatAnisotropy) },
            { "m_CoatRotation",         offsetof(Material, m_CoatRotation) },
            { "m_CoatIOR",              offsetof(Material, m_CoatIOR) },
            { "m_CoatAffectColor",      offsetof(Material, m_CoatAffectColor) },
            { "m_CoatAffectRoughness",  offsetof(Material, m_CoatAffectRoughness) },
            { "m_Emission",             offsetof(Material, m_Emission) },
            { "m_EmissionColor",        offsetof(Material, m_EmissionColor) },
            { "m_Opacity",              offsetof(Material, m_Opacity) },
            { "m_ThinWalled",           offsetof(Material, m_ThinWalled) },
            { "m_IsOpaque",             offsetof(Material, m_IsOpaque) },
        };
        return kLayout;
    }

    inline const std::unordered_map<std::string, size_t>& GetFrameLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "m_Resolution", offsetof(FrameUniforms, m_Resolution) },
            { "m_Time",       offsetof(FrameUniforms, m_Time) },
            { "m_TimeDelta",  offsetof(FrameUniforms, m_TimeDelta) },
            { "m_FrameRate",  offsetof(FrameUniforms, m_FrameRate) },
            { "m_Frame",      offsetof(FrameUniforms, m_Frame) },
            { "m_Mouse",      offsetof(FrameUniforms, m_Mouse) },
            { "m_Date",       offsetof(FrameUniforms, m_Date) },
        };
        return kLayout;
    }

    inline const std::unordered_map<std::string, size_t>& GetSceneLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "m_CameraPos", offsetof(SceneUniforms, m_CameraPos) },
            { "m_LightCount",  offsetof(SceneUniforms, m_LightCount) },
        };
        return kLayout;
    }

    inline const std::unordered_map<std::string, size_t>& GetMvpLayout() {
        static const std::unordered_map<std::string, size_t> kLayout = {
            { "m_Model",       offsetof(MVP, m_Model) },
            { "m_View",        offsetof(MVP, m_View) },
            { "m_Proj",        offsetof(MVP, m_Proj) },
            { "m_ViewProj",    offsetof(MVP, m_ViewProj) },
            { "m_InvViewProj", offsetof(MVP, m_InvViewProj) },
        };
        return kLayout;
    }

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_UNIFORMS_H