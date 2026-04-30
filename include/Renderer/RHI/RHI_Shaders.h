#ifndef RHI_SHADERS_H
#define RHI_SHADERS_H

#include <string>
#include <vector>
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
     * Base class for a linked shader program (e.g. vertex + fragment).
     * Holds a map of uniform names to values; backends (GL/VK) implement
     * Bind() and ApplyParameters() to upload them to the GPU.
     */
    class NV_API RHI_Shaders {
    public:
        virtual ~RHI_Shaders() = default;

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

        /** Bind the shader for drawing (e.g. glUseProgram / vkCmdBindPipeline). apiContext: GL = nullptr, VK = VkCommandBuffer*. */
        virtual void Bind(void* apiContext = nullptr) = 0;
        /** Upload m_Parameters to the GPU. apiContext: GL = nullptr, VK = VkCommandBuffer*. */
        virtual void ApplyParameters(void* apiContext = nullptr) = 0;
        /** Upload per-instance data for instanced draws. */
        virtual void SetInstanceData(const std::vector<RHI::Instance>& instances) = 0;
        /** Backend-specific handle (e.g. GL program id, VkPipeline). */
        virtual void* GetNativeHandle() const = 0;

    protected:
        std::unordered_map<std::string, UniformValue> m_Parameters;

        RHI_ProgramReflection m_Reflection{};
        RHI_ShaderResourceSet m_Resources{ &m_Reflection };
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADERS_H