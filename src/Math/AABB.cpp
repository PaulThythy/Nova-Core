#include "Math/AABB.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

#include <glm/gtc/matrix_transform.hpp>

#include "Math/Triangle.h"
#include "Renderer/RHI/RHI_Mesh.h"

namespace Nova::Core::Math {

	AABB::AABB(const glm::vec3& min, const glm::vec3& max) : m_Min(min), m_Max(max) {}

	glm::vec3 AABB::GetCenter() const {
		return (m_Min + m_Max) * 0.5f;
	}

	glm::vec3 AABB::GetExtents() const {
		return m_Max - m_Min;
	}

	void AABB::Expand(const glm::vec3& point) {
		m_Min = glm::min(m_Min, point);
		m_Max = glm::max(m_Max, point);
	}

	void AABB::Merge(const AABB& other) {
		m_Min = glm::min(m_Min, other.m_Min);
		m_Max = glm::max(m_Max, other.m_Max);
	}

	bool AABB::IntersectRay(const Ray& ray, float& outTMin, float& outTMax) const {
		outTMin = 0.0f;
		outTMax = std::numeric_limits<float>::infinity();

		for (int axis = 0; axis < 3; ++axis) {
			const float origin = ray.m_Origin[axis];
			const float dir = ray.m_Direction[axis];
			float t1 = (m_Min[axis] - origin);
			float t2 = (m_Max[axis] - origin);

			if (std::fabs(dir) < 1e-8f) {
				if (origin < m_Min[axis] || origin > m_Max[axis])
					return false;
				continue;
			}

			t1 /= dir;
			t2 /= dir;
			if (t1 > t2)
				std::swap(t1, t2);

			outTMin = std::max(outTMin, t1);
			outTMax = std::min(outTMax, t2);
			if (outTMin > outTMax)
				return false;
		}

		return outTMax >= 0.0f;
	}

	bool AABB::IntersectRay(const Ray& ray) const {
		float tMin = 0.0f, tMax = 0.0f;
		return IntersectRay(ray, tMin, tMax);
	}

	AABB AABBTree::ComputeBounds(const std::vector<uint32_t>& triangleIndices,
	                             const std::vector<Vertex>& vertices,
	                             const std::vector<uint32_t>& indices) {
		AABB bounds;
		const float inf = std::numeric_limits<float>::infinity();
		bounds.m_Min = glm::vec3(inf);
		bounds.m_Max = glm::vec3(-inf);

		for (uint32_t tri : triangleIndices) {
			const uint32_t i0 = indices[tri * 3 + 0];
			const uint32_t i1 = indices[tri * 3 + 1];
			const uint32_t i2 = indices[tri * 3 + 2];
			bounds.Expand(vertices[i0].m_Position);
			bounds.Expand(vertices[i1].m_Position);
			bounds.Expand(vertices[i2].m_Position);
		}

		return bounds;
	}

	void AABBTree::BuildNode(int nodeIndex,
	                         std::vector<uint32_t>& triangleIndices,
	                         uint32_t depth,
	                         uint32_t maxDepth,
	                         const std::vector<Vertex>& vertices,
	                         const std::vector<uint32_t>& indices) {
		auto& node = m_Nodes[static_cast<size_t>(nodeIndex)];
		node.m_Bounds = ComputeBounds(triangleIndices, vertices, indices);

		if ((depth + 1) >= maxDepth || triangleIndices.size() <= 2) {
			node.m_TriangleIndices = std::move(triangleIndices);
			return;
		}

		const glm::vec3 extent = node.m_Bounds.GetExtents();
		int axis = 0;
		if (extent.y > extent[axis]) axis = 1;
		if (extent.z > extent[axis]) axis = 2;

		const float split = node.m_Bounds.GetCenter()[axis];

		std::vector<uint32_t> left;
		std::vector<uint32_t> right;
		left.reserve(triangleIndices.size());
		right.reserve(triangleIndices.size());

		for (uint32_t tri : triangleIndices) {
			const uint32_t i0 = indices[tri * 3 + 0];
			const uint32_t i1 = indices[tri * 3 + 1];
			const uint32_t i2 = indices[tri * 3 + 2];
			const glm::vec3 centroid =
				(vertices[i0].m_Position + vertices[i1].m_Position + vertices[i2].m_Position) / 3.0f;

			if (centroid[axis] < split)
				left.push_back(tri);
			else
				right.push_back(tri);
		}

		if (left.empty() || right.empty()) {
			node.m_TriangleIndices = std::move(triangleIndices);
			return;
		}

		node.m_LeftChild = static_cast<int>(m_Nodes.size());
		m_Nodes.emplace_back();
		node.m_RightChild = static_cast<int>(m_Nodes.size());
		m_Nodes.emplace_back();

		BuildNode(node.m_LeftChild, left, depth + 1, maxDepth, vertices, indices);
		BuildNode(node.m_RightChild, right, depth + 1, maxDepth, vertices, indices);
	}

	void AABBTree::Build(const std::vector<Vertex>& vertices,
	                     const std::vector<uint32_t>& indices,
	                     uint32_t maxDepth) {
		m_Nodes.clear();

		if (indices.size() < 3)
			return;

		const uint32_t triangleCount = static_cast<uint32_t>(indices.size() / 3);
		std::vector<uint32_t> triangleIndices(triangleCount);
		std::iota(triangleIndices.begin(), triangleIndices.end(), 0);

		m_Nodes.emplace_back();
		BuildNode(0, triangleIndices, 0, maxDepth, vertices, indices);
	}

	bool AABBTree::Raycast(const Ray& ray,
	                       const std::vector<Vertex>& vertices,
	                       const std::vector<uint32_t>& indices,
	                       AABBTreeHit& outHit,
	                       float maxDistance) const {
		if (m_Nodes.empty())
			return false;

		float closestT = maxDistance;
		AABBTreeHit hit{};
		if (!RaycastNode(0, ray, vertices, indices, hit, closestT))
			return false;

		outHit = hit;
		return true;
	}

	bool AABBTree::RaycastNode(int nodeIndex,
	                           const Ray& ray,
	                           const std::vector<Vertex>& vertices,
	                           const std::vector<uint32_t>& indices,
	                           AABBTreeHit& outHit,
	                           float& closestT) const {
		const AABBNode& node = m_Nodes[static_cast<size_t>(nodeIndex)];

		float tMin = 0.0f, tMax = 0.0f;
		if (!node.m_Bounds.IntersectRay(ray, tMin, tMax) || tMin > closestT)
			return false;

		if (node.IsLeaf()) {
			bool hit = false;
			for (uint32_t triIndex : node.m_TriangleIndices) {
				const uint32_t base = triIndex * 3;
				if (base + 2 >= indices.size())
					continue;

				const Triangle tri{
					vertices[indices[base + 0]].m_Position,
					vertices[indices[base + 1]].m_Position,
					vertices[indices[base + 2]].m_Position,
				};

				float t = 0.0f;
				if (!tri.IntersectRay(ray, t) || t > closestT || t < 0.0f)
					continue;

				closestT = t;
				outHit.m_T = t;
				outHit.m_Point = ray.At(t);
				outHit.m_Normal = tri.GetNormal();
				outHit.m_TriangleIndex = triIndex;
				hit = true;
			}
			return hit;
		}

		bool hitLeft = false;
		bool hitRight = false;
		if (node.m_LeftChild >= 0)
			hitLeft = RaycastNode(node.m_LeftChild, ray, vertices, indices, outHit, closestT);
		if (node.m_RightChild >= 0)
			hitRight = RaycastNode(node.m_RightChild, ray, vertices, indices, outHit, closestT);
		return hitLeft || hitRight;
	}

	std::shared_ptr<Renderer::RHI::RHI_Mesh> CreateUnitAABBWireframeMesh(const glm::vec3& color) {
		const glm::vec3 corners[8] = {
			{ -0.5f, -0.5f, -0.5f },
			{  0.5f, -0.5f, -0.5f },
			{  0.5f,  0.5f, -0.5f },
			{ -0.5f,  0.5f, -0.5f },
			{ -0.5f, -0.5f,  0.5f },
			{  0.5f, -0.5f,  0.5f },
			{  0.5f,  0.5f,  0.5f },
			{ -0.5f,  0.5f,  0.5f },
		};

		const uint32_t edges[12][2] = {
			{ 0, 1 }, { 1, 2 }, { 2, 3 }, { 3, 0 },
			{ 4, 5 }, { 5, 6 }, { 6, 7 }, { 7, 4 },
			{ 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 },
		};

		std::vector<Vertex> vertices;
		std::vector<uint32_t> lineIndices;
		vertices.reserve(24);
		lineIndices.reserve(24);

		for (const auto& edge : edges) {
			for (int end = 0; end < 2; ++end) {
				Vertex v{};
				v.m_Position = corners[edge[end]];
				v.m_Normal = glm::vec3(0.0f, 1.0f, 0.0f);
				v.m_Color = color;
				vertices.push_back(v);
				lineIndices.push_back(static_cast<uint32_t>(vertices.size() - 1));
			}
		}

		return std::make_shared<Renderer::RHI::RHI_Mesh>(std::move(vertices), std::move(lineIndices));
	}

	glm::mat4 AABBToModelMatrix(const AABB& bounds) {
		const glm::vec3 center = bounds.GetCenter();
		glm::vec3 extent = bounds.GetExtents();
		extent = glm::max(extent, glm::vec3(1e-4f));
		glm::mat4 model(1.0f);
		model = glm::translate(model, center);
		model = glm::scale(model, extent);
		return model;
	}

} // namespace Nova::Core::Math