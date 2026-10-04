#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include <cstddef>
#include <filesystem>
#include <string>

#include "Api.h"
#include "Core/Buffer.h"

namespace Nova::Core {

    struct NV_API FileSystem {
        static bool Exists(const std::filesystem::path& path);

        static std::filesystem::path CurrentPath();
        static std::filesystem::path Absolute(const std::filesystem::path& path);

        static bool CreateDirectories(const std::filesystem::path& path);

        /** Returns default-constructed time on failure. */
        static std::filesystem::file_time_type LastWriteTime(const std::filesystem::path& path);

        static bool TryGetLastWriteTime(
            const std::filesystem::path& path,
            std::filesystem::file_time_type& outTime);

        static bool ReadTextFile(
            const std::filesystem::path& path,
            std::string& outText,
            std::string& outError);

        static bool ReadBinaryFile(
            const std::filesystem::path& path,
            Buffer& outBuffer,
            std::string& outError);

        static bool WriteBinaryFile(
            const std::filesystem::path& path,
            const void* data,
            size_t size,
            std::string& outError);

        static bool WriteBinaryFile(
            const std::filesystem::path& path,
            const Buffer& buffer,
            std::string& outError);
    };

} // namespace Nova::Core

#endif // FILESYSTEM_H