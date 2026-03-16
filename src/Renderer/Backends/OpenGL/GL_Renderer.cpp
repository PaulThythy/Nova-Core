#include "Renderer/Backends/OpenGL/GL_Renderer.h"
#include "Core/Application.h"
#include "Core/Assert.h"
#include "Core/Log.h"

#include <glad/gl.h>
#include <filesystem>
#include <string>
#include <cstdint>
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

    bool GL_Renderer::Create()
    {
        auto& window = Nova::Core::Application::Get().GetWindow();
        NV_ASSERT_MSG(window.GetGLContext(), "GL_Renderer::Create requires a valid OpenGL context.");
        if (!window.GetGLContext())
            return false;

        auto& imguiLayer = Nova::Core::Application::Get().GetImGuiLayer();
        imguiLayer.SetImGuiBackend(GraphicsAPI::OpenGL);

        //if (!glIsEnabled(GL_CONTEXT_CORE_PROFILE_BIT))
            //return false;
        glClipControl(GL_UPPER_LEFT, GL_ZERO_TO_ONE);

        glEnable(GL_FRAMEBUFFER_SRGB);

        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // The GL_UPPER_LEFT clip origin and the Y-flip in the projection matrix cancel each other out,
        // so front faces remain CCW in window space — same winding as default OpenGL.
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);

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

        NV_ASSERT_MSG(vert, "GL_Renderer::Create failed to acquire the vertex shader asset.");
        NV_ASSERT_MSG(frag, "GL_Renderer::Create failed to acquire the fragment shader asset.");
        if (!vert || !frag) return false;
        if (!vert->Compile()) {
            NV_ASSERT_MSG(false, "GL_Renderer::Create failed to compile the vertex shader.");
            NV_LOG_ERROR(("Vertex shader compile failed:\n" + vert->GetLastLog()).c_str());
            return false;
        }
        if (!frag->Compile()) {
            NV_ASSERT_MSG(false, "GL_Renderer::Create failed to compile the fragment shader.");
            NV_LOG_ERROR(("Fragment shader compile failed:\n" + frag->GetLastLog()).c_str());
            return false;
        }

        GLuint vs = 0, fs = 0;
        std::string log;

        vs = LoadSpirvShader(GL_VERTEX_SHADER, vert->GetSpirv(), "main", log);
        if (!vs) NV_LOG_WARN((std::string("SPIR-V VS failed, fallback GLSL: ") + log).c_str());
        NV_ASSERT_MSG(vs != 0, "GL_Renderer::Create failed to create the OpenGL vertex shader.");
        if (!vs) return false;

        fs = LoadSpirvShader(GL_FRAGMENT_SHADER, frag->GetSpirv(), "main", log);
        if (!fs) NV_LOG_WARN((std::string("SPIR-V FS failed, fallback GLSL: ") + log).c_str());
        NV_ASSERT_MSG(fs != 0, "GL_Renderer::Create failed to create the OpenGL fragment shader.");
        if (!fs) {
            glDeleteShader(vs);
            return false;
        }

        GLuint prog = glCreateProgram();
        NV_ASSERT_MSG(prog != 0, "GL_Renderer::Create failed to create the OpenGL program.");
        if (prog == 0) {
            glDeleteShader(vs);
            glDeleteShader(fs);
            return false;
        }
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

        m_Shader = std::make_unique<GL_Shaders>();
        NV_ASSERT_MSG(m_Shader, "GL_Renderer::Create failed to allocate GL_Shaders.");
        if (!m_Shader) {
            glDeleteProgram(prog);
            return false;
        }
        m_Shader->SetProgram(prog);

        // Defer framebuffer creation to the first Resize() call from the client.
        m_ViewportWidth = 0;
        m_ViewportHeight = 0;

        NV_LOG_INFO("OpenGL Renderer created. Triangle program linked from SPIR-V.");
        return true;
    }

    void GL_Renderer::Destroy() {
        GLuint prog = m_Shader ? static_cast<GLuint>(reinterpret_cast<uintptr_t>(m_Shader->GetNativeHandle())) : 0;
        m_Shader.reset();

        if (prog != 0) {
            glDeleteProgram(prog);
        }

        DestroyFramebuffer();

        glDisable(GL_DEPTH_TEST);
        glDisable(GL_BLEND);
        glDisable(GL_CULL_FACE);

        NV_LOG_INFO("OpenGL Renderer destroyed.");
    }

    bool GL_Renderer::Resize(int w, int h) {
        NV_ASSERT_MSG(w > 0 && h > 0, "GL_Renderer::Resize requires a strictly positive viewport size.");
        if (w <= 0 || h <= 0) return false;

        if (w == m_ViewportWidth && h == m_ViewportHeight && m_Framebuffer != 0)
            return true;

        DestroyFramebuffer();
        if (!CreateFramebuffer(w, h)) {
            NV_LOG_ERROR("GL_Renderer::Resize - failed to create framebuffer.");
            return false;
        }

        m_ViewportWidth = w;
        m_ViewportHeight = h;

        // The viewport texture may be displayed in ImGui before the next BeginFrame().
        // Clear it here so the user never sees undefined texture contents while resizing.
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
        glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);

        return true;
    }

    void GL_Renderer::Update(float dt) {
        (void)dt;
    }

    void GL_Renderer::BeginFrame() {
        // If no offscreen framebuffer is available yet, fall back to the default framebuffer.
        if (m_Framebuffer != 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
            glViewport(0, 0, m_ViewportWidth, m_ViewportHeight);
        } else {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
        }

        glClearColor(1.0f, 1.0f, 1.0f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    }

    void GL_Renderer::EndFrame() {
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void GL_Renderer::BeginScene(const glm::mat4& view, const glm::mat4& proj) {
        NV_ASSERT_MSG(m_Shader && m_Shader->IsValid(), "GL_Renderer::BeginScene requires a valid shader.");
        if (!m_Shader || !m_Shader->IsValid()) return;

        m_Shader->SetParameter("view", view);
        m_Shader->SetParameter("proj", proj);
    }

    void GL_Renderer::SetModelMatrix(const glm::mat4& model) {
        NV_ASSERT_MSG(m_Shader && m_Shader->IsValid(), "GL_Renderer::SetModelMatrix requires a valid shader.");
        if (!m_Shader || !m_Shader->IsValid()) return;
        m_Shader->SetParameter("model", model);
    }

    void GL_Renderer::Draw(const RHI::RHI_DrawCommand& cmd) {
        NV_ASSERT_MSG(m_Shader && m_Shader->IsValid(), "GL_Renderer::Draw requires a valid shader.");
        NV_ASSERT_MSG(cmd.m_Mesh, "GL_Renderer::Draw received a null mesh.");
        if (!m_Shader || !m_Shader->IsValid() || !cmd.m_Mesh) return;

        auto glMesh = std::dynamic_pointer_cast<GL_Mesh>(cmd.m_Mesh);
        NV_ASSERT_MSG(glMesh, "GL_Renderer::Draw expected a GL_Mesh instance.");
        if (!glMesh) {
            NV_LOG_WARN("GL_Renderer::Draw - mesh is not a GL_Mesh");
            return;
        }

        m_Shader->Bind(nullptr);
        m_Shader->ApplyParameters(nullptr);

        glMesh->Bind();

        const GLenum mode = ToGLTopology(cmd.m_Topology);
        if (cmd.m_InstanceCount > 1) {
            glDrawArraysInstanced(mode,
                static_cast<GLint>(cmd.m_FirstVertex),
                static_cast<GLsizei>(cmd.m_VertexCount),
                static_cast<GLsizei>(cmd.m_InstanceCount));
        }
        else {
            glDrawArrays(mode,
                static_cast<GLint>(cmd.m_FirstVertex),
                static_cast<GLsizei>(cmd.m_VertexCount));
        }

        glMesh->Unbind();

        // Return to default framebuffer so ImGui can render its own draw data.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    bool GL_Renderer::CreateFramebuffer(int width, int height) {
        NV_ASSERT_MSG(width > 0 && height > 0, "GL_Renderer::CreateFramebuffer requires positive dimensions.");
        if (width <= 0 || height <= 0)
            return false;

        glGenFramebuffers(1, &m_Framebuffer);
        NV_ASSERT_MSG(m_Framebuffer != 0, "GL_Renderer::CreateFramebuffer failed to allocate an FBO.");
        if (m_Framebuffer == 0)
            return false;
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);

        // Color texture
        glGenTextures(1, &m_ColorAttachment);
        NV_ASSERT_MSG(m_ColorAttachment != 0, "GL_Renderer::CreateFramebuffer failed to allocate the color attachment.");
        if (m_ColorAttachment == 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            DestroyFramebuffer();
            return false;
        }
        glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_SRGB8_ALPHA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorAttachment, 0);

        // Depth-stencil renderbuffer
        glGenRenderbuffers(1, &m_DepthAttachment);
        NV_ASSERT_MSG(m_DepthAttachment != 0, "GL_Renderer::CreateFramebuffer failed to allocate the depth attachment.");
        if (m_DepthAttachment == 0) {
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            DestroyFramebuffer();
            return false;
        }
        glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
        glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, width, height);
        glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthAttachment);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            NV_LOG_ERROR("GL_Renderer::CreateFramebuffer - framebuffer is incomplete.");
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            DestroyFramebuffer();
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void GL_Renderer::DestroyFramebuffer() {
        if (m_DepthAttachment != 0) {
            glDeleteRenderbuffers(1, &m_DepthAttachment);
            m_DepthAttachment = 0;
        }
        if (m_ColorAttachment != 0) {
            glDeleteTextures(1, &m_ColorAttachment);
            m_ColorAttachment = 0;
        }
        if (m_Framebuffer != 0) {
            glDeleteFramebuffers(1, &m_Framebuffer);
            m_Framebuffer = 0;
        }
    }

    void* GL_Renderer::GetViewportTextureID() const {
        if (m_ColorAttachment == 0) return nullptr;
        return reinterpret_cast<void*>(static_cast<intptr_t>(m_ColorAttachment));
    }

    void GL_Renderer::DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) {
        NV_ASSERT_MSG(m_Shader && m_Shader->IsValid(), "GL_Renderer::DrawIndexed requires a valid shader.");
        NV_ASSERT_MSG(cmd.m_Mesh, "GL_Renderer::DrawIndexed received a null mesh.");
        if (!m_Shader || !m_Shader->IsValid() || !cmd.m_Mesh) return;

        auto glMesh = std::dynamic_pointer_cast<GL_Mesh>(cmd.m_Mesh);
        NV_ASSERT_MSG(glMesh, "GL_Renderer::DrawIndexed expected a GL_Mesh instance.");
        if (!glMesh) {
            NV_LOG_WARN("GL_Renderer::DrawIndexed - mesh is not a GL_Mesh");
            return;
        }

        m_Shader->Bind(nullptr);
        m_Shader->ApplyParameters(nullptr);

        glMesh->Bind();

        const GLenum mode = ToGLTopology(cmd.m_Topology);
        const GLenum glIndexType = ToGLIndexType(cmd.m_IndexType);
        const size_t offsetBytes = static_cast<size_t>(cmd.m_FirstIndex) * IndexTypeSize(cmd.m_IndexType);
        const void* indexOffset = reinterpret_cast<const void*>(offsetBytes);

        if (cmd.m_InstanceCount > 1) {
            if (cmd.m_VertexOffset != 0) {
                glDrawElementsInstancedBaseVertex(mode, static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType, indexOffset, static_cast<GLsizei>(cmd.m_InstanceCount), cmd.m_VertexOffset);
            }
            else {
                glDrawElementsInstanced(mode, static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType, indexOffset, static_cast<GLsizei>(cmd.m_InstanceCount));
            }
        }
        else {
            if (cmd.m_VertexOffset != 0) {
                glDrawElementsBaseVertex(mode, static_cast<GLsizei>(cmd.m_IndexCount),
                    glIndexType, indexOffset, cmd.m_VertexOffset);
            }
            else {
                glDrawElements(mode, static_cast<GLsizei>(cmd.m_IndexCount), glIndexType, indexOffset);
            }
        }

        glMesh->Unbind();

        // Return to default framebuffer so ImGui can render its own draw data.
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL
