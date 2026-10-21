#ifndef ASSETHANDLE_H
#define ASSETHANDLE_H

#include "Api.h"
#include "Core/UUID.h"

namespace Nova::Core::Asset {

	/**
	 * Opaque typed reference to an asset owned by AssetManager.
	 * Does not keep the asset alive — resolve via AssetManager::GetAsset.
	 */
	template <typename T>
	struct NV_API AssetHandle {
		UUID m_Id = kInvalidUUID;

		bool IsValid() const { return m_Id != kInvalidUUID; }
		UUID GetUUID() const { return m_Id; }

		explicit operator bool() const { return IsValid(); }

		friend bool operator==(const AssetHandle& a, const AssetHandle& b) { return a.m_Id == b.m_Id; }
		friend bool operator!=(const AssetHandle& a, const AssetHandle& b) { return !(a == b); }
	};

} // namespace Nova::Core::Asset

#endif // ASSETHANDLE_H