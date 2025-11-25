#ifndef NAMECOMPONENT_H
#define NAMECOMPONENT_H

#include <string>

namespace Nova::Core::Scene::ECS::Components {

	struct NameComponent {
		std::string m_Name;

		NameComponent() = default;
		NameComponent(const std::string& name) : m_Name(name) {}
	};
} // namespace Nova::Core::Scene::ECS::Components

#endif // NAMECOMPONENT_H