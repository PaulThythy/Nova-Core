#include "Renderer/RHI/RHI_Mesh.h"

#include <glm/gtc/constants.hpp>

namespace Nova::Core::Renderer::RHI {

    void RHI_Mesh::Upload(const Renderer::RHI::RHI_Mesh&) {}

    void RHI_Mesh::Release() {}

    void RHI_Mesh::Bind() const {}

    void RHI_Mesh::Draw() const {}

    void RHI_Mesh::Unbind() const {}

    std::shared_ptr<RHI_Mesh> RHI_Mesh::CreatePlane() {
        std::vector<Graphics::Vertex> vertices;
        vertices.reserve(6); // 2 triangles * 3 vertices

        std::vector<uint32_t> indices;
        indices.reserve(6);

        glm::vec3 p0{ -1.0f, 0.0f, -1.0f }; // bottom-left
        glm::vec3 p1{ 1.0f, 0.0f, -1.0f }; // bottom-right
        glm::vec3 p2{ 1.0f, 0.0f,  1.0f }; // top-right
        glm::vec3 p3{ -1.0f, 0.0f,  1.0f }; // top-left

        glm::vec3 normal{ 0.0f, 1.0f, 0.0f };

        const glm::vec3 triColors[3] = {
            {1.0f, 1.0f, 0.0f}, // yellow
            {1.0f, 0.0f, 1.0f}, // magenta
            {0.0f, 1.0f, 1.0f}  // cyan
        };

        // --- Triangle 0 vertices (p0, p1, p2) ---
        Graphics::Vertex v0; // yellow
        v0.m_Position = p0;
        v0.m_Normal = normal;
        v0.m_TexCoord = { 0.0f, 0.0f };
        v0.m_Color = triColors[0];

        Graphics::Vertex v1; // magenta
        v1.m_Position = p1;
        v1.m_Normal = normal;
        v1.m_TexCoord = { 1.0f, 0.0f };
        v1.m_Color = triColors[1];

        Graphics::Vertex v2; // cyan
        v2.m_Position = p2;
        v2.m_Normal = normal;
        v2.m_TexCoord = { 1.0f, 1.0f };
        v2.m_Color = triColors[2];

        // --- Triangle 1 vertices (p0, p2, p3) ---
        Graphics::Vertex v3; // yellow
        v3.m_Position = p0;
        v3.m_Normal = normal;
        v3.m_TexCoord = { 0.0f, 0.0f };
        v3.m_Color = triColors[0];

        Graphics::Vertex v4; // magenta
        v4.m_Position = p2;
        v4.m_Normal = normal;
        v4.m_TexCoord = { 1.0f, 1.0f };
        v4.m_Color = triColors[1];

        Graphics::Vertex v5; // cyan
        v5.m_Position = p3;
        v5.m_Normal = normal;
        v5.m_TexCoord = { 0.0f, 1.0f };
        v5.m_Color = triColors[2];

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

        return std::make_shared<RHI_Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<RHI_Mesh> RHI_Mesh::CreateCube(float halfExtent) {
        std::vector<Graphics::Vertex> vertices;
        std::vector<uint32_t>    indices;

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
            const glm::vec2& uv0 = { 0.0f, 0.0f },
            const glm::vec2& uv1 = { 1.0f, 0.0f },
            const glm::vec2& uv2 = { 1.0f, 1.0f })
            {
                // Add one triangle (3 unique vertices)
                uint32_t baseIndex = static_cast<uint32_t>(vertices.size());

                Graphics::Vertex v0, v1, v2;
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
                v0.m_Tangent = v1.m_Tangent = v2.m_Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
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
            { h, -h,  h }, // p1 bottom-right
            { h,  h,  h }, // p2 top-right
            { -h,  h,  h }, // p3 top-left
            { 0.0f, 0.0f, 1.0f }
        );

        // Back face   (Z-)
        // Note: order chosen so that it is CCW when viewed from outside (looking along -Z)
        addFace(
            { h, -h, -h }, // p0 bottom-left
            { -h, -h, -h }, // p1 bottom-right
            { -h,  h, -h }, // p2 top-right
            { h,  h, -h }, // p3 top-left
            { 0.0f, 0.0f,-1.0f }
        );

        // Right face  (X+)
        addFace(
            { h, -h,  h }, // p0 bottom-left
            { h, -h, -h }, // p1 bottom-right
            { h,  h, -h }, // p2 top-right
            { h,  h,  h }, // p3 top-left
            { 1.0f, 0.0f, 0.0f }
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
            { h,  h,  h }, // p1 bottom-right
            { h,  h, -h }, // p2 top-right
            { -h,  h, -h }, // p3 top-left
            { 0.0f, 1.0f, 0.0f }
        );

        // Bottom face (Y-)
        addFace(
            { -h, -h, -h }, // p0 bottom-left
            { h, -h, -h }, // p1 bottom-right
            { h, -h,  h }, // p2 top-right
            { -h, -h,  h }, // p3 top-left
            { 0.0f,-1.0f, 0.0f }
        );

        return std::make_shared<RHI_Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<RHI_Mesh> RHI_Mesh::CreateSphere(float radius, int latitudeSegments, int longitudeSegments) {
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
            float v = static_cast<float>(y) / static_cast<float>(latitudeSegments);
            float theta = v * glm::pi<float>(); // [0, PI]

            float sinTheta = std::sin(theta);
            float cosTheta = std::cos(theta);

            for (int x = 0; x <= longitudeSegments; ++x) {
                float u = static_cast<float>(x) / static_cast<float>(longitudeSegments);
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
        std::vector<Graphics::Vertex> vertices;
        std::vector<uint32_t>    indices;

        const glm::vec3 triColors[3] = {
            {1.0f, 1.0f, 0.0f}, // yellow
            {1.0f, 0.0f, 1.0f}, // magenta
            {0.0f, 1.0f, 1.0f}  // cyan
        };

        vertices.reserve(latitudeSegments * longitudeSegments * 6); // 2 tris * 3 verts
        indices.reserve(latitudeSegments * longitudeSegments * 6);

        auto makeVertex = [&](uint32_t gridIndex, const glm::vec3& color) {
            Graphics::Vertex v;
            v.m_Position = positions[gridIndex];
            v.m_Normal = normals[gridIndex];
            v.m_TexCoord = uvs[gridIndex];
            v.m_Color = color;
            v.m_Tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            v.m_Bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
            return v;
            };

        // Build triangles (CW) with per-vertex color R/G/B
        for (int y = 0; y < latitudeSegments; ++y) {
            for (int x = 0; x < longitudeSegments; ++x) {
                int i0 = y * (longitudeSegments + 1) + x;
                int i1 = i0 + 1;
                int i2 = (y + 1) * (longitudeSegments + 1) + x;
                int i3 = i2 + 1;

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

        return std::make_shared<RHI_Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<RHI_Mesh> RHI_Mesh::CreateCylinder(float radius, float height, int radialSegments, int heightSegments) {
        if (radialSegments < 3) radialSegments = 3;
        if (heightSegments < 1) heightSegments = 1;
 
        std::vector<Graphics::Vertex> vertices;
        std::vector<uint32_t>         indices;
 
        const float halfH = height * 0.5f;
        const float twoPi = glm::two_pi<float>();
 
        const glm::vec3 triColors[3] = {
            { 1.0f, 1.0f, 0.0f }, // yellow
            { 1.0f, 0.0f, 1.0f }, // magenta
            { 0.0f, 1.0f, 1.0f }  // cyan
        };
 
        // ---- Helper: push one triangle ----
        auto pushTri = [&](const Graphics::Vertex& a, const Graphics::Vertex& b, const Graphics::Vertex& c)
        {
            uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back(a);
            vertices.push_back(b);
            vertices.push_back(c);
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        };
 
        // ---- Helper: build a vertex on the cylinder barrel ----
        auto barrelVertex = [&](float u, float v, const glm::vec3& color) -> Graphics::Vertex
        {
            float phi = u * twoPi;
            float cosPhi = std::cos(phi);
            float sinPhi = std::sin(phi);
            float y      = glm::mix(-halfH, halfH, v);
 
            glm::vec3 pos    = { radius * cosPhi, y, radius * sinPhi };
            glm::vec3 normal = glm::normalize(glm::vec3{ cosPhi, 0.0f, sinPhi });
            // Tangent points in the direction of increasing phi (along the ring)
            glm::vec3 tangent   = { -sinPhi, 0.0f,  cosPhi };
            glm::vec3 bitangent = {  0.0f,   1.0f,  0.0f   };
 
            Graphics::Vertex vert;
            vert.m_Position  = pos;
            vert.m_Normal    = normal;
            vert.m_TexCoord  = { u, 1.0f - v };
            vert.m_Color     = color;
            vert.m_Tangent   = tangent;
            vert.m_Bitangent = bitangent;
            return vert;
        };
 
        // ---- Barrel ----
        for (int y = 0; y < heightSegments; ++y)
        {
            float v0 = static_cast<float>(y)     / static_cast<float>(heightSegments);
            float v1 = static_cast<float>(y + 1) / static_cast<float>(heightSegments);
 
            for (int x = 0; x < radialSegments; ++x)
            {
                float u0 = static_cast<float>(x)     / static_cast<float>(radialSegments);
                float u1 = static_cast<float>(x + 1) / static_cast<float>(radialSegments);
 
                Graphics::Vertex bl = barrelVertex(u0, v0, triColors[0]);
                Graphics::Vertex br = barrelVertex(u1, v0, triColors[1]);
                Graphics::Vertex tr = barrelVertex(u1, v1, triColors[2]);
                Graphics::Vertex tl = barrelVertex(u0, v1, triColors[0]);
 
                // CCW triangles
                pushTri(bl, br, tr);
                pushTri(bl, tr, tl);
            }
        }
 
        // ---- Cap helper: fan from a centre vertex ----
        auto addCap = [&](float yPos, float yNormal)
        {
            glm::vec3 nrm = { 0.0f, yNormal, 0.0f };
 
            // Tangent / bitangent for caps: arbitrary but consistent
            glm::vec3 tang   = { 1.0f, 0.0f, 0.0f };
            glm::vec3 bitang = { 0.0f, 0.0f, 1.0f };
 
            Graphics::Vertex centre;
            centre.m_Position  = { 0.0f, yPos, 0.0f };
            centre.m_Normal    = nrm;
            centre.m_TexCoord  = { 0.5f, 0.5f };
            centre.m_Color     = triColors[0];
            centre.m_Tangent   = tang;
            centre.m_Bitangent = bitang;
 
            for (int x = 0; x < radialSegments; ++x)
            {
                float u0 = static_cast<float>(x)     / static_cast<float>(radialSegments);
                float u1 = static_cast<float>(x + 1) / static_cast<float>(radialSegments);
                float phi0 = u0 * twoPi;
                float phi1 = u1 * twoPi;
 
                auto rimVertex = [&](float phi, const glm::vec3& col) -> Graphics::Vertex
                {
                    float c = std::cos(phi), s = std::sin(phi);
                    Graphics::Vertex v;
                    v.m_Position  = { radius * c, yPos, radius * s };
                    v.m_Normal    = nrm;
                    v.m_TexCoord  = { 0.5f + 0.5f * c, 0.5f + 0.5f * s };
                    v.m_Color     = col;
                    v.m_Tangent   = tang;
                    v.m_Bitangent = bitang;
                    return v;
                };
 
                Graphics::Vertex r0 = rimVertex(phi0, triColors[1]);
                Graphics::Vertex r1 = rimVertex(phi1, triColors[2]);
 
                // Wind CCW for top (yNormal > 0) and bottom (yNormal < 0)
                if (yNormal > 0.0f)
                    pushTri(centre, r0, r1);
                else
                    pushTri(centre, r1, r0);
            }
        };
 
        addCap(-halfH, -1.0f); // bottom cap
        addCap( halfH,  1.0f); // top cap
 
        return std::make_shared<RHI_Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<RHI_Mesh> RHI_Mesh::CreateCapsule(float radius, float height, int radialSegments, int heightSegments, int hemisphereRings) {
        if (radialSegments  < 3) radialSegments  = 3;
        if (heightSegments  < 1) heightSegments  = 1;
        if (hemisphereRings < 1) hemisphereRings = 1;
 
        std::vector<Graphics::Vertex> vertices;
        std::vector<uint32_t>         indices;
 
        const float halfH  = height * 0.5f;
        const float twoPi  = glm::two_pi<float>();
        const float halfPi = glm::half_pi<float>();
 
        const glm::vec3 triColors[3] = {
            { 1.0f, 1.0f, 0.0f },
            { 1.0f, 0.0f, 1.0f },
            { 0.0f, 1.0f, 1.0f }
        };
 
        auto pushTri = [&](const Graphics::Vertex& a, const Graphics::Vertex& b, const Graphics::Vertex& c)
        {
            uint32_t base = static_cast<uint32_t>(vertices.size());
            vertices.push_back(a);
            vertices.push_back(b);
            vertices.push_back(c);
            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        };
 
        // ---- Hemisphere helper ----
        // yCenter: Y position of the flat circle of the hemisphere.
        // ySign  : +1 for top, -1 for bottom.
        // UV v range: for the top hemisphere v goes [0.5, 1.0]; bottom [0.0, 0.5]
        //   so that the full capsule texture occupies [0, 1] vertically.
        auto addHemisphere = [&](float yCenter, float ySign)
        {
            // theta: polar angle from the pole
            //   top cap:    theta in [0, PI/2]  (pole at top)
            //   bottom cap: theta in [PI/2, PI] (pole at bottom)
            float thetaStart = (ySign > 0.0f) ? 0.0f    : halfPi;
            float thetaEnd   = (ySign > 0.0f) ? halfPi  : glm::pi<float>();
 
            for (int ring = 0; ring < hemisphereRings; ++ring)
            {
                float t0 = static_cast<float>(ring)     / static_cast<float>(hemisphereRings);
                float t1 = static_cast<float>(ring + 1) / static_cast<float>(hemisphereRings);
 
                float theta0 = glm::mix(thetaStart, thetaEnd, t0);
                float theta1 = glm::mix(thetaStart, thetaEnd, t1);
 
                float sinT0 = std::sin(theta0), cosT0 = std::cos(theta0);
                float sinT1 = std::sin(theta1), cosT1 = std::cos(theta1);
 
                for (int seg = 0; seg < radialSegments; ++seg)
                {
                    float u0 = static_cast<float>(seg)     / static_cast<float>(radialSegments);
                    float u1 = static_cast<float>(seg + 1) / static_cast<float>(radialSegments);
 
                    float phi0 = u0 * twoPi;
                    float phi1 = u1 * twoPi;
 
                    float cosPhi0 = std::cos(phi0), sinPhi0 = std::sin(phi0);
                    float cosPhi1 = std::cos(phi1), sinPhi1 = std::sin(phi1);
 
                    auto makeHV = [&](float sinT, float cosT, float cosPhi, float sinPhi, float u, float t, const glm::vec3& col) -> Graphics::Vertex
                    {
                        glm::vec3 n = { cosPhi * sinT, cosT, sinPhi * sinT };
                        glm::vec3 pos = n * radius;
                        pos.y += yCenter;
 
                        // Tangent: perpendicular to n in XZ plane
                        glm::vec3 tang = glm::normalize(glm::vec3{ -sinPhi, 0.0f, cosPhi });
                        glm::vec3 bitang = glm::cross(tang, n);
 
                        // Map V to [0.5,1] for top or [0,0.5] for bottom
                        float vCoord = (ySign > 0.0f) ? 0.5f + 0.5f * t : 0.5f * (1.0f - t);
 
                        Graphics::Vertex v;
                        v.m_Position  = pos;
                        v.m_Normal    = n;
                        v.m_TexCoord  = { u, vCoord };
                        v.m_Color     = col;
                        v.m_Tangent   = tang;
                        v.m_Bitangent = bitang;
                        return v;
                    };
 
                    // 4 corners of the quad (ring × seg)
                    Graphics::Vertex bl = makeHV(sinT0, cosT0, cosPhi0, sinPhi0, u0, t0, triColors[0]);
                    Graphics::Vertex br = makeHV(sinT0, cosT0, cosPhi1, sinPhi1, u1, t0, triColors[1]);
                    Graphics::Vertex tr = makeHV(sinT1, cosT1, cosPhi1, sinPhi1, u1, t1, triColors[2]);
                    Graphics::Vertex tl = makeHV(sinT1, cosT1, cosPhi0, sinPhi0, u0, t1, triColors[0]);
 
                    pushTri(bl, tr, br);
                    pushTri(bl, tl, tr);
                }
            }
        };
 
        // ---- Cylinder barrel ----
        for (int ys = 0; ys < heightSegments; ++ys)
        {
            float v0 = static_cast<float>(ys)     / static_cast<float>(heightSegments);
            float v1 = static_cast<float>(ys + 1) / static_cast<float>(heightSegments);
 
            float y0 = glm::mix(-halfH, halfH, v0);
            float y1 = glm::mix(-halfH, halfH, v1);
 
            for (int x = 0; x < radialSegments; ++x)
            {
                float u0 = static_cast<float>(x)     / static_cast<float>(radialSegments);
                float u1 = static_cast<float>(x + 1) / static_cast<float>(radialSegments);
 
                float phi0 = u0 * twoPi, phi1 = u1 * twoPi;
                float c0 = std::cos(phi0), s0 = std::sin(phi0);
                float c1 = std::cos(phi1), s1 = std::sin(phi1);
 
                auto barrelV = [&](float c, float s, float yPos, float u, float vCoord, const glm::vec3& col) -> Graphics::Vertex
                {
                    glm::vec3 n    = glm::normalize(glm::vec3{ c, 0.0f, s });
                    glm::vec3 tang = { -s, 0.0f, c };
                    glm::vec3 bit  = {  0.0f, 1.0f, 0.0f };
 
                    // Remap barrel v to [0, 1] so UVs span the full cylinder portion
                    Graphics::Vertex v;
                    v.m_Position  = { c * radius, yPos, s * radius };
                    v.m_Normal    = n;
                    v.m_TexCoord  = { u, vCoord };
                    v.m_Color     = col;
                    v.m_Tangent   = tang;
                    v.m_Bitangent = bit;
                    return v;
                };
 
                Graphics::Vertex bl = barrelV(c0, s0, y0, u0, v0, triColors[0]);
                Graphics::Vertex br = barrelV(c1, s1, y0, u1, v0, triColors[1]);
                Graphics::Vertex tr = barrelV(c1, s1, y1, u1, v1, triColors[2]);
                Graphics::Vertex tl = barrelV(c0, s0, y1, u0, v1, triColors[0]);
 
                pushTri(bl, br, tr);
                pushTri(bl, tr, tl);
            }
        }
 
        // ---- Hemispheres ----
        addHemisphere( halfH,  1.0f); // top
        addHemisphere(-halfH, -1.0f); // bottom
 
        return std::make_shared<RHI_Mesh>(std::move(vertices), std::move(indices));
    }

    std::shared_ptr<RHI_Mesh> RHI_Mesh::CreateTorus(float majorRadius, float minorRadius, int majorSegments, int minorSegments) {
        if (majorSegments < 3) majorSegments = 3;
        if (minorSegments < 3) minorSegments = 3;

        std::vector<Graphics::Vertex> vertices;
        std::vector<uint32_t> indices;

        const float twoPi = glm::two_pi<float>();

        const glm::vec3 triColors[3] = {
            {1.0f, 1.0f, 0.0f}, // yellow
            {1.0f, 0.0f, 1.0f}, // magenta
            {0.0f, 1.0f, 1.0f}  // cyan
        };

        auto pushTri = [&](const Graphics::Vertex& a, const Graphics::Vertex& b, const Graphics::Vertex& c)
        {
            uint32_t base = static_cast<uint32_t>(vertices.size());

            vertices.push_back(a);
            vertices.push_back(b);
            vertices.push_back(c);

            indices.push_back(base + 0);
            indices.push_back(base + 1);
            indices.push_back(base + 2);
        };

        auto makeVertex = [&](float majorU, float minorV, const glm::vec3& color) -> Graphics::Vertex
        {
            float phi   = majorU * twoPi;
            float theta = minorV * twoPi;

            float cosPhi   = std::cos(phi);
            float sinPhi   = std::sin(phi);
            float cosTheta = std::cos(theta);
            float sinTheta = std::sin(theta);

            // Center of the tube circle on the torus ring
            glm::vec3 ringCenter =
            {
                majorRadius * cosPhi,
                0.0f,
                majorRadius * sinPhi
            };

            // Outward normal from ring center
            glm::vec3 normal =
            {
                cosPhi * cosTheta,
                sinTheta,
                sinPhi * cosTheta
            };

            glm::vec3 position =
                ringCenter +
                normal * minorRadius;

            // Tangent along the major ring direction (phi)
            glm::vec3 tangent =
            {
                -sinPhi,
                0.0f,
                cosPhi
            };

            tangent = glm::normalize(tangent);

            // Bitangent along the tube direction (theta)
            glm::vec3 bitangent =
            {
                -cosPhi * sinTheta,
                cosTheta,
                -sinPhi * sinTheta
            };

            bitangent = glm::normalize(bitangent);

            Graphics::Vertex v;
            v.m_Position   = position;
            v.m_Normal     = glm::normalize(normal);
            v.m_TexCoord   = { majorU, minorV };
            v.m_Color      = color;
            v.m_Tangent    = tangent;
            v.m_Bitangent  = bitangent;

            return v;
        };

        for (int major = 0; major < majorSegments; ++major)
        {
            float u0 = static_cast<float>(major) / majorSegments;
            float u1 = static_cast<float>(major + 1) / majorSegments;

            for (int minor = 0; minor < minorSegments; ++minor)
            {
                float v0 = static_cast<float>(minor) / minorSegments;
                float v1 = static_cast<float>(minor + 1) / minorSegments;

                Graphics::Vertex bl = makeVertex(u0, v0, triColors[0]);
                Graphics::Vertex br = makeVertex(u1, v0, triColors[1]);
                Graphics::Vertex tr = makeVertex(u1, v1, triColors[2]);
                Graphics::Vertex tl = makeVertex(u0, v1, triColors[0]);

                // Same winding as cylinder/capsule barrel
                pushTri(bl, br, tr);
                pushTri(bl, tr, tl);
            }
        }

        return std::make_shared<RHI_Mesh>(std::move(vertices), std::move(indices));
    }
} // namespace Nova::Core::Renderer::RHI