#ifndef SCENEQUERY_H
#define SCENEQUERY_H

#include <cfloat>
#include <cstdint>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include "Api.h"
#include "Math/Camera.h"
#include "Math/Ray.h"

namespace Nova::Core::Scene {

	class Scene;

	struct NV_API RaycastHit {
		entt::entity m_Entity{ entt::null };
		float m_Distance = 0.0f;
		glm::vec3 m_Point{ 0.0f };
		glm::vec3 m_Normal{ 0.0f };
		uint32_t m_TriangleIndex = 0;
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

} // namespace Nova::Core::Scene

#endif // SCENEQUERY_H