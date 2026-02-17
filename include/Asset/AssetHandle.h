#ifndef ASSETHANDLE_H
#define ASSETHANDLE_H

#include <memory>

#include "Core/UUID.h"

namespace Nova::Core::Asset {

    template <typename T>
    class AssetHandle {
    public:
        AssetHandle() = default;
        explicit AssetHandle(std::shared_ptr<T> asset) : m_AssetRef(std::move(asset)) {}

        T* operator->() { return m_AssetRef.get(); }
        const T* operator->() const { return m_AssetRef.get(); }

        T& operator*() { return *m_AssetRef; }
        const T& operator*() const { return *m_AssetRef; }

        explicit operator bool() const { return (bool)m_AssetRef; }

        std::shared_ptr<T> GetShared() const { return m_AssetRef; }

    private:
        std::shared_ptr<T> m_AssetRef;
    };

} // Nova::Core::Asset

#endif // ASSETHANDLE_H