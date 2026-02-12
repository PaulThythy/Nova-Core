#ifndef RHI_SHADERS_H
#define RHI_SHADERS_H

#include <string>
#include <vector>
#include <filesystem>

#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

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

    struct RHI_ShaderDesc {
        RHI_ShaderStage m_Type = RHI_ShaderStage::Unknown;
        std::filesystem::path m_FilePath;
        std::string m_EntryPoint = "main"; // Optional: default to "main"
        int m_GlslVersion = 130;
    };

    struct RHI_ShaderCompileOptions {
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;
        bool m_DebugInfo = false;
        bool m_Optimize = true;

        // Optional include search paths used for #include "..."
        std::vector<std::filesystem::path> m_IncludeDirs;

        // Optional preprocessor definitions injected as a preamble.
        // Each pair is (name, value). Value can be empty.
        std::vector<std::pair<std::string, std::string>> m_Definitions;
    };

    struct RHI_ShaderCompilationOutput {
        bool m_Success = false;
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
        GraphicsAPI m_TargetApi = GraphicsAPI::Vulkan;

        // Vulkan path (SPIR-V output).
        std::vector<uint32_t> m_Spirv;

        // OpenGL path (validated GLSL source output).
        std::string m_Glsl;

        // Combined compiler / linker log.
        std::string m_Log;
    };

    bool ReadTextFile(const std::filesystem::path& path, std::string& outText, std::string& outError);

    RHI_ShaderStage ShaderStageFromFileExtension(const std::filesystem::path& filePath);
    const char* ShaderStageToString(RHI_ShaderStage stage);

    // Ensures glslang is initialized at least once for the process.
    bool EnsureGlslangInitialized();

    // Decrements an internal ref-count and finalizes glslang when it reaches zero.
    void ShutdownGlslang();

    // Compile a single shader file.
    //  - Vulkan: outputs SPIR-V in out.spirv
    //  - OpenGL: outputs GLSL in out.glsl (validated)
    bool CompileShader(const RHI_ShaderDesc& desc, const RHI_ShaderCompileOptions& options, RHI_ShaderCompilationOutput& out);


    class RHI_ShaderFileIncluder final : public glslang::TShader::Includer {
    public:
        RHI_ShaderFileIncluder(std::filesystem::path sourceDir, std::vector<std::filesystem::path> includeDirs): 
            m_SourceDir(std::move(sourceDir)), m_IncludeDirs(std::move(includeDirs)) {}

        IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override;
        IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override;
        void releaseInclude(IncludeResult* result) override;

    private:
        IncludeResult* TryInclude(const char* headerName, bool isSystem);
        IncludeResult* TryIncludeInDir(const char* headerName, const std::filesystem::path& dir);

        std::filesystem::path m_SourceDir;
        std::vector<std::filesystem::path> m_IncludeDirs;

    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_SHADERS_H