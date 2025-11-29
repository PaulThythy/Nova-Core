#include "Renderer/Mesh.h"

namespace Nova::Core::Renderer {

    void Mesh::Upload(const Renderer::Mesh&) {}

    void Mesh::Release() {}

    void Mesh::Bind() const {}

    void Mesh::Draw() const {}

    void Mesh::Unbind() const {}

    std::shared_ptr<Mesh> Mesh::CreatePlane()
    {
        std::vector<Vertex> vertices;
        vertices.reserve(4);

        Vertex v0;
        v0.m_Position = { -1.0f, 0.0f, -1.0f };
        v0.m_Normal = { 0.0f,  1.0f, 0.0f };
        v0.m_TexCoord = { 0.0f,  0.0f };
        v0.m_Color = { 1.0f,  0.0f, 0.0f }; // red

        Vertex v1;
        v1.m_Position = { 1.0f, 0.0f, -1.0f };
        v1.m_Normal = { 0.0f,  1.0f, 0.0f };
        v1.m_TexCoord = { 1.0f,  0.0f };
        v1.m_Color = { 0.0f,  1.0f, 0.0f }; // green

        Vertex v2;
        v2.m_Position = { 1.0f,  0.0f, 1.0f };
        v2.m_Normal = { 0.0f,  1.0f, 0.0f };
        v2.m_TexCoord = { 1.0f,  1.0f };
        v2.m_Color = { 0.0f,  0.0f, 1.0f }; // blue

        Vertex v3;
        v3.m_Position = { -1.0f,  0.0f, 1.0f };
        v3.m_Normal = { 0.0f,  1.0f, 0.0f };
        v3.m_TexCoord = { 0.0f,  1.0f };
        v3.m_Color = { 1.0f,  1.0f, 1.0f }; // white

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);

        std::vector<int> indices = {
            0, 2, 1,
            3, 2, 0
        };

        return std::make_shared<Mesh>(std::move(vertices), std::move(indices));
    }

} // namespace Nova::Core::Renderer