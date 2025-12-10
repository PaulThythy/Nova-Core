#ifndef GL_FRAMEBUFFER_H
#define GL_FRAMEBUFFER_H

#include <glad/gl.h>

#include "Renderer/FrameBuffer.h"

namespace Nova::Core::Renderer::OpenGL {

    class GL_FrameBuffer : public FrameBuffer {
    public:
        GL_FrameBuffer() = default;
        GL_FrameBuffer(int width, int height) {
            Create(width, height);
        }

        ~GL_FrameBuffer() override {
            Release();
        }

        bool Create(int width, int height) override;
        void Release() override;

        void Bind() const override;
        void Unbind() const override;

        void Resize(int width, int height) override;
        void Invalidate() override;

        GLuint GetColorAttachment() const { return m_ColorAttachment; }
        GLuint GetDepthAttachment() const { return m_DepthAttachment; }
        GLuint GetFramebufferID() const { return m_Framebuffer; }

        int GetWidth() const { return m_Width; }
        int GetHeight() const { return m_Height; }

    private:
        GLuint m_Framebuffer{ 0 };
        GLuint m_ColorAttachment{ 0 };
        GLuint m_DepthAttachment{ 0 };

        int m_Width{ 0 };
        int m_Height{ 0 };
    };

} // namespace Nova::Core::Renderer::OpenGL

#endif // GL_FRAMEBUFFER_H