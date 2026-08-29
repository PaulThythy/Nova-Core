#ifndef AABB_H
#define AABB_H

#include <cstdint>
#include <memory>
#include <vector>

#include <glm/glm.hpp>

#include "Api.h"
#include "Math/Ray.h"
#include "Math/Vertex.h"

namespace Nova::Core::Renderer::RHI {
	struct RHI_Mesh;
}

namespace Nova::Core::Math {

	struct NV_API AABB {
		glm::vec3 m_Min{ 0.0f };
		glm::vec3 m_Max{ 0.0f };

		AABB() = default;
		AABB(const glm::vec3& min, const glm::vec3& max);

		glm::vec3 GetCenter() const;
		glm::vec3 GetExtents() const;
		void Expand(const glm::vec3& point);
		void Merge(const AABB& other);

		/** Slab test. outTMin / outTMax are entry/exit distances along the ray. */
		bool IntersectRay(const Ray& ray, float& outTMin, float& outTMax) const;
		bool IntersectRay(const Ray& ray) const;
	};

	struct NV_API AABBNode {
		AABB m_Bounds;
		std::vector<uint32_t> m_TriangleIndices;
		int m_LeftChild = -1;
		int m_RightChild = -1;

		bool IsLeaf() const { return m_LeftChild < 0 && m_RightChild < 0; }
	};

	struct NV_API AABBTreeHit {
		float m_T = 0.0f;
		glm::vec3 m_Point{ 0.0f };
		glm::vec3 m_Normal{ 0.0f };
		uint32_t m_TriangleIndex = 0;
	};

	class NV_API AABBTree {
	public:
		void Build(const std::vector<Vertex>& vertices,
		           const std::vector<uint32_t>& indices,
		           uint32_t maxDepth);

		const std::vector<AABBNode>& GetNodes() const { return m_Nodes; }
		bool IsBuilt() const { return !m_Nodes.empty(); }

		/**
		 * Closest triangle hit in local mesh space.
		 * Requires the same vertices/indices used to Build().
		 */
		bool Raycast(const Ray& ray,
		             const std::vector<Vertex>& vertices,
		             const std::vector<uint32_t>& indices,
		             AABBTreeHit& outHit,
		             float maxDistance = 1e30f) const;

	private:
		void BuildNode(int nodeIndex,
		               std::vector<uint32_t>& triangleIndices,
		               uint32_t depth,
		               uint32_t maxDepth,
		               const std::vector<Vertex>& vertices,
		               const std::vector<uint32_t>& indices);

		bool RaycastNode(int nodeIndex,
		                 const Ray& ray,
		                 const std::vector<Vertex>& vertices,
		                 const std::vector<uint32_t>& indices,
		                 AABBTreeHit& outHit,
		                 float& closestT) const;

		static AABB ComputeBounds(const std::vector<uint32_t>& triangleIndices,
		                          const std::vector<Vertex>& vertices,
		                          const std::vector<uint32_t>& indices);

		std::vector<AABBNode> m_Nodes;
	};

	/** Unit cube wireframe (-0.5..0.5) as a line list, for debug drawing. */
	NV_API std::shared_ptr<Renderer::RHI::RHI_Mesh> CreateUnitAABBWireframeMesh(const glm::vec3& color = glm::vec3(0.0f, 1.0f, 0.0f));

	/** Model matrix that maps the unit wireframe cube onto `bounds`. */
	NV_API glm::mat4 AABBToModelMatrix(const AABB& bounds);

} // namespace Nova::Core::Math

#endif // AABB_H