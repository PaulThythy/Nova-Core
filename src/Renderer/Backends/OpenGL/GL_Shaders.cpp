#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

#include "Renderer/Backends/OpenGL/GL_Shaders.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

#include <glm/glm.hpp>

namespace Nova::Core::Renderer::Backends::OpenGL {

    // --- GL_Shaders ---
    GL_Shaders::~GL_Shaders() {
        if (m_BufInstances != 0) { glDeleteBuffers(1, &m_BufInstances); m_BufInstances = 0; }
        if (m_BufGlobals != 0) { glDeleteBuffers(1, &m_BufGlobals); m_BufGlobals = 0; }
        if (m_BufMaterial != 0) { glDeleteBuffers(1, &m_BufMaterial); m_BufMaterial = 0; }
        if (m_BufMvp != 0) { glDeleteBuffers(1, &m_BufMvp); m_BufMvp = 0; }
        m_Program = 0;
        m_InstanceBufferSize = 0;
        m_LocationCache.clear();
    }

    void GL_Shaders::SetProgram(GLuint program) {
        m_Program = program;
        m_LocationCache.clear();

        if (m_BufMvp != 0) { glDeleteBuffers(1, &m_BufMvp); m_BufMvp = 0; }
        glGenBuffers(1, &m_BufMvp);
        glBindBuffer(GL_UNIFORM_BUFFER, m_BufMvp);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(RHI::MVP), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Mvp), m_BufMvp);

        if (m_BufMaterial != 0) { glDeleteBuffers(1, &m_BufMaterial); m_BufMaterial = 0; }
        glGenBuffers(1, &m_BufMaterial);
        glBindBuffer(GL_UNIFORM_BUFFER, m_BufMaterial);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(RHI::Material), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Material), m_BufMaterial);

        if (m_BufGlobals != 0) { glDeleteBuffers(1, &m_BufGlobals); m_BufGlobals = 0; }
        glGenBuffers(1, &m_BufGlobals);
        glBindBuffer(GL_UNIFORM_BUFFER, m_BufGlobals);
        glBufferData(GL_UNIFORM_BUFFER, sizeof(RHI::Globals), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Globals), m_BufGlobals);

        if (m_BufInstances != 0) { glDeleteBuffers(1, &m_BufInstances); m_BufInstances = 0; }
        glGenBuffers(1, &m_BufInstances);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_BufInstances);
        m_InstanceBufferSize = sizeof(RHI::Instance);
        glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(m_InstanceBufferSize), nullptr, GL_DYNAMIC_DRAW);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Instances), m_BufInstances);
    }

    void GL_Shaders::UploadMaterialUBO() {
        if (m_BufMaterial == 0) return;
        RHI::Material data{};
        const auto layout = RHI::GetMaterialParameterLayout();
        for (const auto& [name, offset] : layout) {
            auto it = m_Parameters.find(name);
            if (it == m_Parameters.end()) continue;
            char* dst = reinterpret_cast<char*>(&data) + offset;
            std::visit([dst](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, glm::vec3>) *reinterpret_cast<glm::vec4*>(dst) = glm::vec4(v, 1.0f);
                else if constexpr (std::is_same_v<T, glm::vec4>) *reinterpret_cast<glm::vec4*>(dst) = v;
            }, it->second);
        }
        glBindBuffer(GL_UNIFORM_BUFFER, m_BufMaterial);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(RHI::Material), &data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Material), m_BufMaterial);
    }

    void GL_Shaders::UploadGlobalsUBO() {
        if (m_BufGlobals == 0) return;
        RHI::Globals data{};
        const auto layout = RHI::GetGlobalsLayout();
        for (const auto& [name, offset] : layout) {
            auto it = m_Parameters.find(name);
            if (it == m_Parameters.end()) continue;
            char* dst = reinterpret_cast<char*>(&data) + offset;
            std::visit([dst](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, int>) *reinterpret_cast<int*>(dst) = v;
                else if constexpr (std::is_same_v<T, float>) *reinterpret_cast<float*>(dst) = v;
                else if constexpr (std::is_same_v<T, glm::vec3>) *reinterpret_cast<glm::vec3*>(dst) = v;
                else if constexpr (std::is_same_v<T, glm::vec4>) *reinterpret_cast<glm::vec4*>(dst) = v;
            }, it->second);
        }
        glBindBuffer(GL_UNIFORM_BUFFER, m_BufGlobals);
        glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(RHI::Globals), &data);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);
        glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Globals), m_BufGlobals);
    }

    GLint GL_Shaders::GetLocation(const std::string& name) {
        auto it = m_LocationCache.find(name);
        if (it != m_LocationCache.end())
            return it->second;
        GLint loc = glGetUniformLocation(m_Program, name.c_str());
        m_LocationCache[name] = loc;
        return loc;
    }

    void GL_Shaders::Bind(void* apiContext) {
        (void)apiContext;
        if (m_Program != 0)
            glUseProgram(m_Program);
    }

    void GL_Shaders::ApplyParameters(void* apiContext) {
        (void)apiContext;
        if (m_Program == 0) return;

        glUseProgram(m_Program);

        if (m_BufMvp != 0) {
            RHI::MVP mvp{};
            auto itM = m_Parameters.find("model"), itV = m_Parameters.find("view"), itP = m_Parameters.find("proj"), itVP = m_Parameters.find("viewProj"), itInvVP = m_Parameters.find("invViewProj");
            if (itM != m_Parameters.end() && std::holds_alternative<glm::mat4>(itM->second)) mvp.model = std::get<glm::mat4>(itM->second);
            if (itV != m_Parameters.end() && std::holds_alternative<glm::mat4>(itV->second)) mvp.view = std::get<glm::mat4>(itV->second);
            if (itP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itP->second)) mvp.proj = std::get<glm::mat4>(itP->second);
            if (itVP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itVP->second)) mvp.viewProj = std::get<glm::mat4>(itVP->second);
            if (itInvVP != m_Parameters.end() && std::holds_alternative<glm::mat4>(itInvVP->second)) mvp.invViewProj = std::get<glm::mat4>(itInvVP->second);
            glBindBuffer(GL_UNIFORM_BUFFER, m_BufMvp);
            glBufferSubData(GL_UNIFORM_BUFFER, 0, sizeof(RHI::MVP), &mvp);
            glBindBuffer(GL_UNIFORM_BUFFER, 0);
            glBindBufferBase(GL_UNIFORM_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Mvp), m_BufMvp);
        }

        UploadMaterialUBO();
        UploadGlobalsUBO();

        const auto materialLayout = RHI::GetMaterialParameterLayout();
        const auto globalsLayout = RHI::GetGlobalsLayout();
        for (const auto& [name, value] : m_Parameters) {
            if (name == "model" || name == "view" || name == "proj" || name == "viewProj" || name == "invViewProj")
                continue;
            if (materialLayout.count(name) || globalsLayout.count(name))
                continue;
            GLint loc = GetLocation(name);
            if (loc < 0) continue;
            std::visit([loc](auto&& v) {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, std::monostate>) {}
                else if constexpr (std::is_same_v<T, int>)
                    glUniform1i(loc, v);
                else if constexpr (std::is_same_v<T, float>)
                    glUniform1f(loc, v);
                else if constexpr (std::is_same_v<T, glm::vec2>)
                    glUniform2fv(loc, 1, glm::value_ptr(v));
                else if constexpr (std::is_same_v<T, glm::vec3>)
                    glUniform3fv(loc, 1, glm::value_ptr(v));
                else if constexpr (std::is_same_v<T, glm::vec4>)
                    glUniform4fv(loc, 1, glm::value_ptr(v));
                else if constexpr (std::is_same_v<T, glm::mat2>)
                    glUniformMatrix2fv(loc, 1, GL_FALSE, glm::value_ptr(v));
                else if constexpr (std::is_same_v<T, glm::mat3>)
                    glUniformMatrix3fv(loc, 1, GL_FALSE, glm::value_ptr(v));
                else if constexpr (std::is_same_v<T, glm::mat4>)
                    glUniformMatrix4fv(loc, 1, GL_FALSE, glm::value_ptr(v));
            }, value);
        }
    }

    void GL_Shaders::SetInstanceData(const std::vector<RHI::Instance>& instances) {
        if (m_BufInstances == 0 || instances.empty())
            return;

        const size_t requiredSize = instances.size() * sizeof(RHI::Instance);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_BufInstances);
        if (requiredSize > m_InstanceBufferSize) {
            m_InstanceBufferSize = requiredSize;
            glBufferData(GL_SHADER_STORAGE_BUFFER, static_cast<GLsizeiptr>(m_InstanceBufferSize), nullptr, GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, static_cast<GLsizeiptr>(requiredSize), instances.data());
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, static_cast<GLuint>(RHI::EngineResourceSlot::Instances), m_BufInstances);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    void* GL_Shaders::GetNativeHandle() const {
        return reinterpret_cast<void*>(static_cast<intptr_t>(m_Program));
    }

    // --- Free functions ---
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