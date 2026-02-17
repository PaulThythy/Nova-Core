#include "Renderer/RHI/RHI_Shaders.h"

#include <algorithm>
#include <fstream>
#include <mutex>
#include <sstream>
#include <cstring>

namespace Nova::Core::Renderer::RHI {

    std::mutex g_GlslangMutex;
    int g_GlslangRefCount = 0;

    //TODO uncomment this function
    /*const TBuiltInResource& GetDefaultResources() {
        return DefaultTBuiltInResource;
    }*/

    struct ScopedGlslangProcess {
        bool ok = false;
        ScopedGlslangProcess();
        ~ScopedGlslangProcess();
    };

    ScopedGlslangProcess::ScopedGlslangProcess() {
        std::lock_guard<std::mutex> lock(g_GlslangMutex);
        if (g_GlslangRefCount == 0) {
            ok = glslang::InitializeProcess();
        }
        else {
            ok = true;
        }

        if (ok)
            ++g_GlslangRefCount;
    }

    ScopedGlslangProcess::~ScopedGlslangProcess() {
        std::lock_guard<std::mutex> lock(g_GlslangMutex);
        if (!ok)
            return;

        --g_GlslangRefCount;
        if (g_GlslangRefCount <= 0) {
            g_GlslangRefCount = 0;
            glslang::FinalizeProcess();
        }
    }

    std::string MakePreamble(const Nova::Core::Renderer::RHI::RHI_ShaderCompileOptions& options) {
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

    //TODO use my log system to print errors
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
        std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return (char)std::tolower(c); });

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

    static EShLanguage ShaderStageToEShLanguage(RHI_ShaderStage stage) {
        using Stage = Nova::Core::Renderer::RHI::RHI_ShaderStage;
        switch (stage) {
            case Stage::Vertex:             return EShLangVertex;
            case Stage::Fragment:           return EShLangFragment;
            case Stage::Geometry:           return EShLangGeometry;
            case Stage::TessControl:        return EShLangTessControl;
            case Stage::TessEvaluation:     return EShLangTessEvaluation;
            case Stage::Compute:            return EShLangCompute;
            case Stage::RayGen:             return EShLangRayGen;
            case Stage::RayMiss:            return EShLangMiss;
            case Stage::RayClosestHit:      return EShLangClosestHit;
            case Stage::RayAnyHit:          return EShLangAnyHit;
            case Stage::RayIntersection:    return EShLangIntersect;
            case Stage::RayCallable:        return EShLangCallable;
            default:                        return EShLangVertex;
        }
    }

    const char* ShaderStageToString(RHI_ShaderStage stage) {
        using Stage = Nova::Core::Renderer::RHI::RHI_ShaderStage;
        switch (stage) {
            case Stage::Vertex:             return "Vertex";
            case Stage::Fragment:           return "Fragment";
            case Stage::Geometry:           return "Geometry";
            case Stage::TessControl:        return "Tessellation Control";
            case Stage::TessEvaluation:     return "Tessellation Evaluation";
            case Stage::Compute:            return "Compute";
            case Stage::RayGen:             return "Ray Generation";
            case Stage::RayMiss:            return "Ray Miss";
            case Stage::RayClosestHit:      return "Ray Closest Hit";
            case Stage::RayAnyHit:          return "Ray Any Hit";
            case Stage::RayIntersection:    return "Ray Intersection";
            case Stage::RayCallable:        return "Ray Callable";
            default:                        return "Unknown";
        }
    }

    bool EnsureGlslangInitialized() {
        std::lock_guard<std::mutex> lock(g_GlslangMutex);
        if (g_GlslangRefCount == 0) {
            if (!glslang::InitializeProcess())
                return false;
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

        const char* strings[] = { source.c_str() };
        shader.setStrings(strings, 1);
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
            messages = (EShMessages)(EShMsgSpvRules | EShMsgVulkanRules);
        }
        else {
            shader.setEnvInput(glslang::EShSourceGlsl, lang, glslang::EShClientOpenGL, 450);
            shader.setEnvClient(glslang::EShClientOpenGL, glslang::EShTargetOpenGL_450);
            shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_0);
            messages = (EShMessages)(EShMsgSpvRules);
        }


        RHI_ShaderFileIncluder includer(
            desc.m_FilePath.has_parent_path() ? desc.m_FilePath.parent_path() : std::filesystem::current_path(),
            options.m_IncludeDirs
        );

        const TBuiltInResource& resources = *GetDefaultResources();
        const bool parsed = shader.parse(&resources,
            desc.m_GlslVersion,
            ENoProfile,
            false,
            false,
            messages,
            includer);

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

        // Outputs
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

        // OpenGL path: return the validated GLSL.
        out.m_Glsl = std::move(source);
        out.m_Success = true;
        return true;
    }

    glslang::TShader::Includer::IncludeResult* RHI_ShaderFileIncluder::includeSystem(const char* headerName, const char* includerName, size_t inclusionDepth) {
        // Try to resolve the header path directly; return nullptr if not found or cannot be read.
        std::filesystem::path path(headerName);
        if (!std::filesystem::exists(path)) {
            return nullptr;
        }

        std::string content;
        std::string error;
        if (!ReadTextFile(path, content, error)) {
            return nullptr;
        }

        // Allocate copies for the includer result (glslang expects ownership to be transferred).
        char* contentCopy = new char[content.size() + 1];
        std::memcpy(contentCopy, content.c_str(), content.size() + 1);

        const std::string headerPathStr = path.string();
        char* headerCopy = new char[headerPathStr.size() + 1];
        std::memcpy(headerCopy, headerPathStr.c_str(), headerPathStr.size() + 1);

        return new glslang::TShader::Includer::IncludeResult(headerCopy, contentCopy, content.size(), nullptr);
    }

    glslang::TShader::Includer::IncludeResult* RHI_ShaderFileIncluder::includeLocal(const char* headerName, const char* includerName, size_t inclusionDepth) {
        (void)inclusionDepth;

        // First try relative to the file doing the include.
        if (includerName && includerName[0] != '\0') {
            const std::filesystem::path includerPath(includerName);
            const std::filesystem::path includerDir = includerPath.has_parent_path() ? includerPath.parent_path() : m_SourceDir;

            if (IncludeResult* r = TryIncludeInDir(headerName, includerDir))
                return r;
        }

        // Then try relative to the root source directory.
        if (IncludeResult* r = TryIncludeInDir(headerName, m_SourceDir))
            return r;

        // Finally try include dirs.
        return TryInclude(headerName, /*isSystem*/false);
    }

    void RHI_ShaderFileIncluder::releaseInclude(glslang::TShader::Includer::IncludeResult* result) {
        if (!result)
            return;

        // userData holds the allocated std::string
        delete reinterpret_cast<std::string*>(result->userData);
        delete result;
    }

    glslang::TShader::Includer::IncludeResult* RHI_ShaderFileIncluder::TryInclude(const char* headerName, bool isSystem) {
        (void)isSystem;
        for (const auto& dir : m_IncludeDirs) {
            if (glslang::TShader::Includer::IncludeResult* r = TryIncludeInDir(headerName, dir))
                return r;
        }
        return nullptr;
    }

    glslang::TShader::Includer::IncludeResult* RHI_ShaderFileIncluder::TryIncludeInDir(const char* headerName, const std::filesystem::path& dir) {
        if (!headerName || headerName[0] == '\0')
            return nullptr;

        const std::filesystem::path candidate = dir / headerName;
        std::error_code ec;
        if (!std::filesystem::exists(candidate, ec) || ec)
            return nullptr;

        std::string* content = new std::string();
        std::string error;
        if (!ReadTextFile(candidate, *content, error)) {
            delete content;
            return nullptr;
        }

        // glslang will call releaseInclude; we keep the content alive via userData.
        return new glslang::TShader::Includer::IncludeResult(candidate.string(), content->c_str(), content->size(), content);
    }

} // namespace Nova::Core::Renderer::RHI