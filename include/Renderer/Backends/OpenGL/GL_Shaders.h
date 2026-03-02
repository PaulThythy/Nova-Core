#ifndef GL_SHADERS_HPP
#define GL_SHADERS_HPP

#include <glad/gl.h>
#include <string>
#include <vector>

#include "Core/Log.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    GLuint CompileShader(GLenum shaderType, const std::string& shaderCode);
    GLuint LinkProgram(const std::initializer_list<GLuint>& shaderIDs);

    bool CheckGLProgram(GLuint prog, std::string& outLog);
    GLuint LoadSpirvShader(GLenum stage,
        const std::vector<uint32_t>& spirv,
        const char* entryPoint,
        std::string& outLog);

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_SHADERS_HPP