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
 
         // Note: shader parameter name is `u_BaseColorFactor`.
         static Material FromColor(const glm::vec4& color) {
             Material m{};
             m.m_BaseColorFactor = color;
             return m;
         }
 
         RHI::Material ToRhi() const {
             RHI::Material out{};
             out.baseColorFactor = m_BaseColorFactor;
             out.emissiveFactor = glm::vec4(m_EmissiveFactor, 1.0f);
             out.metallicFactor = m_MetallicFactor;
             out.roughnessFactor = m_RoughnessFactor;
             out.normalScale = m_NormalScale;
             out.occlusionStrength = m_OcclusionStrength;
             out.emissiveStrength = m_EmissiveStrength;
             out.alphaCutoff = m_AlphaCutoff;
             out.alphaMode = m_AlphaMode;
             return out;
         }
     };
 
 } // namespace Nova::Core::Renderer::Graphics
 
 #endif // NOVA_GRAPHICS_MATERIAL_H
