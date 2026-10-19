#ifndef RHI_MESH_H
#define RHI_MESH_H

#include <cstdint>
#include <cstring>
#include <memory>
#include <vector>

#include "Api.h"
#include "Core/Buffer.h"
#include "Math/Vertex.h"

namespace Nova::Core::Renderer::RHI {

	struct NV_API RHI_Mesh {

		RHI_Mesh() = default;

		RHI_Mesh(std::vector<Math::Vertex> vertices, std::vector<uint32_t> indices) {
			if (!vertices.empty()) {
				const uint64_t size = static_cast<uint64_t>(vertices.size() * sizeof(Math::Vertex));
				m_VertexBuffer.Allocate(size);
				std::memcpy(m_VertexBuffer.Data, vertices.data(), static_cast<size_t>(size));
			}
			if (!indices.empty()) {
				const uint64_t size = static_cast<uint64_t>(indices.size() * sizeof(uint32_t));
				m_IndexBuffer.Allocate(size);
				std::memcpy(m_IndexBuffer.Data, indices.data(), static_cast<size_t>(size));
			}
		}

		RHI_Mesh(Core::Buffer vertexBuffer, Core::Buffer indexBuffer): m_VertexBuffer(std::move(vertexBuffer)), m_IndexBuffer(std::move(indexBuffer)) {}

		virtual ~RHI_Mesh() = default;

		const Core::Buffer& GetVertexBuffer() const { return m_VertexBuffer; }
		const Core::Buffer& GetIndexBuffer() const { return m_IndexBuffer; }
		Core::Buffer& GetVertexBuffer() { return m_VertexBuffer; }
		Core::Buffer& GetIndexBuffer() { return m_IndexBuffer; }

		uint32_t GetVertexCount() const {
			return m_VertexBuffer.Size > 0
				? static_cast<uint32_t>(m_VertexBuffer.Size / sizeof(Math::Vertex))
				: 0u;
		}

		uint32_t GetIndexCount() const {
			return m_IndexBuffer.Size > 0
				? static_cast<uint32_t>(m_IndexBuffer.Size / sizeof(uint32_t))
				: 0u;
		}

		Math::Vertex* GetVertices() { return m_VertexBuffer.As<Math::Vertex>(); }
		const Math::Vertex* GetVertices() const { return m_VertexBuffer.As<Math::Vertex>(); }
		uint32_t* GetIndices() { return m_IndexBuffer.As<uint32_t>(); }
		const uint32_t* GetIndices() const { return m_IndexBuffer.As<uint32_t>(); }

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

		Core::Buffer m_VertexBuffer;
		Core::Buffer m_IndexBuffer;
	};
	
} // namespace Nova::Core::Renderer::RHI

#endif // RHI_MESH_H