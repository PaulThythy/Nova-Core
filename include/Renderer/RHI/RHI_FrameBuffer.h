#ifndef RHI_FRAMEBUFFER_H
#define RHI_FRAMEBUFFER_H

namespace Nova::Core::Renderer::RHI {

    class IFrameBuffer {
    public:
        virtual ~IFrameBuffer() = default;

        virtual bool Create(int width, int height) = 0;
        virtual void Release() = 0;

        virtual void Bind() const = 0;
        virtual void Unbind() const = 0;
        virtual void Resize(int width, int height) = 0;

        virtual void Invalidate() = 0;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_FRAMEBUFFER_H