#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "Renderer/RHI/RHI_Renderer.h"
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
        void Render() override;
        void EndFrame() override;

        GLuint GetProgram() const { return m_Program; }

        //TODO add framebuffer field

        //TODO GetViewportTextureID()
    private:
        GLuint m_Program{ 0 };
        //GLuint m_EmptyVAO{ 0 };
    };

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_RENDERER_H