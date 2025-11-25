#ifndef GL_MESH_H
#define GL_MESH_H

#include <glad/gl.h>
#include "Renderer/Mesh.h"

namespace Nova::Core::Renderer::OpenGL {

	struct GL_Mesh : public Renderer::Mesh{

		GL_Mesh() = default;
		explicit GL_Mesh(const Renderer::Mesh& mesh);
		~GL_Mesh() override;

		void Upload(const Renderer::Mesh& mesh) override;
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

} // namespace Nova::Core::Renderer::OpenGL

#endif // GL_MESH_H