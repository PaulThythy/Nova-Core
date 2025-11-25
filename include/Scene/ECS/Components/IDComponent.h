#ifndef IDCOMPONENT_H
#define IDCOMPONENT_H

#include "Core/UUID.h"

namespace Nova::Core::Scene::ECS::Components {

	struct IDComponent {
		UUID m_ID = 0;

		IDComponent() = default;
		IDComponent(UUID id) : m_ID(id) {}
	};
} // namespace Nova::Core::Scene::ECS::Components

#endif // IDCOMPONENT_H