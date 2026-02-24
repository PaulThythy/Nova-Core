#include "Renderer/Backends/OpenGL/GL_Renderer.h"
#include "Core/Application.h"
#include "Core/Log.h"

#include <glad/gl.h>
#include <filesystem>
#include <string>
#include <glm/gtc/type_ptr.hpp>

#include "Asset/AssetManager.h"
#include "Asset/Assets/ShaderAsset.h"

#include "Renderer/Backends/OpenGL/GL_Mesh.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    GLenum ToGLTopology(Nova::Core::Renderer::RHI::RHI_PrimitiveTopology topo) {
        using namespace Nova::Core::Renderer::RHI;
        switch (topo) {
            case RHI_PrimitiveTopology::Triangles: return GL_TRIANGLES;
            case RHI_PrimitiveTopology::Lines:     return GL_LINES;
            case RHI_PrimitiveTopology::Points:    return GL_POINTS;
            default:                               return GL_TRIANGLES;
        }
    }

    GLenum ToGLIndexType(Nova::Core::Renderer::RHI::RHI_IndexType type) {
        using namespace Nova::Core::Renderer::RHI;
        switch (type) {
            case RHI_IndexType::UInt16: return GL_UNSIGNED_SHORT;
            case RHI_IndexType::UInt32: return GL_UNSIGNED_INT;
            default:                    return GL_UNSIGNED_INT;
        }
    }

    size_t IndexTypeSize(Nova::Core::Renderer::RHI::RHI_IndexType type) {
        using namespace Nova::Core::Renderer::RHI;
        switch (type) {
            case RHI_IndexType::UInt16: return sizeof(uint16_t);
            case RHI_IndexType::UInt32: return sizeof(uint32_t);
            default:                    return sizeof(uint32_t);
        }
    }

    static bool CheckGLProgram(GLuint prog, std::string& outLog) { 
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

    static GLuint LoadSpirvShader(GLenum stage,
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

    bool GL_Renderer::Create()
    {
        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetImGuiBackend(GraphicsAPI::OpenGL);

        //if (!glIsEnabled(GL_CONTEXT_CORE_PROFILE_BIT))
            //return false;
        //invert clip space to be the same as vulkan
        //glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // clip space changed, warning : change winding order
        //glEnable(GL_CULL_FACE);
        //glCullFace(GL_BACK);
        // inverted winding as vulkan, due to clipspace inversion
        //glFrontFace(GL_CCW);

        std::filesystem::path p = std::filesystem::current_path();
        std::filesystem::path shaderDir = p / "Nova-Core" / "Resources" / "Engine" / "Shaders";
        std::filesystem::path vertPath = shaderDir / "model.vert";
        std::filesystem::path fragPath = shaderDir / "model.frag";

        using Nova::Core::Asset::AssetManager;
        using Nova::Core::Asset::Assets::ShaderAsset;
        using Nova::Core::Renderer::RHI::RHI_ShaderStage;

        Nova::Core::Renderer::RHI::RHI_ShaderDesc vDesc{};
        vDesc.m_Stage = RHI_ShaderStage::Vertex;
        vDesc.m_GlslVersion = 450;

        Nova::Core::Renderer::RHI::RHI_ShaderDesc fDesc{};
        fDesc.m_Stage = RHI_ShaderStage::Fragment;
        fDesc.m_GlslVersion = 450;

        auto vert = AssetManager::Get().Acquire<ShaderAsset>(vertPath, vDesc);
        auto frag = AssetManager::Get().Acquire<ShaderAsset>(fragPath, fDesc);

        if (!vert || !frag) return false;
        if (!vert->Compile()) {
            NV_LOG_ERROR(("Vertex shader compile failed:\n" + vert->GetLastLog()).c_str());
            return false;
        }
        if (!frag->Compile()) {
            NV_LOG_ERROR(("Fragment shader compile failed:\n" + frag->GetLastLog()).c_str());
            return false;
        }

        GLuint vs = 0, fs = 0;
        std::string log;

        vs = LoadSpirvShader(GL_VERTEX_SHADER, vert->GetSpirv(), "main", log);
        if (!vs) NV_LOG_WARN((std::string("SPIR-V VS failed, fallback GLSL: ") + log).c_str());

        fs = LoadSpirvShader(GL_FRAGMENT_SHADER, frag->GetSpirv(), "main", log);
        if (!fs) NV_LOG_WARN((std::string("SPIR-V FS failed, fallback GLSL: ") + log).c_str());

        GLuint prog = glCreateProgram();
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glLinkProgram(prog);

        glDetachShader(prog, vs);
        glDetachShader(prog, fs);
        glDeleteShader(vs);
        glDeleteShader(fs);

        if (!CheckGLProgram(prog, log)) {
            glDeleteProgram(prog);
            return false;
        }

        m_Program = prog;
        NV_LOG_INFO("OpenGL Renderer created. Triangle program linked from SPIR-V.");
        return true;
    }

    void GL_Renderer::Destroy() {
        if (m_Program != 0) {
            glDeleteProgram(m_Program);
            m_Program = 0;
        }

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        NV_LOG_INFO("OpenGL Renderer destroyed.");
    }

    bool GL_Renderer::Resize(int w, int h) {
        if (w <= 0 || h <= 0) return false;
        glViewport(0, 0, w, h);
        return true;
    }

    void GL_Renderer::Update(float dt) {
        (void)dt;
    }

    void GL_Renderer::BeginFrame() {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void GL_Renderer::Render() {
        if (!m_Program) return;

        glUseProgram(m_Program);

        glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    void GL_Renderer::EndFrame() {
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void GL_Renderer::Draw(const RHI::RHI_DrawCommand& cmd) {
        if (!m_Program || !cmd.m_Mesh) return;

        auto glMesh = std::dynamic_pointer_cast<GL_Mesh>(cmd.m_Mesh);
        if (!glMesh) {
            NV_LOG_WARN("GL_Renderer::Draw - mesh is not a GL_Mesh");
            return;
        }

        glUseProgram(m_Program);

        if (m_LocModel >= 0) glUniformMatrix4fv(m_LocModel, 1, GL_FALSE, glm::value_ptr(cmd.m_Model));
        if (m_LocView  >= 0) glUniformMatrix4fv(m_LocView,  1, GL_FALSE, glm::value_ptr(cmd.m_View));
        if (m_LocProj  >= 0) glUniformMatrix4fv(m_LocProj,  1, GL_FALSE, glm::value_ptr(cmd.m_Proj));

        if (m_LocMVP >= 0) {
            const glm::mat4 mvp = cmd.m_Proj * cmd.m_View * cmd.m_Model;
            glUniformMatrix4fv(m_LocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        }

        glMesh->Bind();

        const GLenum mode = ToGLTopology(cmd.m_Topology);

        if (cmd.m_InstanceCount > 1) {
            glDrawArraysInstanced(
                mode,
                static_cast<GLint>(cmd.m_FirstVertex),
                static_cast<GLsizei>(cmd.m_VertexCount),
                static_cast<GLsizei>(cmd.m_InstanceCount)
            );
        } else {
            glDrawArrays(
                mode,
                static_cast<GLint>(cmd.m_FirstVertex),
                static_cast<GLsizei>(cmd.m_VertexCount)
            );
        }

        glMesh->Unbind();
    }

    void GL_Renderer::DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        if (!m_Program || !cmd.m_Mesh) return;

        auto glMesh = std::dynamic_pointer_cast<GL_Mesh>(cmd.m_Mesh);
        if (!glMesh) {
            NV_LOG_WARN("GL_Renderer::DrawIndexed - mesh is not a GL_Mesh");
            return;
        }

        glUseProgram(m_Program);

        if (m_LocModel >= 0) glUniformMatrix4fv(m_LocModel, 1, GL_FALSE, glm::value_ptr(cmd.m_Model));
        if (m_LocView  >= 0) glUniformMatrix4fv(m_LocView,  1, GL_FALSE, glm::value_ptr(cmd.m_View));
        if (m_LocProj  >= 0) glUniformMatrix4fv(m_LocProj,  1, GL_FALSE, glm::value_ptr(cmd.m_Proj));

        if (m_LocMVP >= 0) {
            const glm::mat4 mvp = cmd.m_Proj * cmd.m_View * cmd.m_Model;
            glUniformMatrix4fv(m_LocMVP, 1, GL_FALSE, glm::value_ptr(mvp));
        }

        glMesh->Bind();

        const GLenum mode = ToGLTopology(cmd.m_Topology);
        const GLenum glIndexType = ToGLIndexType(cmd.m_IndexType);

        const size_t offsetBytes = static_cast<size_t>(cmd.m_FirstIndex) * IndexTypeSize(cmd.m_IndexType);
        const void* indexOffsetPtr = reinterpret_cast<const void*>(offsetBytes);

        // BaseVertex + instancing
        if (cmd.m_InstanceCount > 1) {
            if (cmd.m_VertexOffset != 0) {
                glDrawElementsInstancedBaseVertex(
                    mode,
                    static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType,
                    indexOffsetPtr,
                    static_cast<GLsizei>(cmd.m_InstanceCount),
                    cmd.m_VertexOffset
                );
            } else {
                glDrawElementsInstanced(
                    mode,
                    static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType,
                    indexOffsetPtr,
                    static_cast<GLsizei>(cmd.m_InstanceCount)
                );
            }
        } else {
            if (cmd.m_VertexOffset != 0) {
                glDrawElementsBaseVertex(
                    mode,
                    static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType,
                    indexOffsetPtr,
                    cmd.m_VertexOffset
                );
            } else {
                glDrawElements(
                    mode,
                    static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType,
                    indexOffsetPtr
                );
            }
        }

        glMesh->Unbind();
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL
