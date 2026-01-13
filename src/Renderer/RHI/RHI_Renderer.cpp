#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/OpenGL/GL_Renderer.h"
#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include <iostream>

namespace Nova::Core::Renderer::RHI {

    std::unique_ptr<IRenderer> IRenderer::Create(Core::GraphicsAPI api) {
        std::unique_ptr<IRenderer> renderer;

        switch (api) {
        case Core::GraphicsAPI::OpenGL:
            renderer = std::make_unique<Backends::OpenGL::GL_Renderer>();
            break;
        case Core::GraphicsAPI::Vulkan:
            renderer = std::make_unique<Backends::Vulkan::VK_Renderer>();
            break;
        default:
            return nullptr;
        }

        if (!renderer->Create()) {
            return nullptr;
        }

        return renderer;
    }

} // namespace Nova::Core::Renderer::RHI