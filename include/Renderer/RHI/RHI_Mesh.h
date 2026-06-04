#ifndef RHI_MESH_H
#define RHI_MESH_H

#include <vector>
#include <memory>

#include "Api.h"
#include "Renderer/Graphics/Vertex.h"

namespace Nova::Core::Renderer::RHI {

	struct NV_API RHI_Mesh {

		RHI_Mesh() = default;
		RHI_Mesh(std::vector<Graphics::Vertex> vertices, std::vector<uint32_t> indices): m_Vertices(vertices), m_Indices(indices) {}
		virtual ~RHI_Mesh() = default;

		const std::vector<Graphics::Vertex>& GetVertices() const { return m_Vertices; }
		const std::vector<uint32_t>& GetIndices() const { return m_Indices; }
		std::vector<Graphics::Vertex>& GetVertices() { return m_Vertices; }
		std::vector<uint32_t>& GetIndices() { return m_Indices; }

		virtual void Upload(const Renderer::RHI::RHI_Mesh& mesh);
		virtual void Release();

		virtual void Bind()   const;
		virtual void Draw()   const;
		virtual void Unbind() const;

		// Primitive factories
		static std::shared_ptr<RHI_Mesh> CreatePlane();
		static std::shared_ptr<RHI_Mesh> CreateCube(float halfExtent = 0.5f);
		static std::shared_ptr<RHI_Mesh> CreateSphere(float radius = 0.5f, int latitudeSegments = 16, int longitudeSegments = 32);
		static std::shared_ptr<RHI_Mesh> CreateCylinder(float radius = 0.5f, float height = 1.0f, int radialSegments = 32, int heightSegments = 1);
		static std::shared_ptr<RHI_Mesh> CreateCapsule(float radius = 0.5f, float height = 1.0f, int radialSegments = 32, int heightSegments = 1, int hemisphereRings = 8);
		static std::shared_ptr<RHI_Mesh> CreateTorus(float majorRadius = 0.5f, float minorRadius = 0.2f, int majorSegments = 32, int minorSegments = 16);

		std::vector<Graphics::Vertex> m_Vertices;
		std::vector<uint32_t>	m_Indices;
	};
	
} // namespace Nova::Core::Renderer::RHI

#endif // RHI_MESH_H