#include "Renderer/Backends/OpenGL/GL_FrameBuffer.h"

#include <glad/gl.h>
#include <iostream>

namespace Nova::Core::Renderer::Backends::OpenGL {

    bool GL_FrameBuffer::Create(int width, int height) {

        if (width <= 0 || height <= 0) {
            Release();
            return false;
        }

        Release();

        m_Width  = width;
        m_Height = height;

        glGenFramebuffers(1, &m_Framebuffer);
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);

        glGenTextures(1, &m_ColorAttachment);
        glBindTexture(GL_TEXTURE_2D, m_ColorAttachment);
        glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,static_cast<GLsizei>(m_Width),static_cast<GLsizei>(m_Height),0,GL_RGBA,GL_UNSIGNED_BYTE,nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,m_ColorAttachment,0);

        glGenRenderbuffers(1, &m_DepthAttachment);
        glBindRenderbuffer(GL_RENDERBUFFER, m_DepthAttachment);
        glRenderbufferStorage(GL_RENDERBUFFER,GL_DEPTH24_STENCIL8,static_cast<GLsizei>(m_Width),static_cast<GLsizei>(m_Height));
        glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_RENDERBUFFER,m_DepthAttachment);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "GL_FrameBuffer::Create - Framebuffer incomplete !" << std::endl;
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            Release();
            return false;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return true;
    }

    void GL_FrameBuffer::Release() {
        if (m_DepthAttachment) {
            glDeleteRenderbuffers(1, &m_DepthAttachment);
            m_DepthAttachment = 0;
        }
        if (m_ColorAttachment) {
            glDeleteTextures(1, &m_ColorAttachment);
            m_ColorAttachment = 0;
        }
        if (m_Framebuffer) {
            glDeleteFramebuffers(1, &m_Framebuffer);
            m_Framebuffer = 0;
        }

        m_Width  = 0;
        m_Height = 0;
    }

    void GL_FrameBuffer::Bind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, m_Framebuffer);
    }

    void GL_FrameBuffer::Unbind() const {
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void GL_FrameBuffer::Resize(int width, int height) {
        if (width <= 0 || height <= 0)
            return;

        if (width == m_Width && height == m_Height)
            return;

        Create(width, height);
    }

    void GL_FrameBuffer::Invalidate() {
        if (m_Width <= 0 || m_Height <= 0) {
            Release();
            return;
        }

        Create(m_Width, m_Height);
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL