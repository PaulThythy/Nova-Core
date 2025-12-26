#ifndef RHI_RENDERER_H
#define RHI_RENDERER_H

#include <memory>

#include "Core/GraphicsAPI.h"

namespace Nova::Core::Renderer::RHI {

    class IRenderer {
    public:
        virtual ~IRenderer() = default;
        static std::unique_ptr<IRenderer> Create(Core::GraphicsAPI api);

        virtual bool Create() = 0;
        virtual void Destroy() = 0;

        virtual bool Resize(int w, int h) = 0;

        virtual void Update(float dt) = 0;

        virtual void BeginFrame() = 0;
        virtual void Render() = 0;
        virtual void EndFrame() = 0;

        //TODO get framebuffer, swapchain, etc.

        //TODO GetViewportTextureID()
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERER_H