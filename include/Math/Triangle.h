#ifndef TRIANGLE_H
#define TRIANGLE_H

#include <cmath>

#include <glm/glm.hpp>

#include "Api.h"
#include "Math/Ray.h"

namespace Nova::Core::Math {

	struct NV_API Triangle {
		glm::vec3 m_V0{ 0.0f };
		glm::vec3 m_V1{ 0.0f };
		glm::vec3 m_V2{ 0.0f };

		Triangle() = default;

		Triangle(const glm::vec3& v0, const glm::vec3& v1, const glm::vec3& v2)
			: m_V0(v0), m_V1(v1), m_V2(v2) {}

		glm::vec3 GetNormal() const {
			return glm::normalize(glm::cross(m_V1 - m_V0, m_V2 - m_V0));
		}

		glm::vec3 GetCentroid() const {
			return (m_V0 + m_V1 + m_V2) / 3.0f;
		}

		/**
		 * Möller–Trumbore ray-triangle intersection.
		 * outT is distance along the ray (requires unit direction).
		 * outU / outV are barycentric coordinates (w = 1 - u - v).
		 */
		bool IntersectRay(const Ray& ray, float& outT, float& outU, float& outV, bool cullBackface = false) const {
			constexpr float kEpsilon = 1e-8f;

			const glm::vec3 edge1 = m_V1 - m_V0;
			const glm::vec3 edge2 = m_V2 - m_V0;
			const glm::vec3 pvec = glm::cross(ray.m_Direction, edge2);
			const float det = glm::dot(edge1, pvec);

			if (cullBackface) {
				if (det < kEpsilon)
					return false;
			} else {
				if (std::fabs(det) < kEpsilon)
					return false;
			}

			const float invDet = 1.0f / det;
			const glm::vec3 tvec = ray.m_Origin - m_V0;
			outU = glm::dot(tvec, pvec) * invDet;
			if (outU < 0.0f || outU > 1.0f)
				return false;

			const glm::vec3 qvec = glm::cross(tvec, edge1);
			outV = glm::dot(ray.m_Direction, qvec) * invDet;
			if (outV < 0.0f || outU + outV > 1.0f)
				return false;

			outT = glm::dot(edge2, qvec) * invDet;
			return outT >= 0.0f;
		}

		bool IntersectRay(const Ray& ray, float& outT, bool cullBackface = false) const {
			float u = 0.0f, v = 0.0f;
			return IntersectRay(ray, outT, u, v, cullBackface);
		}
	};

} // namespace Nova::Core::Math

#endif // TRIANGLE_H