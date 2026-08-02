#ifndef IDCOMPONENT_H
#define IDCOMPONENT_H

#include "Api.h"
#include "Core/UUID.h"

namespace Nova::Core::ECS::Components {

	struct NV_API IDComponent {
		UUID m_ID = 0;

		IDComponent() = default;
		IDComponent(UUID id) : m_ID(id) {}
	};
} // namespace Nova::Core::ECS::Components

#endif // IDCOMPONENT_H