 #ifndef NOVA_GRAPHICS_MATERIAL_H
 #define NOVA_GRAPHICS_MATERIAL_H
 
 #include <glm/glm.hpp>
 
 #include "Api.h"
 #include "Renderer/RHI/RHI_ShaderUniforms.h"
 
 namespace Nova::Core::Renderer::Graphics {
 
     struct NV_API Material {
         // Metallic/Roughness PBR (factors only for now; textures later).
         glm::vec4 m_BaseColorFactor{ 1.0f };
         float m_MetallicFactor{ 0.0f };
         float m_RoughnessFactor{ 1.0f };
 
         glm::vec3 m_EmissiveFactor{ 0.0f, 0.0f, 0.0f };
         float m_EmissiveStrength{ 0.0f };
 
         float m_NormalScale{ 1.0f };
         float m_OcclusionStrength{ 1.0f };
         float m_AlphaCutoff{ 0.5f };
         int   m_AlphaMode{ 0 }; // 0=Opaque, 1=Mask, 2=Blend (placeholder)
 
        static Material FromColor(const glm::vec4& color) {
            Material m{};
            m.m_BaseColorFactor = color;
            return m;
        }

        RHI::Material ToRhi() const {
            RHI::Material out{};
            out.base = 1.0f;
            out.baseColor = glm::vec3(m_BaseColorFactor);
            out.metalness = m_MetallicFactor;
            out.specularRoughness = m_RoughnessFactor;
            out.emissionColor = m_EmissiveFactor;
            out.emission = m_EmissiveStrength;
            const float a = m_BaseColorFactor.w;
            out.opacity = glm::vec3(a, a, a);
            out.isOpaque = (m_AlphaMode == 0) ? 1u : 0u;
            return out;
        }
     };
 
 } // namespace Nova::Core::Renderer::Graphics
 
 #endif // NOVA_GRAPHICS_MATERIAL_H
