#ifndef RAY_H
#define RAY_H

#include <glm/glm.hpp>

#include "Api.h"

namespace Nova::Core::Math {

	struct NV_API Ray {
		glm::vec3 m_Origin{ 0.0f };
		glm::vec3 m_Direction{ 0.0f, 0.0f, -1.0f }; // Expected unit length for distance queries.

		Ray() = default;

		Ray(const glm::vec3& origin, const glm::vec3& direction) : m_Origin(origin), m_Direction(glm::normalize(direction)) {}

		glm::vec3 At(float t) const {
			return m_Origin + m_Direction * t;
		}
	};

} // namespace Nova::Core::Math

#endif // RAY_H