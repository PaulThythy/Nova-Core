#include "Scene/SceneQuery.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <unordered_map>
#include <vector>

#include <glm/gtc/matrix_inverse.hpp>

#include "Asset/AssetManager.h"
#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/MeshRendererComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Math/AABB.h"
#include "Scene/Scene.h"

namespace Nova::Core::Scene {

	size_t HashMaterial(const Renderer::RHI::MaterialGPU& material) {
		const auto* bytes = reinterpret_cast<const uint8_t*>(&material);
		size_t hash = 14695981039346656037ull;
		for (size_t i = 0; i < sizeof(Renderer::RHI::MaterialGPU); ++i) {
			hash ^= static_cast<size_t>(bytes[i]);
			hash *= 1099511628211ull;
		}
		return hash;
	}

	uint32_t FindOrAddMaterial(
		std::vector<Renderer::RHI::MaterialGPU>& materials,
		std::unordered_map<size_t, std::vector<uint32_t>>& byHash,
		const Renderer::RHI::MaterialGPU& material)
	{
		const size_t hash = HashMaterial(material);
		auto& bucket = byHash[hash];
		for (uint32_t index : bucket) {
			if (std::memcmp(&materials[index], &material, sizeof(Renderer::RHI::MaterialGPU)) == 0)
				return index;
		}
		const uint32_t index = static_cast<uint32_t>(materials.size());
		materials.push_back(material);
		bucket.push_back(index);
		return index;
	}

	Math::Ray ScreenPointToRay(const Math::Camera& camera, float u, float v) {
		const float ndcX = u * 2.0f - 1.0f;
		const float ndcY = v * 2.0f - 1.0f;

		const glm::mat4 invViewProj = glm::inverse(camera.GetProjectionMatrix() * camera.GetViewMatrix());

		auto unproject = [&](float ndcZ) {
			glm::vec4 world = invViewProj * glm::vec4(ndcX, ndcY, ndcZ, 1.0f);
			world /= world.w;
			return glm::vec3(world);
		};

		// Depth [0,1] (RH_ZO): near = 0, far = 1.
		const glm::vec3 nearPoint = unproject(0.0f);
		const glm::vec3 farPoint = unproject(1.0f);
		return Math::Ray(nearPoint, farPoint - nearPoint);
	}

	bool Raycast(Scene& scene, const Math::Ray& worldRay, RaycastHit& outHit, float maxDistance) {
		auto& registry = scene.GetRegistry();
		auto view = registry.view<ECS::Components::TransformComponent, ECS::Components::MeshComponent>();

		bool anyHit = false;
		float closestDistance = maxDistance;
		RaycastHit best{};

		for (auto entity : view) {
			const auto& mc = view.get<ECS::Components::MeshComponent>(entity);

			auto meshAsset = Asset::AssetManager::Get().GetAsset(mc.m_Mesh);
			if (!mc.m_AABBTree.IsBuilt() || !meshAsset || !meshAsset->IsLoaded())
				continue;

			auto cpuMesh = meshAsset->GetCPUMesh();
			if (!cpuMesh)
				continue;

			const glm::mat4 model = scene.GetWorldTransform(entity);
			const glm::mat4 invModel = glm::inverse(model);

			// Transform the ray into local mesh space (direction may be non-unit under non-uniform scale).
			const glm::vec3 localOrigin = glm::vec3(invModel * glm::vec4(worldRay.m_Origin, 1.0f));
			const glm::vec3 localDir = glm::vec3(invModel * glm::vec4(worldRay.m_Direction, 0.0f));
			const float localDirLength = glm::length(localDir);
			if (localDirLength < 1e-8f)
				continue;

			const Math::Ray localRay(localOrigin, localDir / localDirLength);

			Math::AABBTreeHit localHit{};
			const float localMaxT = closestDistance * localDirLength;
			if (!mc.m_AABBTree.Raycast(
					localRay,
					cpuMesh->GetVertices(), cpuMesh->GetVertexCount(),
					cpuMesh->GetIndices(), cpuMesh->GetIndexCount(),
					localHit, localMaxT)) {
				continue;
			}

			const glm::vec3 worldPoint = glm::vec3(model * glm::vec4(localHit.m_Point, 1.0f));
			const float worldDistance = glm::dot(worldPoint - worldRay.m_Origin, worldRay.m_Direction);
			if (worldDistance < 0.0f || worldDistance >= closestDistance)
				continue;

			const glm::mat3 normalMatrix = glm::transpose(glm::mat3(invModel));
			glm::vec3 worldNormal = glm::normalize(normalMatrix * localHit.m_Normal);

			closestDistance = worldDistance;
			best.m_Entity = entity;
			best.m_Distance = worldDistance;
			best.m_Point = worldPoint;
			best.m_Normal = worldNormal;
			best.m_TriangleIndex = localHit.m_TriangleIndex;
			anyHit = true;
		}

		if (!anyHit)
			return false;

		outHit = best;
		return true;
	}

	SceneRenderBatches BuildRenderBatches(Scene& scene, uint32_t firstInstance) {
		SceneRenderBatches out;

		auto& registry = scene.GetRegistry();
		auto view = registry.view<ECS::Components::MeshRendererComponent, ECS::Components::MeshComponent>();

		std::unordered_map<UUID, uint32_t> batchByMesh;
		std::unordered_map<size_t, std::vector<uint32_t>> materialsByHash;
		std::vector<uint32_t> instanceBatch;
		uint32_t lastBatch = 0;

		for (auto entity : view) {
			const auto& mrc = view.get<ECS::Components::MeshRendererComponent>(entity);
			const auto& mc = view.get<ECS::Components::MeshComponent>(entity);
			auto meshAsset = Asset::AssetManager::Get().GetAsset(mc.m_Mesh);
			if (!meshAsset || !meshAsset->IsLoaded())
				continue;

			auto cpuMesh = meshAsset->GetCPUMesh();
			if (!cpuMesh)
				continue;

			const UUID meshID = mc.m_Mesh.GetUUID();
			const uint32_t matIndex = FindOrAddMaterial(out.m_Materials, materialsByHash, mrc.m_Material);

			if (out.m_Batches.empty() || out.m_Batches[lastBatch].m_MeshID != meshID) {
				const auto [it, inserted] = batchByMesh.try_emplace(meshID, static_cast<uint32_t>(out.m_Batches.size()));
				if (inserted)
					out.m_Batches.push_back({ meshID, cpuMesh, 0, 0 });
				lastBatch = it->second;
			}
			++out.m_Batches[lastBatch].m_InstanceCount;

			Renderer::RHI::InstanceGPU& instance = out.m_Instances.emplace_back();
			instance.m_Model = scene.GetWorldTransform(entity);
			instance.m_MaterialIndex = matIndex;
			instanceBatch.push_back(lastBatch);
		}

		uint32_t offset = firstInstance;
		for (auto& batch : out.m_Batches) {
			batch.m_FirstInstance = offset;
			offset += batch.m_InstanceCount;
		}

		if (std::is_sorted(instanceBatch.begin(), instanceBatch.end()))
			return out;

		std::vector<uint32_t> cursors(out.m_Batches.size());
		for (size_t b = 0; b < out.m_Batches.size(); ++b)
			cursors[b] = out.m_Batches[b].m_FirstInstance - firstInstance;

		std::vector<Renderer::RHI::InstanceGPU> scratchInstances(out.m_Instances.size());
		std::vector<uint32_t> scratchBatch(instanceBatch.size());
		for (size_t i = 0; i < out.m_Instances.size(); ++i) {
			const uint32_t batch = instanceBatch[i];
			const uint32_t dst = cursors[batch]++;
			scratchInstances[dst] = out.m_Instances[i];
			scratchBatch[dst] = batch;
		}
		out.m_Instances.swap(scratchInstances);
		return out;
	}

} // namespace Nova::Core::Scene