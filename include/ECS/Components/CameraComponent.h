#ifndef CAMERACOMPONENT_H
#define CAMERACOMPONENT_H

#include <memory>

#include "Api.h"
#include "Math/Camera.h"

namespace Nova::Core::ECS::Components {

    struct NV_API CameraComponent {
        std::shared_ptr<Math::Camera> m_Camera;
        bool m_IsPrimary = false;

        CameraComponent() = default;

        CameraComponent(const std::shared_ptr<Math::Camera>& camera, bool isPrimary = false)
            : m_Camera(camera), m_IsPrimary(isPrimary)
        {}
    };

} // namespace Nova::Core::ECS::Components

#endif // CAMERACOMPONENT_H