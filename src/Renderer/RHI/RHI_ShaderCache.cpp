#include "Renderer/RHI/RHI_ShaderCache.h"

#include <fstream>
#include <unordered_map>

namespace Nova::Core::Renderer::RHI {

    std::unordered_map<std::string, RHI_ShaderCompileResult> g_MemoryCache;

    constexpr uint32_t kReflectionCacheMagic = 0x4E565245; // 'NVRE'
    constexpr uint32_t kReflectionCacheVersion = 1;

    void WriteU32(std::ostream& os, uint32_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    void WriteU64(std::ostream& os, uint64_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    void WriteBool(std::ostream& os, bool v) { uint8_t b = v ? 1u : 0u; os.write(reinterpret_cast<const char*>(&b), sizeof(b)); }
    void WriteString(std::ostream& os, const std::string& s) {
        WriteU32(os, static_cast<uint32_t>(s.size()));
        if (!s.empty()) os.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    bool ReadU32(std::istream& is, uint32_t& out) { return (bool)is.read(reinterpret_cast<char*>(&out), sizeof(out)); }
    bool ReadU64(std::istream& is, uint64_t& out) { return (bool)is.read(reinterpret_cast<char*>(&out), sizeof(out)); }
    bool ReadBool(std::istream& is, bool& out) { uint8_t b = 0; if (!is.read(reinterpret_cast<char*>(&b), sizeof(b))) return false; out = (b != 0); return true; }
    bool ReadString(std::istream& is, std::string& out) {
        uint32_t n = 0;
        if (!ReadU32(is, n)) return false;
        out.resize(n);
        if (n == 0) return true;
        return (bool)is.read(out.data(), static_cast<std::streamsize>(n));
    }

    std::filesystem::path GetReflectionCachePath(const std::filesystem::path& dir, const std::string& hash) {
        return dir / (hash + ".refl");
    }

    bool LoadReflectionCache(const std::filesystem::path& dir, const std::string& hash, RHI_ProgramReflection& out) {
        const auto path = GetReflectionCachePath(dir, hash);
        if (!std::filesystem::exists(path)) return false;

        std::ifstream is(path, std::ios::binary);
        if (!is.is_open()) return false;

        uint32_t magic = 0, version = 0;
        if (!ReadU32(is, magic) || magic != kReflectionCacheMagic) return false;
        if (!ReadU32(is, version) || version != kReflectionCacheVersion) return false;

        bool hasPC = false;
        if (!ReadBool(is, hasPC)) return false;
        if (hasPC) {
            uint64_t size = 0;
            uint32_t stages = 0;
            if (!ReadU64(is, size)) return false;
            if (!ReadU32(is, stages)) return false;
            out.m_PushConstants = RHI_PushConstantInfo{ static_cast<size_t>(size), static_cast<RHI_ShaderStageMask>(stages) };
        }

        uint32_t setCount = 0;
        if (!ReadU32(is, setCount)) return false;
        out.m_Sets.clear();
        out.m_Sets.reserve(setCount);
        for (uint32_t si = 0; si < setCount; ++si) {
            RHI_DescriptorSetLayoutInfo dsl{};
            if (!ReadU32(is, dsl.m_Set)) return false;
            uint32_t bindingCount = 0;
            if (!ReadU32(is, bindingCount)) return false;
            dsl.m_Bindings.reserve(bindingCount);
            for (uint32_t bi = 0; bi < bindingCount; ++bi) {
                RHI_BindingInfo b{};
                b.m_Key.m_Set = dsl.m_Set;
                if (!ReadU32(is, b.m_Key.m_Binding)) return false;
                uint32_t kind = 0;
                if (!ReadU32(is, kind)) return false;
                b.m_Kind = static_cast<RHI_ResourceKind>(static_cast<uint8_t>(kind));
                if (!ReadU32(is, b.m_ArrayCount)) return false;
                uint64_t byteSize = 0;
                if (!ReadU64(is, byteSize)) return false;
                b.m_ByteSizeIfKnown = static_cast<size_t>(byteSize);
                if (!ReadString(is, b.m_FullName)) return false;
                uint32_t stages = 0;
                if (!ReadU32(is, stages)) return false;
                b.m_Stages = static_cast<RHI_ShaderStageMask>(stages);
                if (!ReadBool(is, b.m_IsDynamicUniformBuffer)) return false;
                dsl.m_Bindings.push_back(std::move(b));
            }
            out.m_Sets.push_back(std::move(dsl));
        }

        uint32_t nameCount = 0;
        if (!ReadU32(is, nameCount)) return false;
        out.m_NameToBinding.clear();
        out.m_NameToBinding.reserve(nameCount);
        for (uint32_t i = 0; i < nameCount; ++i) {
            std::string name;
            RHI_BindingKey key{};
            if (!ReadString(is, name)) return false;
            if (!ReadU32(is, key.m_Set)) return false;
            if (!ReadU32(is, key.m_Binding)) return false;
            out.m_NameToBinding.emplace(std::move(name), key);
        }

        return true;
    }

    void SaveReflectionCache(const std::filesystem::path& dir, const std::string& hash, const RHI_ProgramReflection& refl) {
        const auto path = GetReflectionCachePath(dir, hash);
        std::ofstream os(path, std::ios::binary);
        if (!os.is_open()) return;

        WriteU32(os, kReflectionCacheMagic);
        WriteU32(os, kReflectionCacheVersion);

        WriteBool(os, refl.m_PushConstants.has_value());
        if (refl.m_PushConstants) {
            WriteU64(os, static_cast<uint64_t>(refl.m_PushConstants->m_SizeBytes));
            WriteU32(os, static_cast<uint32_t>(refl.m_PushConstants->m_Stages));
        }

        WriteU32(os, static_cast<uint32_t>(refl.m_Sets.size()));
        for (const auto& dsl : refl.m_Sets) {
            WriteU32(os, dsl.m_Set);
            WriteU32(os, static_cast<uint32_t>(dsl.m_Bindings.size()));
            for (const auto& b : dsl.m_Bindings) {
                WriteU32(os, b.m_Key.m_Binding);
                WriteU32(os, static_cast<uint32_t>(b.m_Kind));
                WriteU32(os, b.m_ArrayCount);
                WriteU64(os, static_cast<uint64_t>(b.m_ByteSizeIfKnown));
                WriteString(os, b.m_FullName);
                WriteU32(os, static_cast<uint32_t>(b.m_Stages));
                WriteBool(os, b.m_IsDynamicUniformBuffer);
            }
        }

        WriteU32(os, static_cast<uint32_t>(refl.m_NameToBinding.size()));
        for (const auto& [name, key] : refl.m_NameToBinding) {
            WriteString(os, name);
            WriteU32(os, key.m_Set);
            WriteU32(os, key.m_Binding);
        }
    }

    std::filesystem::path RHI_ShaderCache::GetCacheDirectory() {
        auto dir = std::filesystem::current_path() / "Cache" / "Shaders";
        std::filesystem::create_directories(dir);
        return dir;
    }

    std::string RHI_ShaderCache::ComputeHash(const RHI_ShaderCompileInput& input) {
        std::string hash = input.m_File.generic_string();

        std::error_code ec;
        const auto ft = std::filesystem::last_write_time(input.m_File, ec);
        if (!ec) {
            hash += std::to_string(ft.time_since_epoch().count());
        }

        hash += std::to_string(static_cast<int>(input.m_TargetApi));
        hash += std::to_string(static_cast<int>(input.m_Stage));
        hash += input.m_EntryPoint;
        hash += std::to_string(input.m_Debug ? 1 : 0);
        hash += std::to_string(input.m_Optimize ? 1 : 0);

        for (const auto& inc : input.m_IncludeDirs) {
            hash += inc.generic_string();
        }
        for (const auto& d : input.m_Defines) {
            hash += d.first;
            hash += d.second;
        }

        return std::to_string(std::hash<std::string>{}(hash));
    }

    bool RHI_ShaderCache::NeedsRecompile(const RHI_ShaderCompileInput& input, const std::string& hash) {
        (void)input;
        const auto path = GetCacheDirectory() / (hash + ".spv");
        return !std::filesystem::exists(path);
    }

    bool RHI_ShaderCache::TryGetMemory(const std::string& hash, RHI_ShaderCompileResult& out) {
        const auto it = g_MemoryCache.find(hash);
        if (it == g_MemoryCache.end()) return false;
        out = it->second;
        return true;
    }

    void RHI_ShaderCache::PutMemory(const std::string& hash, const RHI_ShaderCompileResult& result) {
        g_MemoryCache[hash] = result;
    }

    void RHI_ShaderCache::ClearMemory() {
        g_MemoryCache.clear();
    }

    bool RHI_ShaderCache::LoadDisk(const std::string& hash, RHI_ShaderCompileResult& out) {
        const auto dir = GetCacheDirectory();
        const auto path = dir / (hash + ".spv");
        if (!std::filesystem::exists(path)) {
            return false;
        }

        std::ifstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return false;
        }

        file.seekg(0, std::ios::end);
        const auto end = file.tellg();
        file.seekg(0);
        const auto size = static_cast<size_t>(end);
        if (size < 4 || (size % 4) != 0) {
            return false;
        }

        out.m_Binary.resize(size);
        file.read(reinterpret_cast<char*>(out.m_Binary.data()), static_cast<std::streamsize>(size));
        out.m_Format = RHI_ShaderBinaryFormat::Spirv;

        (void)LoadReflectionCache(dir, hash, out.m_Reflection);

        out.m_Success = true;
        return true;
    }

    void RHI_ShaderCache::SaveDisk(const std::string& hash, const RHI_ShaderCompileResult& result) {
        const auto dir = GetCacheDirectory();
        const auto path = dir / (hash + ".spv");
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        file.write(reinterpret_cast<const char*>(result.m_Binary.data()),
            static_cast<std::streamsize>(result.m_Binary.size()));

        SaveReflectionCache(dir, hash, result.m_Reflection);
    }

} // namespace Nova::Core::Renderer::RHI