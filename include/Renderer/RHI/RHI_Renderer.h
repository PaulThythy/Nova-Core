#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

namespace Nova::Core::Renderer::RHI {

    class IRenderer {
    public:
        virtual ~IRenderer() = default;

        virtual bool Create() = 0;
        virtual void Destroy() = 0;

        virtual bool Resize(int w, int h) = 0;

        virtual void Update(float dt) = 0;

        virtual void BeginFrame() = 0;
        virtual void Render() = 0;
        virtual void EndFrame() = 0;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H