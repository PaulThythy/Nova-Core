#ifndef BUFFER_H
#define BUFFER_H

#include <cstdint>
#include <cstring>
#include <utility>

namespace Nova::Core {

    /**
     * Owning raw byte buffer (CPU)
     */
    struct Buffer {
        uint8_t* Data = nullptr;
        uint64_t Size = 0;

        Buffer() = default;

        explicit Buffer(uint64_t size) { Allocate(size); }

        Buffer(const Buffer& other) {
            if (other.Data && other.Size > 0) {
                Allocate(other.Size);
                std::memcpy(Data, other.Data, static_cast<size_t>(other.Size));
            }
        }

        Buffer& operator=(const Buffer& other) {
            if (this == &other) {
                return *this;
            }

            if (other.Data && other.Size > 0) {
                Allocate(other.Size);
                std::memcpy(Data, other.Data, static_cast<size_t>(other.Size));
            }
            else {
                Release();
            }

            return *this;
        }

        Buffer(Buffer&& other) noexcept : Data(other.Data), Size(other.Size) {
            other.Data = nullptr;
            other.Size = 0;
        }

        Buffer& operator=(Buffer&& other) noexcept {
            if (this == &other) {
                return *this;
            }

            Release();
            Data = other.Data;
            Size = other.Size;
            other.Data = nullptr;
            other.Size = 0;
            return *this;
        }

        ~Buffer() {
            Release();
        }

        static Buffer Copy(const Buffer& other) {
            return Buffer(other);
        }

        void Allocate(uint64_t size) {
            Release();

            if (size == 0) {
                return;
            }

            Data = new uint8_t[static_cast<size_t>(size)];
            Size = size;
        }

        void Release() {
            delete[] Data;
            Data = nullptr;
            Size = 0;
        }

        void ZeroInitialize() {
            if (Data && Size > 0) {
                std::memset(Data, 0, static_cast<size_t>(Size));
            }
        }

        template<typename T>
        T* As() {
            return reinterpret_cast<T*>(Data);
        }

        template<typename T>
        const T* As() const {
            return reinterpret_cast<const T*>(Data);
        }

        explicit operator bool() const {
            return Data != nullptr;
        }
    };

    /** RAII helper with accessor-style API. Buffer itself is already owning. */
    struct ScopedBuffer {
        explicit ScopedBuffer(Buffer buffer) : m_Buffer(std::move(buffer)) {}

        explicit ScopedBuffer(uint64_t size)
            : m_Buffer(size) {
        }

        ScopedBuffer(const ScopedBuffer&) = delete;
        ScopedBuffer& operator=(const ScopedBuffer&) = delete;

        ScopedBuffer(ScopedBuffer&&) noexcept = default;
        ScopedBuffer& operator=(ScopedBuffer&&) noexcept = default;

        uint8_t* Data() { return m_Buffer.Data; }
        const uint8_t* Data() const { return m_Buffer.Data; }
        uint64_t Size() const { return m_Buffer.Size; }

        Buffer& GetBuffer() { return m_Buffer; }
        const Buffer& GetBuffer() const { return m_Buffer; }

        template<typename T>
        T* As() {
            return m_Buffer.As<T>();
        }

        template<typename T>
        const T* As() const {
            return m_Buffer.As<T>();
        }

        explicit operator bool() const { return static_cast<bool>(m_Buffer); }

    private:
        Buffer m_Buffer;
    };

} // namespace Nova::Core

#endif // BUFFER_H