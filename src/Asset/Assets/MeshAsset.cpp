#include "Asset/Assets/MeshAsset.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include "Core/Log.h"

#include "Renderer/RHI/RHI_Mesh.h"

namespace Nova::Core::Asset::Assets {

    namespace {
        constexpr const char* kEngineScheme = "Engine:";
        constexpr const char* kPrimitiveFolder = "Primitives/";
    }

    MeshAsset::MeshAsset(std::filesystem::path path, MeshAssetDesc desc) : Asset(AssetType::Mesh, path, UUIDForMeshPath(path)), m_Desc(std::move(desc)) {}

    bool MeshAsset::Load() {
        if (m_Loaded && m_CPUMesh)
            return true;

        m_CPUMesh.reset();
        m_Primitive = MeshPrimitive::Unknown;

        if (!LoadFromPath()) {
            NV_LOG_WARN(("MeshAsset load failed: " + m_Path.generic_string()).c_str());
            return false;
        }

        m_Loaded = true;
        return true;
    }

    bool MeshAsset::Reload() {
        m_Loaded = false;
        return Load();
    }

    bool MeshAsset::IsEnginePrimitivePath(const std::filesystem::path& path, std::string* outName) {
        const std::string p = path.generic_string();
        const size_t enginePos = p.find(kEngineScheme);
        if (enginePos == std::string::npos)
            return false;

        const size_t primPos = p.find(kPrimitiveFolder, enginePos);
        if (primPos == std::string::npos)
            return false;

        std::string name = p.substr(primPos + std::string(kPrimitiveFolder).size());
        if (auto slash = name.find('/'); slash != std::string::npos)
            name = name.substr(0, slash);
        if (auto q = name.find('?'); q != std::string::npos)
            name = name.substr(0, q);

        if (outName)
            *outName = std::move(name);

        return true;
    }

    MeshPrimitive MeshAsset::PrimitiveFromName(const std::string& name) {
        if (name == "Plane")    return MeshPrimitive::Plane;
        if (name == "Cube")     return MeshPrimitive::Cube;
        if (name == "Sphere")   return MeshPrimitive::Sphere;
        if (name == "Cylinder") return MeshPrimitive::Cylinder;
        if (name == "Capsule")  return MeshPrimitive::Capsule;
        if (name == "Torus")    return MeshPrimitive::Torus;
        return MeshPrimitive::Unknown;
    }

    UUID MeshAsset::UUIDForPrimitive(MeshPrimitive primitive) {
        switch (primitive) {
        case MeshPrimitive::Cube:     return PrimitiveMeshAssetID::Cube;
        case MeshPrimitive::Sphere:   return PrimitiveMeshAssetID::Sphere;
        case MeshPrimitive::Cylinder: return PrimitiveMeshAssetID::Cylinder;
        case MeshPrimitive::Capsule:  return PrimitiveMeshAssetID::Capsule;
        case MeshPrimitive::Torus:    return PrimitiveMeshAssetID::Torus;
        case MeshPrimitive::Plane:    return PrimitiveMeshAssetID::Plane;
        default:                      return GenerateUUID();
        }
    }

    UUID MeshAsset::UUIDForMeshPath(const std::filesystem::path& path) {
        std::string primitiveName;
        if (IsEnginePrimitivePath(path, &primitiveName)) {
            const MeshPrimitive primitive = PrimitiveFromName(primitiveName);
            if (primitive != MeshPrimitive::Unknown)
                return UUIDForPrimitive(primitive);
        }
        return GenerateUUID();
    }

    bool MeshAsset::LoadFromPath() {
        std::string primitiveName;
        if (IsEnginePrimitivePath(m_Path, &primitiveName)) {
            m_Primitive = PrimitiveFromName(primitiveName);
            if (m_Primitive == MeshPrimitive::Unknown) {
                NV_LOG_WARN(("MeshAsset: unknown primitive '" + primitiveName + "'").c_str());
                return false;
            }
            return LoadPrimitive(m_Primitive);
        }

        return LoadFromFile();
    }

    bool MeshAsset::LoadFromFile() {
        Assimp::Importer importer;
        // Nova Vulkan pipelines use CLOCKWISE front faces (see VK_PipelineCache).
        // Assimp / OBJ are typically CCW — FlipWindingOrder matches engine culling.
        const unsigned int flags =
            aiProcess_Triangulate |
            aiProcess_GenSmoothNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_ImproveCacheLocality |
            aiProcess_PreTransformVertices |
            aiProcess_FlipUVs |
            aiProcess_FlipWindingOrder;

        const aiScene* scene = importer.ReadFile(m_Path.string(), flags);
        if (!scene || !scene->mRootNode || (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || scene->mNumMeshes == 0) {
            const char* err = importer.GetErrorString();
            NV_LOG_WARN(("MeshAsset Assimp failed for '" + m_Path.generic_string() + "': " +
                         (err && err[0] ? err : "unknown")).c_str());
            return false;
        }

        std::vector<Math::Vertex> vertices;
        std::vector<uint32_t> indices;

        for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
            const aiMesh* mesh = scene->mMeshes[meshIndex];
            if (!mesh)
                continue;

            const uint32_t baseVertex = static_cast<uint32_t>(vertices.size());
            vertices.reserve(vertices.size() + mesh->mNumVertices);

            for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
                Math::Vertex vertex{};

                vertex.m_Position = {
                    mesh->mVertices[i].x,
                    mesh->mVertices[i].y,
                    mesh->mVertices[i].z
                };

                if (mesh->HasNormals()) {
                    vertex.m_Normal = {
                        mesh->mNormals[i].x,
                        mesh->mNormals[i].y,
                        mesh->mNormals[i].z
                    };
                }

                if (mesh->mTextureCoords[0]) {
                    vertex.m_TexCoord = {
                        mesh->mTextureCoords[0][i].x,
                        mesh->mTextureCoords[0][i].y
                    };
                }

                if (mesh->HasVertexColors(0)) {
                    vertex.m_Color = {
                        mesh->mColors[0][i].r,
                        mesh->mColors[0][i].g,
                        mesh->mColors[0][i].b
                    };
                } else {
                    vertex.m_Color = { 1.0f, 1.0f, 1.0f };
                }

                if (mesh->HasTangentsAndBitangents()) {
                    vertex.m_Tangent = {
                        mesh->mTangents[i].x,
                        mesh->mTangents[i].y,
                        mesh->mTangents[i].z
                    };
                    vertex.m_Bitangent = {
                        mesh->mBitangents[i].x,
                        mesh->mBitangents[i].y,
                        mesh->mBitangents[i].z
                    };
                }

                vertices.push_back(vertex);
            }

            for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
                const aiFace& face = mesh->mFaces[f];
                if (face.mNumIndices != 3)
                    continue;

                indices.push_back(baseVertex + face.mIndices[0]);
                indices.push_back(baseVertex + face.mIndices[1]);
                indices.push_back(baseVertex + face.mIndices[2]);
            }
        }

        if (vertices.empty() || indices.empty()) {
            NV_LOG_WARN(("MeshAsset: no geometry in '" + m_Path.generic_string() + "'").c_str());
            return false;
        }

        m_Primitive = MeshPrimitive::Unknown;
        m_CPUMesh = std::make_shared<Renderer::RHI::RHI_Mesh>(std::move(vertices), std::move(indices));
        return static_cast<bool>(m_CPUMesh);
    }

    bool MeshAsset::LoadPrimitive(MeshPrimitive primitive) {
        using Nova::Core::Renderer::RHI::RHI_Mesh;

        switch (primitive) {
        case MeshPrimitive::Plane:
            m_CPUMesh = RHI_Mesh::CreatePlane();
            break;
        case MeshPrimitive::Cube:
            m_CPUMesh = RHI_Mesh::CreateCube(m_Desc.m_HalfExtent);
            break;
        case MeshPrimitive::Sphere:
            m_CPUMesh = RHI_Mesh::CreateSphere(
                m_Desc.m_Radius,
                m_Desc.m_LatitudeSegments,
                m_Desc.m_LongitudeSegments);
            break;
        case MeshPrimitive::Cylinder:
            m_CPUMesh = RHI_Mesh::CreateCylinder(
                m_Desc.m_Radius,
                m_Desc.m_Height,
                m_Desc.m_RadialSegments,
                m_Desc.m_HeightSegments);
            break;
        case MeshPrimitive::Capsule:
            m_CPUMesh = RHI_Mesh::CreateCapsule(
                m_Desc.m_Radius,
                m_Desc.m_Height,
                m_Desc.m_RadialSegments,
                m_Desc.m_HeightSegments,
                m_Desc.m_HemisphereRings);
            break;
        case MeshPrimitive::Torus:
            m_CPUMesh = RHI_Mesh::CreateTorus(
                m_Desc.m_MajorRadius,
                m_Desc.m_MinorRadius,
                m_Desc.m_MajorSegments,
                m_Desc.m_MinorSegments);
            break;
        default:
            return false;
        }

        return static_cast<bool>(m_CPUMesh);
    }

} // namespace Nova::Core::Asset::Assets