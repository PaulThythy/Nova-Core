#include "Core/FileSystem.h"

#include <fstream>
#include <sstream>

namespace Nova::Core {

    bool FileSystem::Exists(const std::filesystem::path& path) {
        std::error_code ec;
        return std::filesystem::exists(path, ec);
    }

    std::filesystem::path FileSystem::CurrentPath() {
        std::error_code ec;
        auto path = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path{} : path;
    }

    std::filesystem::path FileSystem::Absolute(const std::filesystem::path& path) {
        std::error_code ec;
        auto abs = std::filesystem::absolute(path, ec);
        return ec ? path : abs;
    }

    bool FileSystem::CreateDirectories(const std::filesystem::path& path) {
        std::error_code ec;
        std::filesystem::create_directories(path, ec);
        return !ec && Exists(path);
    }

    std::filesystem::file_time_type FileSystem::LastWriteTime(const std::filesystem::path& path) {
        std::filesystem::file_time_type time{};
        TryGetLastWriteTime(path, time);
        return time;
    }

    bool FileSystem::TryGetLastWriteTime(
        const std::filesystem::path& path,
        std::filesystem::file_time_type& outTime)
    {
        std::error_code ec;
        outTime = std::filesystem::last_write_time(path, ec);
        return !ec;
    }

    bool FileSystem::ReadTextFile(
        const std::filesystem::path& path,
        std::string& outText,
        std::string& outError)
    {
        std::ifstream file(path, std::ios::in | std::ios::binary);
        if (!file.is_open()) {
            outError = "Failed to open file: " + path.string();
            return false;
        }

        std::ostringstream ss;
        ss << file.rdbuf();
        outText = ss.str();
        if (outText.empty()) {
            outError = "File is empty: " + path.string();
            return false;
        }

        return true;
    }

    bool FileSystem::ReadBinaryFile(
        const std::filesystem::path& path,
        Buffer& outBuffer,
        std::string& outError)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            outError = "Failed to open file: " + path.string();
            return false;
        }

        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        file.seekg(0);
        if (end < 0) {
            outError = "Failed to read file size: " + path.string();
            return false;
        }

        const auto size = static_cast<uint64_t>(end);
        outBuffer.Allocate(size);
        if (size > 0) {
            file.read(reinterpret_cast<char*>(outBuffer.Data), static_cast<std::streamsize>(size));
            if (!file) {
                outBuffer.Release();
                outError = "Failed to read file: " + path.string();
                return false;
            }
        }

        return true;
    }

    bool FileSystem::WriteBinaryFile(
        const std::filesystem::path& path,
        const void* data,
        size_t size,
        std::string& outError)
    {
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            outError = "Failed to open file for write: " + path.string();
            return false;
        }

        if (size > 0 && data != nullptr) {
            file.write(static_cast<const char*>(data), static_cast<std::streamsize>(size));
            if (!file) {
                outError = "Failed to write file: " + path.string();
                return false;
            }
        }

        return true;
    }

    bool FileSystem::WriteBinaryFile(
        const std::filesystem::path& path,
        const Buffer& buffer,
        std::string& outError)
    {
        return WriteBinaryFile(path, buffer.Data, static_cast<size_t>(buffer.Size), outError);
    }

} // namespace Nova::Core