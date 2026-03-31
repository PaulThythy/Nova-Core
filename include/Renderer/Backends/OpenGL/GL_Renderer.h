#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <memory>
#include <glad/gl.h>

#include "Api.h"
#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/Backends/OpenGL/GL_Shaders.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    class NV_API GL_Renderer final : public RHI::IRenderer {
    public:
        GL_Renderer() = default;
        ~GL_Renderer() override = default;

        bool Create() override;
        void Destroy() override;

        bool Resize(int w, int h) override;

        void Update(float dt) override;

        void BeginFrame() override;
        void EndFrame() override;

        void BeginScene(const glm::mat4& view, const glm::mat4& proj) override;
        void SetModelMatrix(const glm::mat4& model) override;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        // ImGui-compatible texture identifier for the viewport render target.
        void* GetViewportTextureID() const override;

        RHI::RHI_Shaders* GetShader() override { return m_Shader.get(); }
        GLuint GetProgram() const { return m_Shader ? static_cast<GLuint>(reinterpret_cast<uintptr_t>(m_Shader->GetNativeHandle())) : 0; }

        RHI::RHI_Shaders* CreateFullscreenShader(
            const std::vector<uint32_t>& vertSpirv,
            const std::vector<uint32_t>& fragSpirv) override;
        void DestroyFullscreenShader(RHI::RHI_Shaders* shader) override;
        void DrawFullscreen(RHI::RHI_Shaders* shader) override;

    private:
        std::unique_ptr<GL_Shaders> m_Shader;

        // Offscreen framebuffer used as viewport render target.
        GLuint m_Framebuffer{ 0 };
        GLuint m_ColorAttachment{ 0 };
        GLuint m_DepthAttachment{ 0 };
        int m_ViewportWidth{ 0 };
        int m_ViewportHeight{ 0 };

        bool CreateFramebuffer(int width, int height);
        void DestroyFramebuffer();

        GLuint m_EmptyVAO{ 0 };
    };

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_RENDERER_H