#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include "Api.h"
#include "Asset/Assets/MeshAsset.h"

namespace Nova::Core::ECS::Components {

	struct NV_API MeshComponent {
		std::shared_ptr<Asset::Assets::MeshAsset> m_MeshAsset;

		MeshComponent() = default;
		explicit MeshComponent(const std::shared_ptr<Asset::Assets::MeshAsset>& meshAsset)
			: m_MeshAsset(meshAsset) {}
	};

} // namespace Nova::Core::ECS::Components

#endif // MESHCOMPONENT_H
