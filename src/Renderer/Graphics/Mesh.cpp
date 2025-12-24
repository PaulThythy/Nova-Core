#include "Renderer/Graphics/Mesh.h"

#include <glm/gtc/constants.hpp>

namespace Nova::Core::Renderer::Graphics {

    void Mesh::Upload(const Renderer::Graphics::Mesh&) {}

    void Mesh::Release() {}

    void Mesh::Bind() const {}

    void Mesh::Draw() const {}

    void Mesh::Unbind() const {}

    std::shared_ptr<Mesh> Mesh::CreatePlane(){
        std::vector<Vertex> vertices;
        vertices.reserve(6); // 2 triangles * 3 vertices

        std::vector<int> indices;
        indices.reserve(6);

        glm::vec3 p0{ -1.0f, 0.0f, -1.0f }; // bottom-left
        glm::vec3 p1{  1.0f, 0.0f, -1.0f }; // bottom-right
        glm::vec3 p2{  1.0f, 0.0f,  1.0f }; // top-right
        glm::vec3 p3{ -1.0f, 0.0f,  1.0f }; // top-left

        glm::vec3 normal{ 0.0f, 1.0f, 0.0f };

        const glm::vec3 triColors[3] = {
            {1.0f, 1.0f, 0.0f}, // yellow
            {1.0f, 0.0f, 1.0f}, // magenta
            {0.0f, 1.0f, 1.0f}  // cyan
        };

        // --- Triangle 0 vertices (p0, p1, p2) ---
        Vertex v0; // yellow
        v0.m_Position = p0;
        v0.m_Normal   = normal;
        v0.m_TexCoord = { 0.0f, 0.0f };
        v0.m_Color = triColors[0];

        Vertex v1; // magenta
        v1.m_Position = p1;
        v1.m_Normal   = normal;
        v1.m_TexCoord = { 1.0f, 0.0f };
        v1.m_Color    = triColors[1];

        Vertex v2; // cyan
        v2.m_Position = p2;
        v2.m_Normal   = normal;
        v2.m_TexCoord = { 1.0f, 1.0f };
        v2.m_Color    = triColors[2];

        // --- Triangle 1 vertices (p0, p2, p3) ---
        Vertex v3; // yellow
        v3.m_Position = p0;
        v3.m_Normal   = normal;
        v3.m_TexCoord = { 0.0f, 0.0f };
        v3.m_Color    = triColors[0];

        Vertex v4; // magenta
        v4.m_Position = p2;
        v4.m_Normal   = normal;
        v4.m_TexCoord = { 1.0f, 1.0f };
        v4.m_Color    = triColors[1];

        Vertex v5; // cyan
        v5.m_Position = p3;
        v5.m_Normal   = normal;
        v5.m_TexCoord = { 0.0f, 1.0f };
        v5.m_Color    = triColors[2];

        vertices.push_back(v0);
        vertices.push_back(v1);
        vertices.push_back(v2);
        vertices.push_back(v3);
        vertices.push_back(v4);
        vertices.push_back(v5);

        indices.push_back(0);
        indices.push_back(1);
        indices.push_back(2);

        indices.push_back(3);
        indices.push_back(4);
        indices.push_back(5);

        return std::make_shared<Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<Mesh> Mesh::CreateCube(float halfExtent) {
        std::vector<Vertex> vertices;
        std::vector<int>    indices;

        // 6 faces * 2 triangles * 3 vertices
        vertices.reserve(6 * 2 * 3);
        indices.reserve(6 * 2 * 3);

        const float h = halfExtent;

        // Per-triangle
        const glm::vec3 triColors[3] = {
            {1.0f, 1.0f, 0.0f}, // yellow
            {1.0f, 0.0f, 1.0f}, // magenta
            {0.0f, 1.0f, 1.0f}  // cyan
        };

        auto addTriangle = [&](const glm::vec3& p0,
                            const glm::vec3& p1,
                            const glm::vec3& p2,
                            const glm::vec3& normal,
                            const glm::vec2& uv0 = {0.0f, 0.0f},
                            const glm::vec2& uv1 = {1.0f, 0.0f},
                            const glm::vec2& uv2 = {1.0f, 1.0f})
        {
            // Add one triangle (3 unique vertices)
            int baseIndex = static_cast<int>(vertices.size());

            Vertex v0, v1, v2;
            v0.m_Position = p0;
            v1.m_Position = p1;
            v2.m_Position = p2;

            v0.m_Normal = v1.m_Normal = v2.m_Normal = normal;

            v0.m_TexCoord = uv0;
            v1.m_TexCoord = uv1;
            v2.m_TexCoord = uv2;

            // Each triangle has 3 distinct colors (R, G, B)
            v0.m_Color = triColors[0];
            v1.m_Color = triColors[1];
            v2.m_Color = triColors[2];

            // Simple default tangent/bitangent
            v0.m_Tangent = v1.m_Tangent = v2.m_Tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
            v0.m_Bitangent = v1.m_Bitangent = v2.m_Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);

            vertices.push_back(v0);
            vertices.push_back(v1);
            vertices.push_back(v2);

            indices.push_back(baseIndex + 0);
            indices.push_back(baseIndex + 2);
            indices.push_back(baseIndex + 1);
        };

        auto addFace = [&](const glm::vec3& p0,
                        const glm::vec3& p1,
                        const glm::vec3& p2,
                        const glm::vec3& p3,
                        const glm::vec3& normal)
        {
            // Triangle 1
            addTriangle(
                p0, p1, p2,
                normal,
                glm::vec2(0.0f, 0.0f),
                glm::vec2(1.0f, 0.0f),
                glm::vec2(1.0f, 1.0f)
            );

            // Triangle 2
            addTriangle(
                p0, p2, p3,
                normal,
                glm::vec2(0.0f, 0.0f),
                glm::vec2(1.0f, 1.0f),
                glm::vec2(0.0f, 1.0f)
            );
        };

        // Front face  (Z+)
        addFace(
            { -h, -h,  h }, // p0 bottom-left
            {  h, -h,  h }, // p1 bottom-right
            {  h,  h,  h }, // p2 top-right
            { -h,  h,  h }, // p3 top-left
            {  0.0f, 0.0f, 1.0f }
        );

        // Back face   (Z-)
        // Note: order chosen so that it is CCW when viewed from outside (looking along -Z)
        addFace(
            {  h, -h, -h }, // p0 bottom-left
            { -h, -h, -h }, // p1 bottom-right
            { -h,  h, -h }, // p2 top-right
            {  h,  h, -h }, // p3 top-left
            {  0.0f, 0.0f,-1.0f }
        );

        // Right face  (X+)
        addFace(
            {  h, -h,  h }, // p0 bottom-left
            {  h, -h, -h }, // p1 bottom-right
            {  h,  h, -h }, // p2 top-right
            {  h,  h,  h }, // p3 top-left
            {  1.0f, 0.0f, 0.0f }
        );

        // Left face   (X-)
        addFace(
            { -h, -h, -h }, // p0 bottom-left
            { -h, -h,  h }, // p1 bottom-right
            { -h,  h,  h }, // p2 top-right
            { -h,  h, -h }, // p3 top-left
            { -1.0f, 0.0f, 0.0f }
        );

        // Top face    (Y+)
        addFace(
            { -h,  h,  h }, // p0 bottom-left
            {  h,  h,  h }, // p1 bottom-right
            {  h,  h, -h }, // p2 top-right
            { -h,  h, -h }, // p3 top-left
            {  0.0f, 1.0f, 0.0f }
        );

        // Bottom face (Y-)
        addFace(
            { -h, -h, -h }, // p0 bottom-left
            {  h, -h, -h }, // p1 bottom-right
            {  h, -h,  h }, // p2 top-right
            { -h, -h,  h }, // p3 top-left
            {  0.0f,-1.0f, 0.0f }
        );

        return std::make_shared<Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<Mesh> Mesh::CreateSphere(float radius, int latitudeSegments, int longitudeSegments) {
        // Clamp segments to avoid degenerate spheres
        if (latitudeSegments < 2)   latitudeSegments = 2;
        if (longitudeSegments < 3)  longitudeSegments = 3;

        // Temporary grid: positions / normals / uvs
        std::vector<glm::vec3> positions;
        std::vector<glm::vec3> normals;
        std::vector<glm::vec2> uvs;

        positions.reserve((latitudeSegments + 1) * (longitudeSegments + 1));
        normals.reserve(positions.capacity());
        uvs.reserve(positions.capacity());

        // Build the parametric sphere grid
        for (int y = 0; y <= latitudeSegments; ++y) {
            float v    = static_cast<float>(y) / static_cast<float>(latitudeSegments);
            float theta = v * glm::pi<float>(); // [0, PI]

            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (int x = 0; x <= longitudeSegments; ++x) {
                float u   = static_cast<float>(x) / static_cast<float>(longitudeSegments);
                float phi = u * 2.0f * glm::pi<float>(); // [0, 2PI]

                float sinPhi = std::sin(phi);
                float cosPhi = std::cos(phi);

                glm::vec3 pos;
                pos.x = radius * cosPhi * sinTheta;
                pos.y = radius * cosTheta;
                pos.z = radius * sinPhi * sinTheta;

                glm::vec3 n = glm::normalize(pos);

                positions.push_back(pos);
                normals.push_back(n);
                uvs.emplace_back(u, v);
            }
        }

        // Final mesh data: each triangle has its own 3 vertices
        std::vector<Vertex> vertices;
        std::vector<int>    indices;

        const glm::vec3 triColors[3] = {
            {1.0f, 1.0f, 0.0f}, // yellow
            {1.0f, 0.0f, 1.0f}, // magenta
            {0.0f, 1.0f, 1.0f}  // cyan
        };

        vertices.reserve(latitudeSegments * longitudeSegments * 6); // 2 tris * 3 verts
        indices.reserve(latitudeSegments * longitudeSegments * 6);

        auto makeVertex = [&](int gridIndex, const glm::vec3& color) {
            Vertex v;
            v.m_Position  = positions[gridIndex];
            v.m_Normal    = normals[gridIndex];
            v.m_TexCoord  = uvs[gridIndex];
            v.m_Color     = color;
            v.m_Tangent   = glm::vec3(1.0f, 0.0f, 0.0f);
            v.m_Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            return v;
        };

        // Build triangles (CW) with per-vertex color R/G/B
        for (int y = 0; y < latitudeSegments; ++y) {
            for (int x = 0; x < longitudeSegments; ++x) {
                int i0 =  y * (longitudeSegments + 1) + x;
                int i1 =  i0 + 1;
                int i2 = (y + 1) * (longitudeSegments + 1) + x;
                int i3 =  i2 + 1;

                // First triangle (CW): i0, i2, i1
                {
                    int base = static_cast<int>(vertices.size());

                    vertices.push_back(makeVertex(i0, triColors[0])); // red
                    vertices.push_back(makeVertex(i2, triColors[1])); // green
                    vertices.push_back(makeVertex(i1, triColors[2])); // blue

                    indices.push_back(base + 0);
                    indices.push_back(base + 1);
                    indices.push_back(base + 2);
                }

                // Second triangle (CW): i1, i2, i3
                {
                    int base = static_cast<int>(vertices.size());

                    vertices.push_back(makeVertex(i1, triColors[0])); // red
                    vertices.push_back(makeVertex(i2, triColors[1])); // green
                    vertices.push_back(makeVertex(i3, triColors[2])); // blue

                    indices.push_back(base + 0);
                    indices.push_back(base + 1);
                    indices.push_back(base + 2);
                }
            }
        }

        return std::make_shared<Mesh>(std::move(vertices), std::move(indices));
    }


} // namespace Nova::Core::Renderer::Graphics