#ifndef RENDERER_H
#define RENDERER_H

namespace Nova::Renderer {

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

} // namespace Nova::Renderer

#endif // RENDERER_H