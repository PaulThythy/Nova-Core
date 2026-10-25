#ifndef ASSETMANAGER_H
#define ASSETMANAGER_H

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>

#include "Api.h"
#include "AssetHandle.h"
#include "Asset.h"
#include "Core/Assert.h"
#include "Core/FileSystem.h"
#include "Core/UUID.h"

namespace Nova::Core::Asset {

	class NV_API AssetManager final {
	public:
		static AssetManager& Get() {
			static AssetManager s_Instance;
			return s_Instance;
		}

		/** Create or return a cached handle for `path`. Does not load data — call Load. */
		template<typename T, typename... Args>
		AssetHandle<T> Acquire(const std::filesystem::path& path, Args&&... args) {
			static_assert(std::is_base_of_v<Asset, T>, "Acquire<T>: T must derive from Asset");

			const std::filesystem::path normalizedPath = NormalizePath(path);
			const std::string key = PathKey(normalizedPath);

			std::lock_guard<std::mutex> lock(m_Mutex);

			if (auto it = m_IdByPath.find(key); it != m_IdByPath.end()) {
				const UUID id = it->second;
				if (auto itA = m_AssetsById.find(id); itA != m_AssetsById.end()) {
					if (std::dynamic_pointer_cast<T>(itA->second))
						return AssetHandle<T>{ id };

					NV_ASSERT_MSG(false, "AssetManager::Acquire requested the same path with a different asset type.");
					return AssetHandle<T>{};
				}
			}

			auto asset = std::make_shared<T>(normalizedPath, std::forward<Args>(args)...);
			const UUID id = asset->GetUUID();
			m_AssetsById[id] = asset;
			m_IdByPath[key] = id;

			return AssetHandle<T>{ id };
		}

		template <typename T>
		std::shared_ptr<T> GetAsset(AssetHandle<T> handle) const {
			static_assert(std::is_base_of_v<Asset, T>, "GetAsset<T>: T must derive from Asset");
			if (!handle)
				return nullptr;

			std::lock_guard<std::mutex> lock(m_Mutex);
			auto it = m_AssetsById.find(handle.GetUUID());
			if (it == m_AssetsById.end())
				return nullptr;
			return std::dynamic_pointer_cast<T>(it->second);
		}

		template <typename T>
		bool Load(AssetHandle<T> handle) {
			auto asset = GetAsset(handle);
			return asset && asset->Load();
		}

		template <typename T>
		bool Reload(AssetHandle<T> handle) {
			auto asset = GetAsset(handle);
			return asset && asset->Reload();
		}

		// TODO: Acquire by UUID

	private:
		/** Resolve virtual asset URIs (Engine://..., Editor://..., Tests://...) to filesystem paths. */
		static std::filesystem::path ResolveUri(const std::filesystem::path& p) {
			const std::string s = p.generic_string();

			auto tryMount = [&](std::string_view prefix, const std::filesystem::path& root) -> std::filesystem::path {
				if (s.size() < prefix.size())
					return {};
				if (s.compare(0, prefix.size(), prefix) != 0)
					return {};
				return root / s.substr(prefix.size());
			};

			const std::filesystem::path cwd = FileSystem::CurrentPath();
			const std::filesystem::path engineShaders = cwd / "Nova-Core" / "Resources" / "Engine" / "Shaders";
			const std::filesystem::path editorShaders = cwd / "Nova-App" / "Resources" / "Editor" / "Shaders";
			const std::filesystem::path editorIcons = cwd / "Nova-App" / "Resources" / "Editor" / "Icons";
			const std::filesystem::path testsRoot = cwd / "Nova-Tests" / "Resources" / "Tests";

			if (auto resolved = tryMount("Engine://Shaders/", engineShaders); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Engine:/Shaders/", engineShaders); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Editor://Shaders/", editorShaders); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Editor:/Shaders/", editorShaders); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Editor://Icons/", editorIcons); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Editor:/Icons/", editorIcons); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Tests://", testsRoot); !resolved.empty())
				return resolved;
			if (auto resolved = tryMount("Tests:/", testsRoot); !resolved.empty())
				return resolved;

			return p;
		}

		static std::filesystem::path NormalizePath(const std::filesystem::path& p) {
			const std::filesystem::path resolved = ResolveUri(p);

			const std::string s = resolved.generic_string();
			if (s.find("://") != std::string::npos || s.rfind("Engine:", 0) == 0 || s.rfind("Editor:", 0) == 0 || s.rfind("Tests:", 0) == 0)
				return resolved;

			return FileSystem::Absolute(resolved).lexically_normal();
		}

		static std::string PathKey(const std::filesystem::path& p) {
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