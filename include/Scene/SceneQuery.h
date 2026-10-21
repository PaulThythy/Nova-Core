#ifndef SCENEQUERY_H
#define SCENEQUERY_H

#include <cfloat>
#include <cstdint>
#include <memory>
#include <vector>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "Api.h"
#include "Core/UUID.h"
#include "Math/Camera.h"
#include "Math/Ray.h"
#include "Renderer/RHI/RHI_Mesh.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

namespace Nova::Core::Scene {

	class Scene;

	struct NV_API RaycastHit {
		entt::entity m_Entity{ entt::null };
		float m_Distance = 0.0f;
		glm::vec3 m_Point{ 0.0f };
		glm::vec3 m_Normal{ 0.0f };
		uint32_t m_TriangleIndex = 0;
	};

	/** Contiguous run of instances sharing one mesh (one instanced draw call). */
	struct NV_API SceneInstanceBatch {
		UUID m_MeshID = kInvalidUUID;
		std::shared_ptr<Renderer::RHI::RHI_Mesh> m_Mesh;
		uint32_t m_FirstInstance = 0; // element of `nova.instances`
		uint32_t m_InstanceCount = 0;
	};

	/**
	 * Result of BuildRenderBatches: GPU instance list, deduplicated materials, and
	 * per-mesh draw batches. Upload `m_Instances` / `m_Materials` then DrawIndexed each batch.
	 */
	struct NV_API SceneRenderBatches {
		std::vector<Renderer::RHI::InstanceGPU> m_Instances;
		std::vector<Renderer::RHI::MaterialGPU> m_Materials;
		std::vector<SceneInstanceBatch> m_Batches;
	};

	/**
	 * Build a world-space ray from viewport UV coordinates.
	 * u/v in [0,1]: (0,0) = top-left of the viewport image, (1,1) = bottom-right
	 * (matches ImGui / Vulkan framebuffer orientation with Nova's Y-flipped projection).
	 */
	NV_API Math::Ray ScreenPointToRay(const Math::Camera& camera, float u, float v);

	/**
	 * Closest hit against entities that have TransformComponent + MeshComponent
	 * (uses the CPU AABBTree + triangle tests in local mesh space).
	 */
	NV_API bool Raycast(Scene& scene, const Math::Ray& ray, RaycastHit& outHit, float maxDistance = FLT_MAX);

	/**
	 * Group MeshRendererComponent entities by mesh AssetID for GPU instancing.
	 * Deduplicates MaterialGPU into `m_Materials`; each InstanceGPU stores an index into that table.
	 * Batches are contiguous in `m_Instances` starting at `firstInstance`.
	 */
	NV_API SceneRenderBatches BuildRenderBatches(Scene& scene, uint32_t firstInstance = 0);

} // namespace Nova::Core::Scene

#endif // SCENEQUERY_H