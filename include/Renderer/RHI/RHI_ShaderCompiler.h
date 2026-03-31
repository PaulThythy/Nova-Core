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

    // -----------------------------------------------------------------------------
    // Slang → SPIR-V compilation for Nova (.slang modules, Vulkan-style SPIR-V).
    // Global Slang state is lazily initialized; call ShutdownSlang() on teardown if
    // you need a clean shutdown (optional for most apps).
    // -----------------------------------------------------------------------------

    enum class RHI_ShaderStage {
        Unknown = 0,
        Vertex,           // *.vert.slang
        Fragment,         // *.frag.slang
        Geometry,         // *.geom.slang
        TessControl,      // *.tesc.slang
        TessEvaluation,   // *.tese.slang
        Compute,          // *.comp.slang
        RayGen,           // *.rgen.slang
        RayMiss,          // *.rmiss.slang
        RayClosestHit,    // *.rchit.slang
        RayAnyHit,        // *.ahit.slang
        RayIntersection,  // *.rint.slang
        RayCallable       // *.rcall.slang
    };

    /// Input: path to a .slang file (module name = path stem) and optional stage override.
    struct NV_API RHI_ShaderDesc {
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        std::filesystem::path m_FilePath;
        std::string m_EntryPoint = "main";
    };

    /// Options passed to Slang (SPIR-V target, defines, include paths, optimization).
    struct NV_API RHI_ShaderCompileOptions {
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;
        bool m_DebugInfo = false;
        bool m_Optimize = true;

        std::vector<std::filesystem::path> m_IncludeDirs;
        /// Preprocessor macros: name → value (empty value becomes "1").
        std::vector<std::pair<std::string, std::string>> m_Definitions;
    };

    /// Result of compiling a single stage to SPIR-V words (plus source copy and diagnostics).
    struct NV_API RHI_ShaderCompilationOutput {
        bool m_Success = false;
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;

        std::vector<uint32_t> m_Spirv;
        /// Full .slang source read from disk (debug / hot-reload).
        std::string m_Source;
        std::string m_Log;
    };

    NV_API bool ReadTextFile(const std::filesystem::path& path, std::string& outText, std::string& outError);

    NV_API RHI_ShaderStage ShaderStageFromFileExtension(const std::filesystem::path& filePath);
    NV_API const char* ShaderStageToString(RHI_ShaderStage stage);

    /// Lazily creates the Slang global session (thread-safe). Safe to call multiple times.
    NV_API bool EnsureSlangInitialized();
    /// Releases the global session and calls slang::shutdown(). Not required on process exit.
    NV_API void ShutdownSlang();

    /// Compiles one .slang file to SPIR-V for the active target (SPIR-V, glsl_450 profile).
    NV_API bool CompileShader(
        const RHI_ShaderDesc& desc,
        const RHI_ShaderCompileOptions& options,
        RHI_ShaderCompilationOutput& out);

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_COMPILER_H
