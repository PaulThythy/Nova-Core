#include <fstream>
#include <iostream>
#include <string>
#include <sstream>
#include <glm/gtc/type_ptr.hpp>

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

        // L'enum est g�n�ralement ARB m�me en 4.6
#ifndef GL_SHADER_BINARY_FORMAT_SPIR_V_ARB
#define GL_SHADER_BINARY_FORMAT_SPIR_V_ARB 0x9551
#endif

        glShaderBinary(1, &sh,
            GL_SHADER_BINARY_FORMAT_SPIR_V_ARB,
            spirv.data(),
            (GLsizei)(spirv.size() * sizeof(uint32_t)));

        // Sp�cialisation : core 4.6 OU ARB
        bool specialized = false;

        // Si ton GLAD est g�n�r� avec 4.6, cette variable + ce symbole existent.
#ifdef GL_VERSION_4_6
        if (GLAD_GL_VERSION_4_6 && glad_glSpecializeShader) {
            glad_glSpecializeShader(sh, entryPoint, 0, nullptr, nullptr);
            specialized = true;
        }
#endif

        // Si ton GLAD est g�n�r� avec GL_ARB_gl_spirv, ceux-ci existent.
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
            std::cerr << "Shader compilation error:\n" << log << std::endl;

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
            std::cerr << "Program linking error:\n" << log << std::endl;

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

    GL_ShaderProgram::GL_ShaderProgram(const std::string& name,
        GLuint             linkedProgram,
        GLuint             uboBindingIndex)
        : m_Name(name)
        , m_Program(linkedProgram)
        , m_UBOBinding(uboBindingIndex)
    {
        // Introspect le bloc UBO standard (nom choisi dans le shader)
        IntrospectUBO("UBO_PerObject");
    }

    GL_ShaderProgram::~GL_ShaderProgram() {
        if (m_UBO) {
            glDeleteBuffers(1, &m_UBO);
            m_UBO = 0;
        }
        if (m_Program) {
            glDeleteProgram(m_Program);
            m_Program = 0;
        }
    }

    // ---------------------------------------------------------------
    void GL_ShaderProgram::IntrospectUBO(const std::string& blockName) {
        m_BlockIndex = glGetUniformBlockIndex(m_Program, blockName.c_str());
        if (m_BlockIndex == GL_INVALID_INDEX) {
            NV_LOG_WARN(("UBO block '" + blockName + "' not found in program '" + m_Name + "'").c_str());
            return;
        }

        // Taille du bloc
        GLint blockSize = 0;
        glGetActiveUniformBlockiv(m_Program, m_BlockIndex, GL_UNIFORM_BLOCK_DATA_SIZE, &blockSize);
        m_BlockSize = static_cast<std::size_t>(blockSize);
        m_CpuBuffer.resize(m_BlockSize, 0);

        // Lister les uniforms du bloc
        GLint numUniforms = 0;
        glGetActiveUniformBlockiv(m_Program, m_BlockIndex, GL_UNIFORM_BLOCK_ACTIVE_UNIFORMS, &numUniforms);

        std::vector<GLint> indices(numUniforms);
        glGetActiveUniformBlockiv(m_Program, m_BlockIndex,
            GL_UNIFORM_BLOCK_ACTIVE_UNIFORM_INDICES, indices.data());

        std::vector<GLuint> uIndices(indices.begin(), indices.end());
        std::vector<GLint> offsets(numUniforms);
        std::vector<GLint> sizes(numUniforms);   // en "unités de type"
        std::vector<GLint> types(numUniforms);

        glGetActiveUniformsiv(m_Program, numUniforms, uIndices.data(), GL_UNIFORM_OFFSET, offsets.data());
        glGetActiveUniformsiv(m_Program, numUniforms, uIndices.data(), GL_UNIFORM_SIZE, sizes.data());
        glGetActiveUniformsiv(m_Program, numUniforms, uIndices.data(), GL_UNIFORM_TYPE, types.data());

        for (int i = 0; i < numUniforms; ++i) {
            // Récupérer le nom
            GLint nameLen = 0;
            glGetActiveUniformsiv(m_Program, 1, &uIndices[i], GL_UNIFORM_NAME_LENGTH, &nameLen);
            std::string uName(nameLen, '\0');
            glGetActiveUniformName(m_Program, uIndices[i],
                nameLen, nullptr, uName.data());

            // Supprimer le '\0' terminal si présent
            while (!uName.empty() && uName.back() == '\0')
                uName.pop_back();

            // Retirer le préfixe "BlockName." ajouté par certains drivers
            const std::string prefix = blockName + ".";
            if (uName.rfind(prefix, 0) == 0)
                uName = uName.substr(prefix.size());

            // Taille en bytes selon le type GLSL
            std::size_t byteSize = 0;
            switch (types[i]) {
            case GL_FLOAT:      byteSize = sizeof(float);       break;
            case GL_INT:        byteSize = sizeof(int);         break;
            case GL_FLOAT_VEC2: byteSize = sizeof(glm::vec2);   break;
            case GL_FLOAT_VEC3: byteSize = sizeof(glm::vec3);   break; // 12 bytes (attention padding std140 !)
            case GL_FLOAT_VEC4: byteSize = sizeof(glm::vec4);   break;
            case GL_FLOAT_MAT3: byteSize = sizeof(glm::mat3);   break;
            case GL_FLOAT_MAT4: byteSize = sizeof(glm::mat4);   break;
            default:            byteSize = 4;                   break;
            }

            m_Layout[uName] = GL_UniformEntry{
                static_cast<std::size_t>(offsets[i]),
                byteSize
            };
        }

        // Créer et lier le UBO
        glGenBuffers(1, &m_UBO);
        glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
        glBufferData(GL_UNIFORM_BUFFER,
            static_cast<GLsizeiptr>(m_BlockSize),
            nullptr,
            GL_DYNAMIC_DRAW);
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        // Lier le bloc à son binding point
        glUniformBlockBinding(m_Program, m_BlockIndex, m_UBOBinding);
        glBindBufferBase(GL_UNIFORM_BUFFER, m_UBOBinding, m_UBO);
    }

    // ---------------------------------------------------------------
    void GL_ShaderProgram::WriteToBuffer(const std::string& name,
        const void* data,
        std::size_t        bytes) {
        auto it = m_Layout.find(name);
        if (it == m_Layout.end()) {
            NV_LOG_WARN(("[GL_ShaderProgram] Unknown uniform '" + name + "' in '" + m_Name + "'").c_str());
            return;
        }
        const auto& entry = it->second;
        if (entry.offsetInBlock + bytes > m_BlockSize) {
            NV_LOG_ERROR(("[GL_ShaderProgram] Uniform '" + name + "' out of UBO bounds").c_str());
            return;
        }
        std::memcpy(m_CpuBuffer.data() + entry.offsetInBlock, data, bytes);
        m_Dirty = true;
    }

    // ---------------------------------------------------------------
    void GL_ShaderProgram::Bind() const {
        glUseProgram(m_Program);
        if (m_UBO)
            glBindBufferBase(GL_UNIFORM_BUFFER, m_UBOBinding, m_UBO);
    }

    void GL_ShaderProgram::Unbind() const {
        glUseProgram(0);
    }

    // ---------------------------------------------------------------
    void GL_ShaderProgram::UploadUniforms() {
        if (!m_Dirty || !m_UBO || m_BlockSize == 0) return;

        glBindBuffer(GL_UNIFORM_BUFFER, m_UBO);
        glBufferSubData(GL_UNIFORM_BUFFER, 0,
            static_cast<GLsizeiptr>(m_BlockSize),
            m_CpuBuffer.data());
        glBindBuffer(GL_UNIFORM_BUFFER, 0);

        m_Dirty = false;
    }

    // ---------------------------------------------------------------
    // Set* — délèguent tous à WriteToBuffer
    void GL_ShaderProgram::SetFloat(const std::string& name, float v) {
        WriteToBuffer(name, &v, sizeof(float));
    }
    void GL_ShaderProgram::SetInt(const std::string& name, int v) {
        WriteToBuffer(name, &v, sizeof(int));
    }
    void GL_ShaderProgram::SetVec2(const std::string& name, const glm::vec2& v) {
        WriteToBuffer(name, glm::value_ptr(v), sizeof(glm::vec2));
    }
    void GL_ShaderProgram::SetVec3(const std::string& name, const glm::vec3& v) {
        // std140 : vec3 est aligné sur 16 bytes → on écrit seulement 12 bytes,
        // le driver a réservé 16 dans le bloc, on ne dépasse pas l'offset du suivant.
        WriteToBuffer(name, glm::value_ptr(v), sizeof(glm::vec3));
    }
    void GL_ShaderProgram::SetVec4(const std::string& name, const glm::vec4& v) {
        WriteToBuffer(name, glm::value_ptr(v), sizeof(glm::vec4));
    }
    void GL_ShaderProgram::SetMat3(const std::string& name, const glm::mat3& v) {
        // std140 : mat3 = 3 colonnes de vec4 (36 → 48 bytes côté GPU).
        // On écrit colonne par colonne pour respecter le padding.
        auto it = m_Layout.find(name);
        if (it == m_Layout.end()) return;
        const std::size_t base = it->second.offsetInBlock;
        for (int col = 0; col < 3; ++col) {
            const glm::vec3 column = v[col];
            std::memcpy(m_CpuBuffer.data() + base + col * 16,
                glm::value_ptr(column), sizeof(glm::vec3));
        }
        m_Dirty = true;
    }
    void GL_ShaderProgram::SetMat4(const std::string& name, const glm::mat4& v) {
        WriteToBuffer(name, glm::value_ptr(v), sizeof(glm::mat4));
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL