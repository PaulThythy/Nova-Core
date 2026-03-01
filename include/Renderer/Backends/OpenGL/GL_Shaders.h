#ifndef GL_SHADERS_HPP
#define GL_SHADERS_HPP

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <string>
#include <unordered_map>
#include <vector>
#include <variant>

#include "Core/Log.h"
#include "Renderer/RHI/RHI_Shaders.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    GLuint CompileShader(GLenum shaderType, const std::string& shaderCode);
    GLuint LinkProgram(const std::initializer_list<GLuint>& shaderIDs);

    bool CheckGLProgram(GLuint prog, std::string& outLog);
    GLuint LoadSpirvShader(GLenum stage,
        const std::vector<uint32_t>& spirv,
        const char* entryPoint,
        std::string& outLog);

    // Un "slot" dans le miroir CPU du UBO
    struct GL_UniformEntry {
        std::size_t offsetInBlock = 0; // offset en bytes dans le UBO
        std::size_t size = 0; // taille en bytes
    };

    // ---------------------------------------------------------------
    class GL_ShaderProgram : public Nova::Core::Renderer::RHI::IShaderProgram {
    public:
        GL_ShaderProgram(const std::string& name,
            GLuint             linkedProgram,
            GLuint             uboBindingIndex = 0);
        ~GL_ShaderProgram() override;

        void Bind()   const override;
        void Unbind() const override;

        void SetFloat(const std::string& name, float v)            override;
        void SetInt(const std::string& name, int v)              override;
        void SetVec2(const std::string& name, const glm::vec2& v) override;
        void SetVec3(const std::string& name, const glm::vec3& v) override;
        void SetVec4(const std::string& name, const glm::vec4& v) override;
        void SetMat3(const std::string& name, const glm::mat3& v) override;
        void SetMat4(const std::string& name, const glm::mat4& v) override;

        void UploadUniforms() override;

        bool              IsLinked()  const override { return m_Program != 0; }
        const std::string& GetName()  const override { return m_Name; }

        GLuint GetGLProgram() const { return m_Program; }

    private:
        void IntrospectUBO(const std::string& blockName);

        void WriteToBuffer(const std::string& name, const void* data, std::size_t bytes);

    private:
        std::string m_Name;
        GLuint      m_Program = 0;
        GLuint      m_UBO = 0;              // GL buffer object
        GLuint      m_UBOBinding = 0;       // binding point
        GLuint      m_BlockIndex = GL_INVALID_INDEX;

        std::size_t              m_BlockSize = 0;
        std::vector<uint8_t>     m_CpuBuffer;
        bool                     m_Dirty = false;

        // name → offset+size dans m_CpuBuffer
        std::unordered_map<std::string, GL_UniformEntry> m_Layout;
    };


} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_SHADERS_HPP