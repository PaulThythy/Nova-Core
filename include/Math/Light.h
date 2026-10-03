#ifndef LIGHT_H
#define LIGHT_H

#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "Api.h"

namespace Nova::Core::Math {

    enum class NV_API LightType {
        Directional = 0,
        Point = 1,
        Spot = 2
    };

    /** Local beam axis for Directional / Spot: +X so identity rotation shines along +X. */
    inline glm::vec3 LightLocalBeamAxis() { return glm::vec3(1.0f, 0.0f, 0.0f); }

    /**
     * Travel direction from TransformComponent::m_Rotation (Euler radians, XYZ).
     * Converts Euler → quaternion first so the beam axis is rotated without gimbal lock.
     * Used by Directional and Spot lights.
     */
    inline glm::vec3 LightTravelDirectionFromRotation(const glm::vec3& eulerRadians) {
        const glm::quat q = glm::normalize(glm::quat(eulerRadians));
        const glm::vec3 dir = q * LightLocalBeamAxis();
        const float len2 = glm::dot(dir, dir);
        return len2 > 1e-12f ? (dir / std::sqrt(len2)) : LightLocalBeamAxis();
    }

    /**
     * Factor in [minFactor, 1]: more top-down → 1, more grazing → minFactor.
     * Used to shrink depth bias when the light leans sideways (reduces ground peter-panning).
     */
    inline float ShadowAngleBiasFactor(const glm::vec3& travelDirection, float minFactor = 0.35f) {
        const float len2 = glm::dot(travelDirection, travelDirection);
        if (len2 < 1e-12f)
            return 1.0f;
        const float upAmount = std::abs(travelDirection.y) / std::sqrt(len2);
        return std::max(upAmount, minFactor);
    }

    struct NV_API Light {
        LightType m_Type = LightType::Directional;
        glm::vec3 m_Color = glm::vec3(1.0f);
        float m_Intensity = 1.0f;
        bool m_LightShadow = false;

        // Specific, ignored if not relevant
        float     m_Range{ 10.0f };         // Spot / Point
        float     m_InnerCone{ 15.0f };     // Spot, degrees
        float     m_OuterCone{ 25.0f };     // Spot, degrees

        /**
         * Shadow bias (per light). With front-face cull, keep these modest.
         * Constant/slope feed the shadow-pass rasterizer; normal bias offsets the receiver sample.
         * Values are also scaled by light angle so side lighting does not detach contact shadows.
         * Spot (perspective) needs lower values than Directional (ortho) — see AppLayer defaults.
         */
        float m_ShadowBiasConstant{ 0.5f };
        float m_ShadowBiasSlope{ 1.0f };
        float m_ShadowNormalBias{ 0.012f };

        // Helpers
        float InnerCos() const { return std::cos(glm::radians(m_InnerCone)); }
        float OuterCos() const { return std::cos(glm::radians(m_OuterCone)); }
    };

    /** Build light-space view-projection for shadow mapping (Directional = ortho, Spot = perspective).
     *  Same Vulkan convention as Camera::GetProjectionMatrix: RH, ZO depth, Y-flip. */
    inline glm::mat4 BuildLightViewProj(
        LightType type,
        const glm::vec3& position,
        const glm::vec3& direction,
        float range,
        float outerConeDegrees,
        float orthoHalfExtent = 10.0f)
    {
        const glm::vec3 dir = glm::normalize(direction);
        const glm::vec3 up = (std::abs(dir.y) > 0.99f) ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);

        glm::mat4 view{ 1.0f };
        glm::mat4 proj{ 1.0f };

        if (type == LightType::Directional) {
            const glm::vec3 eye = -dir * orthoHalfExtent;
            view = glm::lookAtRH(eye, glm::vec3(0.0f), up);
            proj = glm::orthoRH_ZO(
                -orthoHalfExtent, orthoHalfExtent,
                -orthoHalfExtent, orthoHalfExtent,
                0.1f, orthoHalfExtent * 2.0f);
        } else if (type == LightType::Spot) {
            view = glm::lookAtRH(position, position + dir, up);
            const float fov = glm::radians(std::max(outerConeDegrees * 2.0f, 1.0f));
            proj = glm::perspectiveRH_ZO(fov, 1.0f, 0.1f, std::max(range, 0.5f));
        } else {
            return glm::mat4(1.0f);
        }

        // Match Camera: Vulkan NDC Y is down.
        proj[1][1] *= -1.0f;
        return proj * view;
    }

} // namespace Nova::Core::Math

#endif // LIGHT_H