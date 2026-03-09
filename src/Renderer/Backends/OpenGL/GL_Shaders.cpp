#include <fstream>
#include <iostream>
#include <string>
#include <sstream>

#include "Renderer/Backends/OpenGL/GL_Shaders.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    bool CheckGLProgram(GLuint prog, std::string& outLog) { 
        GLint ok = 0; 
        glGetProgramiv(prog, GL_LINK_STATUS, &ok); 
        if (ok == GL_TRUE) return true; 
        
        GLint len = 0; 
        glGetProgramiv(prog, GL_INFO_LOG_LENGTH, &len); 
        outLog.resize((len > 0) ? (size_t)len : 1); 
        glGetProgramInfoLog(prog, len, nullptr, outLog.data()); 
        NV_LOG_ERROR((std::string("[OpenGL] Program link failed:\n") + outLog).c_str()); 
        return false; 
    }

    GLuint LoadSpirvShader(GLenum stage,
        const std::vector<uint32_t>& spirv,
        const char* entryPoint,
        std::string& outLog)
    {
        if (spirv.empty()) {
            outLog = "Empty SPIR-V buffer.";
            return 0;
        }

        GLuint sh = glCreateShader(stage);

        // The enum usually comes from ARB even on OpenGL 4.6.
#ifndef GL_SHADER_BINARY_FORMAT_SPIR_V_ARB
#define GL_SHADER_BINARY_FORMAT_SPIR_V_ARB 0x9551
#endif

        glShaderBinary(1, &sh,
            GL_SHADER_BINARY_FORMAT_SPIR_V_ARB,
            spirv.data(),
            (GLsizei)(spirv.size() * sizeof(uint32_t)));

        // Shader specialization can come from core 4.6 or the ARB extension.
        bool specialized = false;

        // If GLAD was generated with 4.6 support, this variable and symbol exist.
#ifdef GL_VERSION_4_6
        if (GLAD_GL_VERSION_4_6 && glad_glSpecializeShader) {
            glad_glSpecializeShader(sh, entryPoint, 0, nullptr, nullptr);
            specialized = true;
        }
#endif

        // If GLAD was generated with GL_ARB_gl_spirv support, these symbols exist.
#ifdef GL_ARB_gl_spirv
        if (!specialized && GLAD_GL_ARB_gl_spirv && glad_glSpecializeShaderARB) {
            glad_glSpecializeShaderARB(sh, entryPoint, 0, nullptr, nullptr);
            specialized = true;
        }
#endif

        if (!specialized) {
            outLog = "Missing OpenGL 4.6 glSpecializeShader or GL_ARB_gl_spirv support (GLAD not generated or context not compatible).";
            glDeleteShader(sh);
            return 0;
        }

        GLint ok = 0;
        glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            GLint len = 0;
            glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
            outLog.resize((len > 0) ? (size_t)len : 1);
            glGetShaderInfoLog(sh, len, nullptr, outLog.data());
            glDeleteShader(sh);
            return 0;
        }

        return sh;
    }

    GLuint CompileShader(GLenum shaderType, const std::string& shaderCode) {
        if (shaderCode.empty())
            return 0;

        const char* sourcePtr = shaderCode.c_str();
        GLuint shaderID = glCreateShader(shaderType);
        glShaderSource(shaderID, 1, &sourcePtr, nullptr);
        glCompileShader(shaderID);

        GLint compileStatus;
        glGetShaderiv(shaderID, GL_COMPILE_STATUS, &compileStatus);
        if (compileStatus == GL_FALSE) {
            GLint logLength;
            glGetShaderiv(shaderID, GL_INFO_LOG_LENGTH, &logLength);

            std::string log(logLength, '\0');
            glGetShaderInfoLog(shaderID, logLength, nullptr, &log[0]);
            NV_LOG_ERROR(std::string("Shader compilation error:\n") + log);

            glDeleteShader(shaderID);
            return 0;
        }
        return shaderID;
    }

    GLuint LinkProgram(const std::initializer_list<GLuint>& shaderIDs) {
        GLuint programID = glCreateProgram();

        for (auto sid : shaderIDs)
        {
            if (sid != 0)
                glAttachShader(programID, sid);
        }

        glLinkProgram(programID);

        GLint linkStatus;
        glGetProgramiv(programID, GL_LINK_STATUS, &linkStatus);
        if (linkStatus == GL_FALSE)
        {
            GLint logLength;
            glGetProgramiv(programID, GL_INFO_LOG_LENGTH, &logLength);

            std::string log(logLength, '\0');
            glGetProgramInfoLog(programID, logLength, nullptr, &log[0]);
            NV_LOG_ERROR(std::string("Program linking error:\n") + log);

            glDeleteProgram(programID);
            return 0;
        }

        for (auto sid : shaderIDs)
        {
            if (sid != 0)
            {
                glDetachShader(programID, sid);
                glDeleteShader(sid);
            }
        }

        return programID;
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL