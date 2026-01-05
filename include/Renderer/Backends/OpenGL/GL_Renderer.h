#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include "Renderer/RHI/RHI_Renderer.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    class GL_Renderer : public RHI::IRenderer {
    public:
        GL_Renderer();
        ~GL_Renderer();

        bool Create() override;
        void Destroy() override;

        bool Resize(int w, int h) override;

        void Update(float dt) override;

        void BeginFrame() override;
        void Render() override;
        void EndFrame() override;

        //TODO add framebuffer field

        //TODO GetViewportTextureID()
    };

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_RENDERER_H