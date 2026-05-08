#include "Renderer/RHI/RHI_Renderer.h"

#include "Renderer/Backends/Vulkan/VK_Renderer.h"

#include <iostream>

namespace Nova::Core::Renderer::RHI {

    std::unique_ptr<IRenderer> IRenderer::Create(Core::GraphicsAPI api) {
        std::unique_ptr<IRenderer> renderer;

        switch (api) {
        case Core::GraphicsAPI::Vulkan:
            renderer = std::make_unique<Backends::Vulkan::VK_Renderer>();
            break;
        default:
            NV_LOG_ERROR("IRenderer::Create - unsupported graphics API");
            return nullptr;
        }

        if (!renderer->Create()) {
            NV_LOG_ERROR("IRenderer::Create - backend Create() failed");
            return nullptr;
        }

        return renderer;
    }

} // namespace Nova::Core::Renderer::RHI