#ifndef GL_MESH_H
#define GL_MESH_H

#include <glad/gl.h>
#include "Api.h"
#include "Renderer/RHI/RHI_Mesh.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

	struct NV_API GL_Mesh : public Renderer::RHI::RHI_Mesh{

		GL_Mesh() = default;
		explicit GL_Mesh(const Renderer::RHI::RHI_Mesh& mesh);
		~GL_Mesh() override;

		void Upload(const Renderer::RHI::RHI_Mesh& mesh) override;
		void Release() override;

		void Bind() const override;
		void Draw() const override;
		void Unbind() const override;

		int GetIndexCount() { return m_IndexCount; }

		GLuint  m_VAO{ 0 };
		GLuint  m_VBO{ 0 };
		GLuint  m_EBO{ 0 };

		int m_IndexCount{ 0 };
	};

} // namespace Nova::Core::Renderer::Backends::OpenGL

#endif // GL_MESH_H