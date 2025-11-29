#ifndef CAMERACOMPONENT_H
#define CAMERACOMPONENT_H

#include <memory>

#include "Renderer/Camera.h"

namespace Nova::Core::Scene::ECS::Components {

    struct CameraComponent {
        std::shared_ptr<Renderer::Camera> m_Camera;
        bool m_IsPrimary = false;

        CameraComponent() = default;

        CameraComponent(const std::shared_ptr<Renderer::Camera>& camera,
                        bool isPrimary = false)
            : m_Camera(camera), m_IsPrimary(isPrimary)
        {}
    };

} // namespace Nova::Core::Scene::ECS::Components

#endif // CAMERACOMPONENT_H