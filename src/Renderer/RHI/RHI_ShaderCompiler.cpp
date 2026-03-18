#include "Renderer/RHI/RHI_ShaderCompiler.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <mutex>
#include <sstream>

#include <glslang/Public/ResourceLimits.h>
#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>

namespace Nova::Core::Renderer::RHI {

    std::mutex g_GlslangMutex;
    int g_GlslangRefCount = 0;

    struct ScopedGlslangProcess {
        bool ok = false;

        ScopedGlslangProcess() {
            std::lock_guard<std::mutex> lock(g_GlslangMutex);
            if (g_GlslangRefCount == 0) {
                ok = glslang::InitializeProcess();
            }
            else {
                ok = true;
            }

            if (ok) {
                ++g_GlslangRefCount;
            }
        }

        ~ScopedGlslangProcess() {
            std::lock_guard<std::mutex> lock(g_GlslangMutex);
            if (!ok) {
                return;
            }

            --g_GlslangRefCount;
            if (g_GlslangRefCount <= 0) {
                g_GlslangRefCount = 0;
                glslang::FinalizeProcess();
            }
        }
    };

    std::string MakePreamble(const RHI_ShaderCompileOptions& options) {
        std::string preamble;
        for (const auto& def : options.m_Definitions) {
            preamble += "#define ";
            preamble += def.first;
            if (!def.second.empty()) {
                preamble += " ";
                preamble += def.second;
            }
            preamble += "\n";
        }
        return preamble;
    }

    EShLanguage ShaderStageToEShLanguage(RHI_ShaderStage stage) {
        switch (stage) {
            case RHI_ShaderStage::Vertex:          return EShLangVertex;
            case RHI_ShaderStage::Fragment:        return EShLangFragment;
            case RHI_ShaderStage::Geometry:        return EShLangGeometry;
            case RHI_ShaderStage::TessControl:     return EShLangTessControl;
            case RHI_ShaderStage::TessEvaluation:  return EShLangTessEvaluation;
            case RHI_ShaderStage::Compute:         return EShLangCompute;
            case RHI_ShaderStage::RayGen:          return EShLangRayGen;
            case RHI_ShaderStage::RayMiss:         return EShLangMiss;
            case RHI_ShaderStage::RayClosestHit:   return EShLangClosestHit;
            case RHI_ShaderStage::RayAnyHit:       return EShLangAnyHit;
            case RHI_ShaderStage::RayIntersection: return EShLangIntersect;
            case RHI_ShaderStage::RayCallable:     return EShLangCallable;
            default:                               return EShLangVertex;
        }
    }

    class RHI_ShaderFileIncluder final : public glslang::TShader::Includer {
    public:
        RHI_ShaderFileIncluder(std::filesystem::path sourceDir, std::vector<std::filesystem::path> includeDirs)
            : m_SourceDir(std::move(sourceDir)), m_IncludeDirs(std::move(includeDirs)) {}

        IncludeResult* includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) override {
            (void)includerName;
            (void)inclusionDepth;

            std::filesystem::path path(headerName ? headerName : "");
            if (path.is_absolute()) {
                if (auto* result = TryIncludeInDir(path.filename().string().c_str(), path.parent_path())) {
                    return result;
                }
            }

            return TryInclude(headerName);
        }

        IncludeResult* includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) override {
            (void)inclusionDepth;

            if (includerName && includerName[0] != '\0') {
                const std::filesystem::path includerPath(includerName);
                const std::filesystem::path includerDir =
                    includerPath.has_parent_path() ? includerPath.parent_path() : m_SourceDir;

                if (IncludeResult* result = TryIncludeInDir(headerName, includerDir)) {
                    return result;
                }
            }

            if (IncludeResult* result = TryIncludeInDir(headerName, m_SourceDir)) {
                return result;
            }

            return TryInclude(headerName);
        }

        void releaseInclude(IncludeResult* result) override {
            if (!result) {
                return;
            }

            delete reinterpret_cast<std::string*>(result->userData);
            delete result;
        }

    private:
        IncludeResult* TryInclude(const char* headerName) {
            for (const auto& dir : m_IncludeDirs) {
                if (IncludeResult* result = TryIncludeInDir(headerName, dir)) {
                    return result;
                }
            }
            return nullptr;
        }

        IncludeResult* TryIncludeInDir(const char* headerName, const std::filesystem::path& dir) {
            if (!headerName || headerName[0] == '\0') {
                return nullptr;
            }

            const std::filesystem::path candidate = dir / headerName;
            std::error_code ec;
            if (!std::filesystem::exists(candidate, ec) || ec) {
                return nullptr;
            }

            std::string* content = new std::string();
            std::string error;
            if (!ReadTextFile(candidate, *content, error)) {
                delete content;
                return nullptr;
            }

            return new IncludeResult(candidate.string(), content->c_str(), content->size(), content);
        }

        std::filesystem::path m_SourceDir;
        std::vector<std::filesystem::path> m_IncludeDirs;
    };

    bool ReadTextFile(const std::filesystem::path& path, std::string& outText, std::string& outError) {
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

    RHI_ShaderStage ShaderStageFromFileExtension(const std::filesystem::path& filePath) {
        std::string ext = filePath.extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });

        if (ext == ".vert") return RHI_ShaderStage::Vertex;
        if (ext == ".frag") return RHI_ShaderStage::Fragment;
        if (ext == ".geom") return RHI_ShaderStage::Geometry;
        if (ext == ".tesc") return RHI_ShaderStage::TessControl;
        if (ext == ".tese") return RHI_ShaderStage::TessEvaluation;
        if (ext == ".comp") return RHI_ShaderStage::Compute;
        if (ext == ".rgen") return RHI_ShaderStage::RayGen;
        if (ext == ".rmiss") return RHI_ShaderStage::RayMiss;
        if (ext == ".rchit") return RHI_ShaderStage::RayClosestHit;
        if (ext == ".ahit") return RHI_ShaderStage::RayAnyHit;
        if (ext == ".rint") return RHI_ShaderStage::RayIntersection;
        if (ext == ".rcall") return RHI_ShaderStage::RayCallable;
        return RHI_ShaderStage::Unknown;
    }

    const char* ShaderStageToString(RHI_ShaderStage stage) {
        switch (stage) {
            case RHI_ShaderStage::Vertex:          return "Vertex";
            case RHI_ShaderStage::Fragment:        return "Fragment";
            case RHI_ShaderStage::Geometry:        return "Geometry";
            case RHI_ShaderStage::TessControl:     return "Tessellation Control";
            case RHI_ShaderStage::TessEvaluation:  return "Tessellation Evaluation";
            case RHI_ShaderStage::Compute:         return "Compute";
            case RHI_ShaderStage::RayGen:          return "Ray Generation";
            case RHI_ShaderStage::RayMiss:         return "Ray Miss";
            case RHI_ShaderStage::RayClosestHit:   return "Ray Closest Hit";
            case RHI_ShaderStage::RayAnyHit:       return "Ray Any Hit";
            case RHI_ShaderStage::RayIntersection: return "Ray Intersection";
            case RHI_ShaderStage::RayCallable:     return "Ray Callable";
            default:                               return "Unknown";
        }
    }

    bool EnsureGlslangInitialized() {
        std::lock_guard<std::mutex> lock(g_GlslangMutex);
        if (g_GlslangRefCount == 0) {
            if (!glslang::InitializeProcess()) {
                return false;
            }
        }
        ++g_GlslangRefCount;
        return true;
    }

    void ShutdownGlslang() {
        std::lock_guard<std::mutex> lock(g_GlslangMutex);
        --g_GlslangRefCount;
        if (g_GlslangRefCount <= 0) {
            g_GlslangRefCount = 0;
            glslang::FinalizeProcess();
        }
    }

    bool CompileShader(const RHI_ShaderDesc& desc, const RHI_ShaderCompileOptions& options, RHI_ShaderCompilationOutput& out) {
        out = RHI_ShaderCompilationOutput{};
        out.m_TargetApi = options.m_TargetApi;

        RHI_ShaderStage stage = desc.m_Stage;
        if (stage == RHI_ShaderStage::Unknown && desc.m_FilePath.has_extension()) {
            stage = ShaderStageFromFileExtension(desc.m_FilePath);
        }

        out.m_Stage = stage;
        if (stage == RHI_ShaderStage::Unknown) {
            out.m_Log = "Cannot infer shader stage from extension: " + desc.m_FilePath.string();
            return false;
        }

        std::string source;
        {
            std::string error;
            if (!ReadTextFile(desc.m_FilePath, source, error)) {
                out.m_Log = error;
                return false;
            }
        }

        ScopedGlslangProcess scoped;
        if (!scoped.ok) {
            out.m_Log = "glslang::InitializeProcess() failed";
            return false;
        }

        const EShLanguage lang = ShaderStageToEShLanguage(stage);
        glslang::TShader shader(lang);

        const std::string fileNameStr = desc.m_FilePath.string();
        const char* strings[] = { source.c_str() };
        const int lengths[] = { static_cast<int>(source.size()) };
        const char* names[] = { fileNameStr.c_str() };

        shader.setStringsWithLengthsAndNames(strings, lengths, names, 1);
        shader.setEntryPoint(desc.m_EntryPoint.c_str());

        const std::string preamble = MakePreamble(options);
        if (!preamble.empty()) {
            shader.setPreamble(preamble.c_str());
        }

        EShMessages messages = EShMsgDefault;
        if (options.m_TargetApi == GraphicsAPI::Vulkan) {
            shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientVulkan, 100);
            shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_3);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
            messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
        }
        else {
            shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientOpenGL, 450);
            shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
            messages = static_cast<EShMessages>(EShMsgSpvRules);
        }

        RHI_ShaderFileIncluder includer(
            desc.m_FilePath.has_parent_path() ? desc.m_FilePath.parent_path() : std::filesystem::current_path(),
            options.m_IncludeDirs
        );

        const TBuiltInResource& resources = *GetDefaultResources();
        const bool parsed = shader.parse(
            &resources,
            desc.m_GlslVersion,
            ENoProfile,
            false,
            false,
            messages,
            includer
        );

        std::string log;
        if (shader.getInfoLog() && shader.getInfoLog()[0] != '\0') {
            log += shader.getInfoLog();
            log += "\n";
        }
        if (shader.getInfoDebugLog() && shader.getInfoDebugLog()[0] != '\0') {
            log += shader.getInfoDebugLog();
            log += "\n";
        }

        if (!parsed) {
            out.m_Log = log.empty() ? "Shader parse failed" : log;
            return false;
        }

        glslang::TProgram program;
        program.addShader(&shader);

        const bool linked = program.link(messages);
        if (program.getInfoLog() && program.getInfoLog()[0] != '\0') {
            log += program.getInfoLog();
            log += "\n";
        }
        if (program.getInfoDebugLog() && program.getInfoDebugLog()[0] != '\0') {
            log += program.getInfoDebugLog();
            log += "\n";
        }

        if (!linked) {
            out.m_Log = log.empty() ? "Program link failed" : log;
            return false;
        }

        out.m_Log = log;

        const glslang::TIntermediate* intermediate = program.getIntermediate(lang);
        if (!intermediate) {
            out.m_Log += "No intermediate representation available\n";
            return false;
        }

        glslang::SpvOptions spvOptions{};
        spvOptions.generateDebugInfo = options.m_DebugInfo;
        spvOptions.disableOptimizer = !options.m_Optimize;

        try {
            glslang::GlslangToSpv(*intermediate, out.m_Spirv, &spvOptions);
        }
        catch (...) {
            out.m_Log += "GlslangToSpv threw an exception\n";
            return false;
        }

        if (out.m_Spirv.empty()) {
            out.m_Log += "SPIR-V output is empty\n";
            return false;
        }

        out.m_Glsl = std::move(source);
        out.m_Success = true;
        return true;
    }

} // namespace Nova::Core::Renderer::RHI
