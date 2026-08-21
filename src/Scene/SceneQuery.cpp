#include "Scene/SceneQuery.h"

#include <cmath>
#include <limits>

#include <glm/gtc/matrix_inverse.hpp>

#include "ECS/Components/MeshComponent.h"
#include "ECS/Components/TransformComponent.h"
#include "Math/AABB.h"
#include "Scene/Scene.h"

namespace Nova::Core::Scene {

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
			const auto& tc = view.get<ECS::Components::TransformComponent>(entity);
			const auto& mc = view.get<ECS::Components::MeshComponent>(entity);

			if (!mc.m_AABBTree.IsBuilt() || !mc.m_MeshAsset || !mc.m_MeshAsset->IsLoaded())
				continue;

			auto cpuMesh = mc.m_MeshAsset->GetCPUMesh();
			if (!cpuMesh)
				continue;

			const glm::mat4 model = tc.GetTransform();
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
			if (!mc.m_AABBTree.Raycast(localRay, cpuMesh->GetVertices(), cpuMesh->GetIndices(),
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

} // namespace Nova::Core::Scene