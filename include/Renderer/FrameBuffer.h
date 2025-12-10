#ifndef FRAMEBUFFER_H
#define FRAMEBUFFER_H

namespace Nova::Core::Renderer {

    class FrameBuffer {
    public:
        virtual ~FrameBuffer() = default;

        virtual bool Create(int width, int height) = 0;
        virtual void Release() = 0;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual void Resize(int width, int height) = 0;

        virtual void Invalidate() = 0;
    };

} // namespace Nova::Core::Renderer

#endif // FRAMEBUFFER_H