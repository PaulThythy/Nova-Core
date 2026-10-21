#ifndef MESHCOMPONENT_H
#define MESHCOMPONENT_H

#include "Api.h"
#include "Asset/AssetHandle.h"
#include "Asset/AssetManager.h"
#include "Asset/Assets/MeshAsset.h"
#include "Math/AABB.h"

namespace Nova::Core::ECS::Components {

	struct NV_API MeshComponent {
		Asset::AssetHandle<Asset::Assets::MeshAsset> m_Mesh;
		uint32_t m_AABBTreeDepth = 4;
		Math::AABBTree m_AABBTree;

		MeshComponent() = default;

		explicit MeshComponent(Asset::AssetHandle<Asset::Assets::MeshAsset> mesh)
			: m_Mesh(mesh) {
			if (auto asset = Asset::AssetManager::Get().GetAsset(m_Mesh))
				m_AABBTreeDepth = asset->GetDesc().m_AABBTreeDepth;
			RebuildAABBTree();
		}

		void RebuildAABBTree() {
			m_AABBTree = Math::AABBTree{};
			auto asset = Asset::AssetManager::Get().GetAsset(m_Mesh);
			if (!asset || !asset->IsLoaded())
				return;

			auto cpuMesh = asset->GetCPUMesh();
			if (!cpuMesh)
				return;

			m_AABBTree.Build(
				cpuMesh->GetVertices(), cpuMesh->GetVertexCount(),
				cpuMesh->GetIndices(), cpuMesh->GetIndexCount(),
				m_AABBTreeDepth);
		}
	};

} // namespace Nova::Core::ECS::Components

#endif // MESHCOMPONENT_H