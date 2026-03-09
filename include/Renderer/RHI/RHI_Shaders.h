#ifndef RHI_SHADERS_H
#define RHI_SHADERS_H

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <variant>

#include <glm/glm.hpp>
#include <glslang/Include/glslang_c_interface.h>
#include <glslang/Public/ShaderLang.h>
#include <glslang/Public/ResourceLimits.h>
#include <SPIRV/GlslangToSpv.h>

#include "Core/GraphicsAPI.h"

namespace Nova::Core::Renderer::RHI {

    /** Type-safe storage for shader uniform values (name -> value). */
    using UniformValue = std::variant<
        int, float, glm::vec2, glm::vec3, glm::vec4, glm::mat2, glm::mat3, glm::mat4
    >;
    
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
        RHI_ShaderStage m_Stage = RHI_ShaderStage::Unknown;
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

    /**
     * Base class for a linked shader program (e.g. vertex + fragment).
     * Holds a map of uniform names to values; backends (GL/VK) implement
     * Bind() and ApplyParameters() to upload them to the GPU.
     */
    class RHI_Shaders {
    public:
        virtual ~RHI_Shaders() = default;

        /** Store a uniform by name. Same API for all backends. */
        void SetParameter(const std::string& name, int value);
        void SetParameter(const std::string& name, float value);
        void SetParameter(const std::string& name, const glm::vec2& value);
        void SetParameter(const std::string& name, const glm::vec3& value);
        void SetParameter(const std::string& name, const glm::vec4& value);
        void SetParameter(const std::string& name, const glm::mat2& value);
        void SetParameter(const std::string& name, const glm::mat3& value);
        void SetParameter(const std::string& name, const glm::mat4& value);

        /** Bind the shader for drawing (e.g. glUseProgram / vkCmdBindPipeline). apiContext: GL = nullptr, VK = VkCommandBuffer*. */
        virtual void Bind(void* apiContext = nullptr) = 0;
        /** Upload m_Parameters to the GPU. apiContext: GL = nullptr, VK = VkCommandBuffer*. */
        virtual void ApplyParameters(void* apiContext = nullptr) = 0;
        /** Backend-specific handle (e.g. GL program id, VkPipeline). */
        virtual void* GetNativeHandle() const = 0;

    protected:
        std::unordered_map<std::string, UniformValue> m_Parameters;
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