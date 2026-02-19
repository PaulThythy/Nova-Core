#ifndef ASSET_H
#define ASSET_H

#include <filesystem>

#include "Core/UUID.h"

namespace Nova::Core::Asset {

    enum class AssetType {
        Unknown = 0,
        Texture,
        Mesh,
        Material,
        Shader,
        Audio,
        Script
    };

    //TODO add:
    /*
    enum class AssetState { Unloaded, Loading, Ready, Failed };
    */

    class Asset {
    public:
        Asset(AssetType type, std::filesystem::path path) : m_Type(type), m_Path(std::move(path)) {}
        virtual ~Asset() = default;

        UUID GetUUID() const { return m_UUID; }
        AssetType GetType() const { return m_Type; }
        const std::filesystem::path& GetPath() const { return m_Path; }

    protected:
        UUID m_UUID;
        AssetType m_Type;
        std::filesystem::path m_Path;
    };

} // Nova::Core::Asset

#endif // ASSET_H