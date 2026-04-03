#ifndef RHI_SHADER_COMPILER_H
#define RHI_SHADER_COMPILER_H

#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>
#include <vector>

#include "Api.h"
#include "Core/GraphicsAPI.h"

namespace Nova::Core::Renderer::RHI {

    enum class RHI_ShaderStage {
        Unknown = 0,
        Vertex,
        Fragment,
        Geometry,
        TessControl,
        TessEvaluation,
        Compute,
        RayGen,
        RayMiss,
        RayClosestHit,
        RayAnyHit,
        RayIntersection,
        RayCallable
    };

    struct NV_API RHI_ShaderCompileInput {
        std::filesystem::path m_File;
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        std::string m_EntryPoint = "main";

        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;

        std::vector<std::filesystem::path> m_IncludeDirs;
        std::vector<std::pair<std::string, std::string>> m_Defines;

        bool m_Debug = false;
        bool m_Optimize = true;

        // When true, skips memory and on-disk caches (forced recompile).
        bool m_SkipCache = false;
    };

    struct NV_API RHI_ShaderCompileResult {
        bool m_Success = false;
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;

        std::vector<uint32_t> m_Spirv;
        std::string m_Source;
        std::string m_Log;

        std::filesystem::file_time_type m_LastWriteTime{};
    };

    class NV_API RHI_ShaderCompiler {
    public:
        static RHI_ShaderCompileResult Compile(const RHI_ShaderCompileInput& input);

    private:
        static std::string ComputeHash(const RHI_ShaderCompileInput& input);
        static bool NeedsRecompile(const RHI_ShaderCompileInput& input, const std::string& hash);

        static bool LoadCache(const std::string& hash, RHI_ShaderCompileResult& out);
        static void SaveCache(const std::string& hash, const RHI_ShaderCompileResult& result);

        static std::filesystem::path GetCacheDirectory();
    };

    NV_API bool ReadTextFile(const std::filesystem::path& path, std::string& outText, std::string& outError);

    NV_API RHI_ShaderStage ShaderStageFromFileExtension(const std::filesystem::path& filePath);
    NV_API const char* ShaderStageToString(RHI_ShaderStage stage);

    NV_API bool EnsureSlangInitialized();
    NV_API void ShutdownSlang();

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_COMPILER_H
