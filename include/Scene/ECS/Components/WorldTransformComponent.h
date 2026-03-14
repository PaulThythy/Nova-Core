#ifndef WORLDTRANSFORMCOMPONENT_H
#define WORLDTRANSFORMCOMPONENT_H

#include <glm/glm.hpp>

#include "Api.h"

namespace Nova::Core::Scene::ECS::Components {

	struct NV_API WorldTransformComponent {
		glm::mat4 m_World{ 1.0f };
	};

} // namespace Nova::Core::Scene::ECS::Components

#endif // WORLDTRANSFORMCOMPONENT_H