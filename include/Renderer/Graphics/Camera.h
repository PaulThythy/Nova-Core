#ifndef CAMERA_H
#define CAMERA_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "Api.h"

namespace Nova::Core::Renderer::Graphics {

    struct NV_API Camera {
        Camera() = default;

        Camera(const glm::vec3& lookFrom, const glm::vec3& lookAt, const glm::vec3& up,
            float fov, float aspectRatio, float nearPlane, float farPlane, bool isPerspective = true) :

            m_LookFrom(lookFrom), m_LookAt(lookAt), m_Up(up), m_FOV(fov), m_AspectRatio(aspectRatio),
            m_NearPlane(nearPlane), m_FarPlane(farPlane), m_IsPerspective(isPerspective) {}

        glm::mat4 GetViewMatrix() const {
            return glm::lookAt(m_LookFrom, m_LookAt, m_Up);
        }

        // Unified convention (Vulkan / OpenGL+glClipControl / DX / Metal):
        // Right-handed, depth in [0,1], Y-flip so that +Y in world = top of the screen.
        glm::mat4 GetProjectionMatrix() const {
            glm::mat4 proj{1.0f};
            if (m_IsPerspective) {
                proj = glm::perspectiveRH_ZO(glm::radians(m_FOV), m_AspectRatio, m_NearPlane, m_FarPlane);
            } else {
                float halfHeight = m_FOV * 0.5f;
                float halfWidth  = halfHeight * m_AspectRatio;
                proj = glm::orthoRH_ZO(-halfWidth, halfWidth, -halfHeight, halfHeight, m_NearPlane, m_FarPlane);
            }
            proj[1][1] *= -1.0f;
            return proj;
        }

        glm::vec3 m_LookFrom{ 0.0f, 0.0f, 3.0f };
        glm::vec3 m_LookAt{ 0.0f, 0.0f, 0.0f };
        glm::vec3 m_Up{ 0.0f, 1.0f, 0.0f };

        float m_FOV         = 45.0f;            //in degree
        float m_AspectRatio = 16.0f / 9.0f;
        float m_NearPlane   = 0.1f;
        float m_FarPlane    = 100.0f;

        bool  m_IsPerspective = true;
    };

} // namespace Nova::Core::Renderer::Graphics

#endif // CAMERA_H