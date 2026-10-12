#ifndef RHI_SHADERCACHE_H
#define RHI_SHADERCACHE_H

#include <string>

#include "Api.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"

namespace Nova::Core::Renderer::RHI {

    class NV_API RHI_ShaderCache {
    public:
        static std::string ComputeHash(const RHI_ShaderCompileInput& input);
        static bool NeedsRecompile(const RHI_ShaderCompileInput& input, const std::string& hash);

        static bool TryGetMemory(const std::string& hash, RHI_ShaderCompileResult& out);
        static void PutMemory(const std::string& hash, const RHI_ShaderCompileResult& result);
        static void ClearMemory();

        static bool LoadDisk(const std::string& hash, RHI_ShaderCompileResult& out);
        static void SaveDisk(const std::string& hash, const RHI_ShaderCompileResult& result);

    private:
        static std::filesystem::path GetCacheDirectory();
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADERCACHE_H