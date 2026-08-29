#ifndef PLANE_H
#define PLANE_H

#include <cmath>

#include <glm/glm.hpp>

#include "Api.h"
#include "Math/Ray.h"

namespace Nova::Core::Math {

	/** Plane equation: dot(m_Normal, P) + m_Distance = 0. Normal should be unit length. */
	struct NV_API Plane {
		glm::vec3 m_Normal{ 0.0f, 1.0f, 0.0f };
		float m_Distance = 0.0f;

		Plane() = default;

		Plane(const glm::vec3& normal, float distance)
			: m_Normal(normal), m_Distance(distance) {}

		Plane(const glm::vec3& normal, const glm::vec3& point)
			: m_Normal(glm::normalize(normal)), m_Distance(-glm::dot(m_Normal, point)) {}

		Plane(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c) {
			m_Normal = glm::normalize(glm::cross(b - a, c - a));
			m_Distance = -glm::dot(m_Normal, a);
		}

		float SignedDistance(const glm::vec3& point) const {
			return glm::dot(m_Normal, point) + m_Distance;
		}

		void Normalize() {
			const float len = glm::length(m_Normal);
			if (len > 1e-8f) {
				const float inv = 1.0f / len;
				m_Normal *= inv;
				m_Distance *= inv;
			}
		}

		/** Returns true if the ray hits the front face (or either side if allowBackface). */
		bool IntersectRay(const Ray& ray, float& outT, bool allowBackface = true) const {
			const float denom = glm::dot(m_Normal, ray.m_Direction);
			if (std::fabs(denom) < 1e-8f)
				return false;
			if (!allowBackface && denom > 0.0f)
				return false;

			outT = -(glm::dot(m_Normal, ray.m_Origin) + m_Distance) / denom;
			return outT >= 0.0f;
		}
	};

} // namespace Nova::Core::Math

#endif // PLANE_H