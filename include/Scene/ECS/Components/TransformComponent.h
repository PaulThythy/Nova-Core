#ifndef TRANSFORMCOMPONENT_H
#define TRANSFORMCOMPONENT_H

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace Nova::Core::Scene::ECS::Components {

	struct TransformComponent {
		glm::vec3 m_Translation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Rotation{ 0.0f, 0.0f, 0.0f };
		glm::vec3 m_Scale{ 1.0f, 1.0f, 1.0f };

		TransformComponent() = default;
		TransformComponent(const TransformComponent&) = default;
		TransformComponent(const glm::vec3& t, const glm::vec3& r, const glm::vec3& s) : m_Translation(t), m_Rotation(r), m_Scale(s) {}
		TransformComponent(const glm::vec3& t) : m_Translation(t) {}

		glm::mat4 GetTransform() const {
			glm::mat4 rotation = glm::toMat4(glm::quat(m_Rotation));

			return glm::translate(glm::mat4(1.0f), m_Translation)
				* rotation
				* glm::scale(glm::mat4(1.0f), m_Scale);
		}
	};

} // namespace Nova::Core::Scene::ECS::Components

#endif // TRANSFORMCOMPONENT_H