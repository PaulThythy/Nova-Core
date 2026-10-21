#ifndef MESHRENDERERCOMPONENT_H
#define MESHRENDERERCOMPONENT_H

#include "Api.h"
#include "Asset/AssetHandle.h"
#include "Asset/Assets/MeshAsset.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

namespace Nova::Core::ECS::Components {

	struct NV_API MeshRendererComponent {
		Asset::AssetHandle<Asset::Assets::MeshAsset> m_Mesh;
		Renderer::RHI::MaterialGPU m_Material{};

		MeshRendererComponent() = default;

		explicit MeshRendererComponent(Asset::AssetHandle<Asset::Assets::MeshAsset> mesh)
			: m_Mesh(mesh) {}

		MeshRendererComponent(Asset::AssetHandle<Asset::Assets::MeshAsset> mesh, const Renderer::RHI::MaterialGPU& material)
			: m_Mesh(mesh), m_Material(material) {}
	};

} // namespace Nova::Core::ECS::Components

#endif // MESHRENDERERCOMPONENT_H