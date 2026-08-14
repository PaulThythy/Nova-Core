#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include "Api.h"
#include "Asset/Assets/MeshAsset.h"
#include "Math/AABB.h"

namespace Nova::Core::ECS::Components {

	struct NV_API MeshComponent {
		std::shared_ptr<Asset::Assets::MeshAsset> m_MeshAsset;
		uint32_t m_AABBTreeDepth = 4;
		Math::AABBTree m_AABBTree;

		MeshComponent() = default;

		explicit MeshComponent(const std::shared_ptr<Asset::Assets::MeshAsset>& meshAsset)
			: m_MeshAsset(meshAsset) {
			if (m_MeshAsset)
				m_AABBTreeDepth = m_MeshAsset->GetDesc().m_AABBTreeDepth;
			RebuildAABBTree();
		}

		void RebuildAABBTree() {
			m_AABBTree = Math::AABBTree{};
			if (!m_MeshAsset || !m_MeshAsset->IsLoaded())
				return;

			auto cpuMesh = m_MeshAsset->GetCPUMesh();
			if (!cpuMesh)
				return;

			m_AABBTree.Build(cpuMesh->GetVertices(), cpuMesh->GetIndices(), m_AABBTreeDepth);
		}
	};

} // namespace Nova::Core::ECS::Components

#endif // MESHCOMPONENT_H
