#ifndef LIGHTCOMPONENT_H
#define LIGHTCOMPONENT_H

#include <memory>

#include "Api.h"
#include "Renderer/Graphics/Light.h"

namespace Nova::Core::ECS::Components {

    struct NV_API LightComponent {
        std::shared_ptr<Renderer::Graphics::Light> m_Light;

        LightComponent() = default;

        LightComponent(const std::shared_ptr<Renderer::Graphics::Light>& light) : m_Light(light) {}
    };

} // namespace Nova::Core::ECS::Components

#endif // LIGHTCOMPONENT_H