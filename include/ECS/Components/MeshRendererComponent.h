#ifndef MESHRENDERERCOMPONENT_H
#define MESHRENDERERCOMPONENT_H

#include "Api.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

namespace Nova::Core::ECS::Components {

	/** GPU draw data. Mesh geometry comes from MeshComponent on the same entity. */
	struct NV_API MeshRendererComponent {
		Renderer::RHI::MaterialGPU m_Material{};

		MeshRendererComponent() = default;

		explicit MeshRendererComponent(const Renderer::RHI::MaterialGPU& material)
			: m_Material(material) {}
	};

} // namespace Nova::Core::ECS::Components

#endif // MESHRENDERERCOMPONENT_H