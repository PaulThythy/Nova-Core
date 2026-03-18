#include "Renderer/RHI/RHI_Shaders.h"

namespace Nova::Core::Renderer::RHI {

    void RHI_Shaders::SetParameter(const std::string& name, int value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, float value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, const glm::vec2& value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, const glm::vec3& value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, const glm::vec4& value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, const glm::mat2& value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, const glm::mat3& value) {
        m_Parameters[name] = value;
    }

    void RHI_Shaders::SetParameter(const std::string& name, const glm::mat4& value) {
        m_Parameters[name] = value;
    }

} // namespace Nova::Core::Renderer::RHI