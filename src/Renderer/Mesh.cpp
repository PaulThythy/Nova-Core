#include "Renderer/Mesh.h"

namespace Nova::Core::Renderer {

    void Mesh::Upload(const Renderer::Mesh&) {}

    void Mesh::Release() {}

    void Mesh::Bind() const {}

    void Mesh::Draw() const {}

    void Mesh::Unbind() const {}

    std::shared_ptr<Mesh> Mesh::CreatePlane(){
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

    std::shared_ptr<Mesh> Mesh::CreateCube(float halfExtent) {
        std::vector<Vertex> vertices;
        std::vector<int>    indices;

        vertices.reserve(24);  // 6 faces * 4 vertices per face
        indices.reserve(36);   // 6 faces * 2 triangles * 3 indices

        const float h = halfExtent;
        const glm::vec3 colors[3] = {
            {1.0f, 0.0f, 0.0f}, // red
            {0.0f, 1.0f, 0.0f}, // green
            {0.0f, 0.0f, 1.0f}  // blue
        };

        auto addFace = [&](const glm::vec3& p0,
                           const glm::vec3& p1,
                           const glm::vec3& p2,
                           const glm::vec3& p3,
                           const glm::vec3& normal)
        {
            // p0..p3 are assumed in CCW order when seen from outside
            // We will add triangles with CW order later via indices

            int baseIndex = static_cast<int>(vertices.size());

            Vertex v0, v1, v2, v3;
            v0.m_Position = p0;
            v1.m_Position = p1;
            v2.m_Position = p2;
            v3.m_Position = p3;

            v0.m_Normal = v1.m_Normal = v2.m_Normal = v3.m_Normal = normal;

            // Simple UVs for each face
            v0.m_TexCoord = {0.0f, 0.0f};
            v1.m_TexCoord = {1.0f, 0.0f};
            v2.m_TexCoord = {1.0f, 1.0f};
            v3.m_TexCoord = {0.0f, 1.0f};

            // Assign RGB colors in a repeating pattern over vertices
            Vertex* v[4] = { &v0, &v1, &v2, &v3 };
            for (int i = 0; i < 4; ++i) {
                int idx = baseIndex + i;
                v[i]->m_Color = colors[idx % 3];
            }

            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);
            vertices.push_back(v3);

            int i0 = baseIndex + 0;
            int i1 = baseIndex + 1;
            int i2 = baseIndex + 2;
            int i3 = baseIndex + 3;

            // Triangles in CW order
            indices.push_back(i0);
            indices.push_back(i1);
            indices.push_back(i2);

            indices.push_back(i0);
            indices.push_back(i2);
            indices.push_back(i3);
        };

        // Front face  (Z+)
        addFace(
            { -h, -h,  h }, // bottom-left
            {  h, -h,  h }, // bottom-right
            {  h,  h,  h }, // top-right
            { -h,  h,  h }, // top-left
            {  0.0f, 0.0f, 1.0f }
        );

        // Back face   (Z-)
        addFace(
            { -h, -h, -h }, // bottom-left
            { -h,  h, -h }, // top-left
            {  h,  h, -h }, // top-right
            {  h, -h, -h }, // bottom-right
            {  0.0f, 0.0f,-1.0f }
        );

        // Right face  (X+)
        addFace(
            {  h, -h, -h }, // bottom-left
            {  h,  h, -h }, // top-left
            {  h,  h,  h }, // top-right
            {  h, -h,  h }, // bottom-right
            {  1.0f, 0.0f, 0.0f }
        );

        // Left face   (X-)
        addFace(
            { -h, -h,  h }, // bottom-left
            { -h,  h,  h }, // top-left
            { -h,  h, -h }, // top-right
            { -h, -h, -h }, // bottom-right
            { -1.0f, 0.0f, 0.0f }
        );

        // Top face    (Y+)
        addFace(
            { -h,  h, -h }, // bottom-left
            { -h,  h,  h }, // bottom-right
            {  h,  h,  h }, // top-right
            {  h,  h, -h }, // top-left
            {  0.0f, 1.0f, 0.0f }
        );

        // Bottom face (Y-)
        addFace(
            { -h, -h,  h }, // bottom-left
            { -h, -h, -h }, // bottom-right
            {  h, -h, -h }, // top-right
            {  h, -h,  h }, // top-left
            {  0.0f,-1.0f, 0.0f }
        );

        return std::make_shared<Mesh>(std::move(vertices), std::move(indices));
    }

} // namespace Nova::Core::Renderer