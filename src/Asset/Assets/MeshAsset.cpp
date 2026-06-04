#include "Asset/Assets/MeshAsset.h"

#include "Core/Application.h"
#include "Core/Log.h"

#include "Renderer/RHI/RHI_Mesh.h"
#include "Renderer/Backends/Vulkan/VK_Mesh.h"

namespace Nova::Core::Asset::Assets {

    namespace {
        constexpr const char* kEngineScheme = "Engine:";
        constexpr const char* kPrimitiveFolder = "Primitives/";
    }

    MeshAsset::MeshAsset(std::filesystem::path path, MeshAssetDesc desc)
        : Asset(AssetType::Mesh, std::move(path)),
        m_Desc(std::move(desc))
    {}

    bool MeshAsset::Load() {
        if (m_Loaded && m_CPUMesh && m_GPUMesh)
            return true;

        m_CPUMesh.reset();
        m_GPUMesh.reset();
        m_Primitive = MeshPrimitive::Unknown;

        if (!LoadFromPath()) {
            NV_LOG_WARN(("MeshAsset load failed: " + m_Path.generic_string()).c_str());
            return false;
        }

        GraphicsAPI api = Application::Get().GetWindow().GetGraphicsAPI();
        if (!BuildGpuMesh(api)) {
            NV_LOG_WARN(("MeshAsset GPU build failed: " + m_Path.generic_string()).c_str());
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
        if (name == "Plane")   return MeshPrimitive::Plane;
        if (name == "Cube")    return MeshPrimitive::Cube;
        if (name == "Sphere")  return MeshPrimitive::Sphere;
        if (name == "Cylinder")return MeshPrimitive::Cylinder;
        if (name == "Capsule") return MeshPrimitive::Capsule;
        if (name == "Torus")   return MeshPrimitive::Torus;
        return MeshPrimitive::Unknown;
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

        NV_LOG_WARN(("MeshAsset: unsupported path '" + m_Path.generic_string() + "'").c_str());
        return false;
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

        return (bool)m_CPUMesh;
    }

    bool MeshAsset::BuildGpuMesh(GraphicsAPI api) {
        if (!m_CPUMesh)
            return false;

        if (api == GraphicsAPI::Vulkan) {
            auto vkMesh = std::make_shared<Renderer::Backends::Vulkan::VK_Mesh>(*m_CPUMesh);
            // vkMesh->Upload(*m_CPUMesh); deferred because Vulkan mesh upload requires a live device.
            m_GPUMesh = std::move(vkMesh);
            return true;
        }

        NV_LOG_WARN("MeshAsset: GPU backend not implemented for this API. Using CPU mesh.");
        m_GPUMesh = m_CPUMesh;
        return true;
    }

} // namespace Nova::Core::Asset::Assets
