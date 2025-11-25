#ifndef MESH_H
#define MESH_H

#include <vector>
#include <memory>

#include "Renderer/Vertex.h"

namespace Nova::Core::Renderer {

	struct Mesh {

		Mesh() = default;
		Mesh(std::vector<Vertex> vertices, std::vector<int> indices): m_Vertices(vertices), m_Indices(indices) {}
		virtual ~Mesh() = default;

		const std::vector<Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<int>& GetIndices() const { return m_Indices; }
		std::vector<Vertex>& GetVertices() { return m_Vertices; }
		std::vector<int>& GetIndices() { return m_Indices; }

		virtual void Upload(const Renderer::Mesh& mesh);
		virtual void Release();

		virtual void Bind()   const;
		virtual void Draw()   const;
		virtual void Unbind() const;

		//primitives
		static std::shared_ptr<Mesh> CreatePlane();
		static std::shared_ptr<Mesh> CreateCube(float halfExtent = 0.5f);
		static std::shared_ptr<Mesh> CreateSphere(float radius = 0.5f, int latitudeSegments = 16, int longitudeSegments = 32);
		static std::shared_ptr<Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, int radialSegments = 32, int heightSegments = 1);
		static std::shared_ptr<Mesh> CreateCapsule(float radius = 0.5f, float cylinderHeight = 1.0f, int radialSegments = 32, int heightSegments = 1, int hemisphereRings = 8);
		//static std::shared_ptr<Mesh> CreateTorus();

		std::vector<Vertex> m_Vertices;
		std::vector<int>	m_Indices;
	};
	
} // namespace Nova::Core::Renderer

#endif // MESH_H