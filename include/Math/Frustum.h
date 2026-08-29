#ifndef FRUSTUM_H
#define FRUSTUM_H

#include <glm/glm.hpp>

#include "Api.h"
#include "Math/AABB.h"
#include "Math/Plane.h"

namespace Nova::Core::Math {

	enum class FrustumPlane : int {
		Left = 0,
		Right,
		Bottom,
		Top,
		Near,
		Far,
		Count
	};

	struct NV_API Frustum {
		Plane m_Planes[static_cast<int>(FrustumPlane::Count)];

		/** Extract frustum planes from a view-projection matrix (row-major glm layout). */
		static Frustum FromViewProjection(const glm::mat4& viewProj) {
			Frustum f{};
			const glm::mat4& m = viewProj;

			// Left:  row3 + row0
			f.m_Planes[0] = Plane(glm::vec3(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0]),
			                      m[3][3] + m[3][0]);
			// Right: row3 - row0
			f.m_Planes[1] = Plane(glm::vec3(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0]),
			                      m[3][3] - m[3][0]);
			// Bottom: row3 + row1
			f.m_Planes[2] = Plane(glm::vec3(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1]),
			                      m[3][3] + m[3][1]);
			// Top: row3 - row1
			f.m_Planes[3] = Plane(glm::vec3(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1]),
			                      m[3][3] - m[3][1]);
			// Near: row3 + row2 (works with ZO depth)
			f.m_Planes[4] = Plane(glm::vec3(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2]),
			                      m[3][3] + m[3][2]);
			// Far: row3 - row2
			f.m_Planes[5] = Plane(glm::vec3(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2]),
			                      m[3][3] - m[3][2]);

			for (auto& plane : f.m_Planes)
				plane.Normalize();

			return f;
		}

		/** True if the AABB is at least partially inside the frustum. */
		bool IntersectsAABB(const AABB& bounds) const {
			const glm::vec3 center = bounds.GetCenter();
			const glm::vec3 extents = bounds.GetExtents() * 0.5f;

			for (const auto& plane : m_Planes) {
				const glm::vec3 absNormal = glm::abs(plane.m_Normal);
				const float radius = glm::dot(extents, absNormal);
				const float distance = plane.SignedDistance(center);
				if (distance + radius < 0.0f)
					return false;
			}
			return true;
		}

		bool ContainsPoint(const glm::vec3& point) const {
			for (const auto& plane : m_Planes) {
				if (plane.SignedDistance(point) < 0.0f)
					return false;
			}
			return true;
		}
	};

} // namespace Nova::Core::Math

#endif // FRUSTUM_H