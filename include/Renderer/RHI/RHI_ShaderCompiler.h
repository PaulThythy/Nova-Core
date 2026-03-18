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
        Vertex,                                     // .vert
        Fragment,                                   // .frag
        Geometry,                                   // .geom
        TessControl,                                // .tesc
        TessEvaluation,                             // .tese
        Compute,                                    // .comp

        // for raytracing pipeline shaders
        RayGen,                                     // .rgen
        RayMiss,                                    // .rmiss
        RayClosestHit,                              // .rchit
        RayAnyHit,                                  // .ahit
        RayIntersection,                            // .rint
        RayCallable                                 // .rcall
    };

    struct NV_API RHI_ShaderDesc {
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        std::filesystem::path m_FilePath;
        std::string m_EntryPoint = "main";
        int m_GlslVersion = 130;
    };

    struct NV_API RHI_ShaderCompileOptions {
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;
        bool m_DebugInfo = false;
        bool m_Optimize = true;

        std::vector<std::filesystem::path> m_IncludeDirs;
        std::vector<std::pair<std::string, std::string>> m_Definitions;
    };

    struct NV_API RHI_ShaderCompilationOutput {
        bool m_Success = false;
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;

        std::vector<uint32_t> m_Spirv;
        std::string m_Glsl;
        std::string m_Log;
    };

    NV_API bool ReadTextFile(const std::filesystem::path& path, std::string& outText, std::string& outError);

    NV_API RHI_ShaderStage ShaderStageFromFileExtension(const std::filesystem::path& filePath);
    NV_API const char* ShaderStageToString(RHI_ShaderStage stage);

    NV_API bool EnsureGlslangInitialized();
    NV_API void ShutdownGlslang();

    NV_API bool CompileShader(const RHI_ShaderDesc& desc, const RHI_ShaderCompileOptions& options, RHI_ShaderCompilationOutput& out);

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADER_COMPILER_H
