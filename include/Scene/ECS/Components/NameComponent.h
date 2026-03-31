#ifndef NAMECOMPONENT_H
#define NAMECOMPONENT_H

#include <string>

#include "Api.h"

namespace Nova::Core::Scene::ECS::Components {

	struct NV_API NameComponent {
		std::string m_Name;

		NameComponent() = default;
		NameComponent(const std::string& name) : m_Name(name) {}
	};
} // namespace Nova::Core::Scene::ECS::Components

#endif // NAMECOMPONENT_H