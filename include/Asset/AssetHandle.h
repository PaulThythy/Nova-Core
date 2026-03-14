#ifndef ASSETHANDLE_H
#define ASSETHANDLE_H

#include <memory>

#include "Api.h"
#include "Core/Assert.h"
#include "Core/UUID.h"

namespace Nova::Core::Asset {

    template <typename T>
    class NV_API AssetHandle {
    public:
        AssetHandle() = default;
        explicit AssetHandle(std::shared_ptr<T> asset) : m_AssetRef(std::move(asset)) {}

        T* operator->() {
            NV_ASSERT_MSG(m_AssetRef, "Attempted to access an empty AssetHandle.");
            return m_AssetRef.get();
        }
        const T* operator->() const {
            NV_ASSERT_MSG(m_AssetRef, "Attempted to access an empty AssetHandle.");
            return m_AssetRef.get();
        }

        T& operator*() {
            NV_ASSERT_MSG(m_AssetRef, "Attempted to dereference an empty AssetHandle.");
            return *m_AssetRef;
        }
        const T& operator*() const {
            NV_ASSERT_MSG(m_AssetRef, "Attempted to dereference an empty AssetHandle.");
            return *m_AssetRef;
        }

        explicit operator bool() const { return (bool)m_AssetRef; }

        std::shared_ptr<T> GetAssetRef() const { return m_AssetRef; }

    private:
        std::shared_ptr<T> m_AssetRef;
    };

} // Nova::Core::Asset

#endif // ASSETHANDLE_H