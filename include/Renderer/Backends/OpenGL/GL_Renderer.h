#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/Backends/OpenGL/GL_Shaders.h"

#include <glad/gl.h>

namespace Nova::Core::Renderer::Backends::OpenGL {

    class GL_Renderer final : public RHI::IRenderer {
    public:
        GL_Renderer() = default;
        ~GL_Renderer() override = default;

        bool Create() override;
        void Destroy() override;

        bool Resize(int w, int h) override;

        void Update(float dt) override;

        void BeginFrame() override;
        void EndFrame() override;

        void Draw(const RHI::RHI_DrawCommand& cmd) override;
        void DrawIndexed(const RHI::RHI_DrawIndexedCommand& cmd) override;

        // ImGui-compatible texture identifier for the viewport render target.
        void* GetViewportTextureID() const override;

        GLuint GetProgram() const { return m_Program; } 
    private:
        GLuint m_Program{ 0 };
        GLuint m_UBO_MVP{ 0 };

        // Offscreen framebuffer used as viewport render target.
        GLuint m_Framebuffer{ 0 };
        GLuint m_ColorAttachment{ 0 };
        GLuint m_DepthAttachment{ 0 };
        int m_ViewportWidth{ 0 };
        int m_ViewportHeight{ 0 };

        bool CreateFramebuffer(int width, int height);
        void DestroyFramebuffer();
    };

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_RENDERER_H