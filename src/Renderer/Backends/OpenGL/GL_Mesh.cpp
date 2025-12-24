#include "Renderer/Backends/OpenGL/GL_Mesh.h"

namespace Nova::Core::Renderer::Backends::OpenGL {

    GL_Mesh::GL_Mesh(const Renderer::Graphics::Mesh& mesh): Renderer::Graphics::Mesh(mesh.GetVertices(), mesh.GetIndices()) {}

    GL_Mesh::~GL_Mesh()
    {
        Release();
    }

    void GL_Mesh::Release() {
        if (m_EBO) glDeleteBuffers(1, &m_EBO);
        if (m_VBO) glDeleteBuffers(1, &m_VBO);
        if (m_VAO) glDeleteVertexArrays(1, &m_VAO);
        m_VAO = m_VBO = m_EBO = 0;
        m_IndexCount = 0;
    }

    void GL_Mesh::Upload(const Renderer::Graphics::Mesh& mesh) {
        Release();

        const auto& vertices = mesh.GetVertices();
        const auto& indices = mesh.GetIndices();
        m_IndexCount = static_cast<int>(indices.size());

        glGenVertexArrays(1, &m_VAO);
        glGenBuffers(1, &m_VBO);
        glGenBuffers(1, &m_EBO);

        glBindVertexArray(m_VAO);

        // VBO
        glBindBuffer(GL_ARRAY_BUFFER, m_VBO);
        glBufferData(GL_ARRAY_BUFFER,
            vertices.size() * sizeof(Renderer::Graphics::Vertex),
            vertices.data(),
            GL_STATIC_DRAW);

        // EBO
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER,
            indices.size() * sizeof(int),
            indices.data(),
            GL_STATIC_DRAW);

        GLsizei stride = sizeof(Renderer::Graphics::Vertex);

        // layout (location) to be adapted in shader :
        // layout(location = 0) in vec3 a_Position;
        // layout(location = 1) in vec3 a_Normal;
        // layout(location = 2) in vec2 a_TexCoord;
        // layout(location = 3) in vec3 a_Color;
        // layout(location = 4) in vec3 a_Tangent;
        // layout(location = 5) in vec3 a_Bitangent;

        // Position
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)offsetof(Renderer::Graphics::Vertex, m_Position));

        // Normal
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)offsetof(Renderer::Graphics::Vertex, m_Normal));

        // TexCoord
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, stride,
            (void*)offsetof(Renderer::Graphics::Vertex, m_TexCoord));

        // Color
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)offsetof(Renderer::Graphics::Vertex, m_Color));

        // Tangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)offsetof(Renderer::Graphics::Vertex, m_Tangent));

        // Bitangent
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride,
            (void*)offsetof(Renderer::Graphics::Vertex, m_Bitangent));

        glBindVertexArray(0);
    }

    void GL_Mesh::Bind() const {
        glBindVertexArray(m_VAO);
    }

    void GL_Mesh::Unbind() const {
        glBindVertexArray(0);
    }

    void GL_Mesh::Draw() const {
        glBindVertexArray(m_VAO);
        glDrawElements(GL_TRIANGLES, m_IndexCount, GL_UNSIGNED_INT, nullptr);
        glBindVertexArray(0);
    }

} // namespace Nova::Core::Renderer::Backends::OpenGL