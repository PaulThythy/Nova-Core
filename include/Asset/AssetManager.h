#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>

#include "Api.h"
#include "AssetHandle.h"
#include "Asset.h"
#include "Core/Assert.h"
#include "Core/UUID.h"

namespace Nova::Core::Asset {

    class NV_API AssetManager final {
    public:
        static AssetManager& Get() {
            static AssetManager s_Instance;
            return s_Instance;
        }

        // generic runtime acquisition with additional parameters
        template<typename T, typename... Args>
        AssetHandle<T> Acquire(const std::filesystem::path& path, Args&&... args) {
            static_assert(std::is_base_of_v<Asset, T>, "Acquire<T>: T must derive from Asset");

            const std::filesystem::path normalizedPath = NormalizePath(path);
            const std::string key = PathKey(normalizedPath);

            std::lock_guard<std::mutex> lock(m_Mutex);

            // Cache hit by path
            if (auto it = m_IdByPath.find(key); it != m_IdByPath.end()) {
                const UUID id = it->second;
                if (auto itA = m_AssetsById.find(id); itA != m_AssetsById.end()) {
                    auto casted = std::dynamic_pointer_cast<T>(itA->second);
                    if (casted) {
                        return AssetHandle<T>(std::move(casted));
                    }

                    NV_ASSERT_MSG(false, "AssetManager::Acquire requested the same path with a different asset type.");
                    return AssetHandle<T>();
                }
            }

            // Cache miss -> create
            auto asset = std::make_shared<T>(normalizedPath, std::forward<Args>(args)...);

            const UUID id = asset->GetUUID();
            m_AssetsById[id] = asset;
            m_IdByPath[key] = id;

            return AssetHandle<T>(std::move(asset));
        }

        template <typename T>
        bool Reload(const AssetHandle<T>& handle) {
            NV_ASSERT_MSG(handle, "AssetManager::Reload received an empty asset handle.");
            if (!handle) return false;
            return handle->Reload(); // assumes Asset exposes a virtual Reload()
        }

        //TODO acquire resources by UUID
        //template <typename T>
        //AssetHandle<T> Acquire(UUID uuid);

    private:
        /** Resolve virtual asset URIs (Engine://Shaders/..., Editor://Shaders/...) to filesystem paths. */
        static std::filesystem::path ResolveUri(const std::filesystem::path& p) {
            const std::string s = p.generic_string();

            auto tryMount = [&](std::string_view prefix, const std::filesystem::path& root) -> std::filesystem::path {
                if (s.size() < prefix.size())
                    return {};
                if (s.compare(0, prefix.size(), prefix) != 0)
                    return {};
                return root / s.substr(prefix.size());
            };

            const std::filesystem::path cwd = std::filesystem::current_path();
            const std::filesystem::path engineShaders = cwd / "Nova-Core" / "Resources" / "Engine" / "Shaders";
            const std::filesystem::path editorShaders = cwd / "Nova-App" / "Resources" / "Editor" / "Shaders";

            if (auto resolved = tryMount("Engine://Shaders/", engineShaders); !resolved.empty())
                return resolved;
            if (auto resolved = tryMount("Engine:/Shaders/", engineShaders); !resolved.empty())
                return resolved;
            if (auto resolved = tryMount("Editor://Shaders/", editorShaders); !resolved.empty())
                return resolved;
            if (auto resolved = tryMount("Editor:/Shaders/", editorShaders); !resolved.empty())
                return resolved;

            return p;
        }

        static std::filesystem::path NormalizePath(const std::filesystem::path& p) {
            const std::filesystem::path resolved = ResolveUri(p);

            // Virtual URIs (e.g. Engine://Primitives/Cube) stay as-is for asset-specific loaders.
            const std::string s = resolved.generic_string();
            if (s.find("://") != std::string::npos || s.rfind("Engine:", 0) == 0 || s.rfind("Editor:", 0) == 0)
                return resolved;

            std::error_code ec;
            auto abs = std::filesystem::absolute(resolved, ec);
            if (ec) abs = resolved;

            // lexically_normal does not touch the filesystem, so it still works for missing files.
            return abs.lexically_normal();
        }

        static std::string PathKey(const std::filesystem::path& p) {
            // On Windows, you may want this key to be case-insensitive.
            std::string s = p.generic_string();
#if defined(_WIN32)
            std::transform(s.begin(), s.end(), s.begin(),
                [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
#endif
            return s;
        }

        mutable std::mutex m_Mutex;
        std::unordered_map<UUID, std::shared_ptr<Asset>> m_AssetsById;
        std::unordered_map<std::string, UUID> m_IdByPath;
    };

} // Nova::Core::Asset

#endif // ASSETMANAGER_H