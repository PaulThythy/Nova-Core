#ifndef RHI_SHADERS_H
#define RHI_SHADERS_H

#include <string>
#include <unordered_map>
#include <variant>

#include <glm/glm.hpp>

#include "Api.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"
#include "Renderer/RHI/RHI_ShaderReflection.h"
#include "Renderer/RHI/RHI_ShaderResourceSet.h"

namespace Nova::Core::Renderer::RHI {

    /** Type-safe storage for shader uniform values (name -> value). */
    using UniformValue = std::variant<
        int, float, glm::vec2, glm::vec3, glm::vec4, glm::mat2, glm::mat3, glm::mat4
    >;
    
    /**
     * Abstract linked shader program (e.g. vertex + fragment). Backend implements Bind /
     * ApplyParameters / GetNativeHandle. Holds SetParameter() values and named Resources().
     * Descriptor sets/bindings always come from Slang reflection.
     */
    class NV_API IShaders {
    public:
        virtual ~IShaders() = default;

        /** Store a uniform by name. Same API for all backends. */
        void SetParameter(const std::string& name, int value);
        void SetParameter(const std::string& name, float value);
        void SetParameter(const std::string& name, const glm::vec2& value);
        void SetParameter(const std::string& name, const glm::vec3& value);
        void SetParameter(const std::string& name, const glm::vec4& value);
        void SetParameter(const std::string& name, const glm::mat2& value);
        void SetParameter(const std::string& name, const glm::mat3& value);
        void SetParameter(const std::string& name, const glm::mat4& value);

        /** Set/replace the reflection used for named resource binding. */
        void SetReflection(const RHI_ProgramReflection& reflection) {
            m_Reflection = reflection;
            m_Resources.SetReflection(&m_Reflection);
        }
        const RHI_ProgramReflection& GetReflection() const { return m_Reflection; }

        /** Access the shader's resource set (bind by reflection name). */
        RHI_ShaderResourceSet& Resources() { return m_Resources; }
        const RHI_ShaderResourceSet& Resources() const { return m_Resources; }

        /** Apply `Resources()` to the backend shader object. */
        bool CommitResources() { return m_Resources.Apply(this); }

        /**
         * Bind a sampled image + sampler by reflection name (e.g. `outline.source` /
         * `outline.sourceSampler`). `imageView` / `sampler` are backend-native handles
         * (VkImageView / VkSampler cast to uint64_t). Prefer resolving them via
         * `IRenderGraph::GetSampledTextureNativeHandles`.
         */
        virtual bool BindSampledTexture(
            const std::string& textureName,
            const std::string& samplerName,
            uint64_t imageView,
            uint64_t sampler,
            uint32_t imageLayout = 0)
        {
            (void)textureName; (void)samplerName; (void)imageView; (void)sampler; (void)imageLayout;
            return false;
        }

        /** Bind the shader for drawing (e.g. vkCmdBindPipeline). apiContext: Vulkan = VkCommandBuffer*. */
        virtual void Bind(void* apiContext = nullptr) = 0;
        /**
         * Upload all shader parameters for the current draw (engine UBOs, user bindings, etc.).
         * apiContext: Vulkan = VkCommandBuffer*.
         */
        virtual void ApplyParameters(void* apiContext = nullptr) = 0;
        /** Backend-specific handle (e.g. GL program id, VkPipeline). */
        virtual void* GetNativeHandle() const = 0;

    protected:
        friend class RHI_ShaderResourceSet;

        /** Backend hook used by `RHI_ShaderResourceSet` to apply named bindings. */
        virtual bool ApplyResourceBinding(const RHI_BindingInfo& info, const RHI_ResourceBinding& value) = 0;

        std::unordered_map<std::string, UniformValue> m_Parameters;

        RHI_ProgramReflection m_Reflection{};
        RHI_ShaderResourceSet m_Resources{ &m_Reflection };
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADERS_H