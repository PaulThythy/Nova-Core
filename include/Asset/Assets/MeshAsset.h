#ifndef MESHASSET_H
#define MESHASSET_H

#include <filesystem>
#include <memory>
#include <string>

#include "Api.h"
#include "Asset/Asset.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_Mesh.h"

namespace Nova::Core::Asset::Assets {

    enum class MeshPrimitive {
        Unknown = 0,
        Plane,
        Cube,
        Sphere,
        Cylinder,
        Capsule,
        Torus
    };

    struct NV_API MeshAssetDesc {
        // Cube
        float m_HalfExtent = 0.5f;

        // Sphere / Cylinder / Capsule
        float m_Radius = 0.5f;

        // Cylinder / Capsule
        float m_Height = 1.0f;

        // Sphere
        int m_LatitudeSegments = 16;
        int m_LongitudeSegments = 32;

        // Cylinder / Capsule
        int m_RadialSegments = 32;
        int m_HeightSegments = 1;

        // Capsule
        int m_HemisphereRings = 8;

        // Torus
        float m_MinorRadius = 0.2f; // tube radius
        float m_MajorRadius = 0.5f; // distance from center to tube center
        int m_MajorSegments = 32; // segments around the major radius
        int m_MinorSegments = 16; // segments around the minor radius
    };

    class NV_API MeshAsset final : public Asset {
    public:
        MeshAsset(std::filesystem::path path, MeshAssetDesc desc = {});

        bool Load();
        bool Reload();

        bool IsLoaded() const { return m_Loaded; }

        MeshPrimitive GetPrimitive() const { return m_Primitive; }
        const MeshAssetDesc& GetDesc() const { return m_Desc; }

        std::shared_ptr<Renderer::RHI::RHI_Mesh> GetCPUMesh() const { return m_CPUMesh; }
        std::shared_ptr<Renderer::RHI::RHI_Mesh> GetGPUMesh() const { return m_GPUMesh; }

    private:
        bool LoadFromPath();
        bool LoadPrimitive(MeshPrimitive primitive);
        bool BuildGpuMesh(GraphicsAPI api);

        static bool IsEnginePrimitivePath(const std::filesystem::path& path, std::string* outName);
        static MeshPrimitive PrimitiveFromName(const std::string& name);

        MeshAssetDesc m_Desc;
        MeshPrimitive m_Primitive = MeshPrimitive::Unknown;

        std::shared_ptr<Renderer::RHI::RHI_Mesh> m_CPUMesh;
        std::shared_ptr<Renderer::RHI::RHI_Mesh> m_GPUMesh;
        bool m_Loaded = false;
    };

} // namespace Nova::Core::Asset::Assets

#endif // MESHASSET_H
