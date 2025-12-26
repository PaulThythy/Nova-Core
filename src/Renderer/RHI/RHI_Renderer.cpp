#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/OpenGL/GL_Renderer.h"

namespace Nova::Core::Renderer::RHI {

    std::unique_ptr<IRenderer> IRenderer::Create(Core::GraphicsAPI api) {
        switch (api) {
        case Core::GraphicsAPI::OpenGL:
            return std::make_unique<Backends::OpenGL::GL_Renderer>();

        default:
            return nullptr;
        }
    }

} // namespace Nova::Core::Renderer::RHI