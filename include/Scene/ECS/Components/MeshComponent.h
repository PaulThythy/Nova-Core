#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include "Renderer/Mesh.h"

namespace Nova::Core::Scene::ECS::Components {

	struct MeshComponent {
		std::shared_ptr<Renderer::Mesh> m_Mesh;

		MeshComponent() = default;
		explicit MeshComponent(const std::shared_ptr<Renderer::Mesh>& mesh)
			: m_Mesh(mesh) {}
	};

} // namespace Nova::Core::Scene::ECS::Components

#endif // MESHCOMPONENT_H