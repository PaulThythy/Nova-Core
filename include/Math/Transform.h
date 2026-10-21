#ifndef TRANSFORM_H
#define TRANSFORM_H

#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

#include "Api.h"

namespace Nova::Core::Math {

	/** Decompose a 4x4 affine transform into translation, rotation (quat), and scale. */
	inline void DecomposeMatrixToTRS(
		const glm::mat4& matrix,
		glm::vec3& outTranslation,
		glm::quat& outRotation,
		glm::vec3& outScale)
	{
		outTranslation = glm::vec3(matrix[3]);

		glm::vec3 column0 = glm::vec3(matrix[0]);
		glm::vec3 column1 = glm::vec3(matrix[1]);
		glm::vec3 column2 = glm::vec3(matrix[2]);

		outScale = glm::vec3(
			glm::length(column0),
			glm::length(column1),
			glm::length(column2));

		if (glm::determinant(glm::mat3(matrix)) < 0.0f)
			outScale.x = -outScale.x;

		if (std::abs(outScale.x) > 1e-8f) column0 /= outScale.x;
		if (std::abs(outScale.y) > 1e-8f) column1 /= outScale.y;
		if (std::abs(outScale.z) > 1e-8f) column2 /= outScale.z;

		outRotation = glm::normalize(glm::quat_cast(glm::mat3(column0, column1, column2)));
	}

} // namespace Nova::Core::Math

#endif // TRANSFORM_H