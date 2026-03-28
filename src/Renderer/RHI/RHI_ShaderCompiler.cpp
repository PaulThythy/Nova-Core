#include "Renderer/RHI/RHI_ShaderCompiler.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string_view>

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace Nova::Core::Renderer::RHI {

    namespace {

        std::mutex g_SlangMutex;
        Slang::ComPtr<slang::IGlobalSession> g_GlobalSession;

        static std::string PathStringSlang(const std::filesystem::path& p) {
            std::string s = p.generic_string();
            return s;
        }

        static std::string BlobToString(ISlangBlob* blob) {
            if (!blob) {
                return {};
            }
            const char* ptr = static_cast<const char*>(blob->getBufferPointer());
            const size_t size = blob->getBufferSize();
            if (!ptr || size == 0) {
                return {};
            }
            return std::string(ptr, ptr + size);
        }

        static bool EnsureGlobalSessionLocked(std::string& outErr) {
            if (g_GlobalSession) {
                return true;
            }
            SlangGlobalSessionDesc desc{};
            desc.structureSize = sizeof(desc);
            desc.enableGLSL = true; // required for `import glsl` (inverse, etc.)
            const SlangResult res = slang::createGlobalSession(&desc, g_GlobalSession.writeRef());
            if (SLANG_FAILED(res)) {
                outErr = "slang::createGlobalSession failed";
                return false;
            }
            return true;
        }

        static SlangStage ToSlangStage(RHI_ShaderStage stage) {
            switch (stage) {
                case RHI_ShaderStage::Vertex:          return SLANG_STAGE_VERTEX;
                case RHI_ShaderStage::Fragment:        return SLANG_STAGE_FRAGMENT;
                case RHI_ShaderStage::Geometry:        return SLANG_STAGE_GEOMETRY;
                case RHI_ShaderStage::TessControl:     return SLANG_STAGE_HULL;
                case RHI_ShaderStage::TessEvaluation:  return SLANG_STAGE_DOMAIN;
                case RHI_ShaderStage::Compute:         return SLANG_STAGE_COMPUTE;
                case RHI_ShaderStage::RayGen:          return SLANG_STAGE_RAY_GENERATION;
                case RHI_ShaderStage::RayMiss:         return SLANG_STAGE_MISS;
                case RHI_ShaderStage::RayClosestHit:   return SLANG_STAGE_CLOSEST_HIT;
                case RHI_ShaderStage::RayAnyHit:       return SLANG_STAGE_ANY_HIT;
                case RHI_ShaderStage::RayIntersection: return SLANG_STAGE_INTERSECTION;
                case RHI_ShaderStage::RayCallable:     return SLANG_STAGE_CALLABLE;
                default:                               return SLANG_STAGE_NONE;
            }
        }

        static bool EndsWithIgnoreCase(std::string_view s, std::string_view suffix) {
            if (suffix.size() > s.size()) {
                return false;
            }
            for (size_t i = 0; i < suffix.size(); ++i) {
                const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(s[s.size() - suffix.size() + i])));
                const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(suffix[i])));
                if (a != b) {
                    return false;
                }
            }
            return true;
        }

    } // namespace

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
        const std::string name = filePath.filename().string();
        if (EndsWithIgnoreCase(name, ".vert.slang")) return RHI_ShaderStage::Vertex;
        if (EndsWithIgnoreCase(name, ".frag.slang")) return RHI_ShaderStage::Fragment;
        if (EndsWithIgnoreCase(name, ".geom.slang")) return RHI_ShaderStage::Geometry;
        if (EndsWithIgnoreCase(name, ".tesc.slang")) return RHI_ShaderStage::TessControl;
        if (EndsWithIgnoreCase(name, ".tese.slang")) return RHI_ShaderStage::TessEvaluation;
        if (EndsWithIgnoreCase(name, ".comp.slang")) return RHI_ShaderStage::Compute;
        if (EndsWithIgnoreCase(name, ".rgen.slang")) return RHI_ShaderStage::RayGen;
        if (EndsWithIgnoreCase(name, ".rmiss.slang")) return RHI_ShaderStage::RayMiss;
        if (EndsWithIgnoreCase(name, ".rchit.slang")) return RHI_ShaderStage::RayClosestHit;
        if (EndsWithIgnoreCase(name, ".ahit.slang")) return RHI_ShaderStage::RayAnyHit;
        if (EndsWithIgnoreCase(name, ".rint.slang")) return RHI_ShaderStage::RayIntersection;
        if (EndsWithIgnoreCase(name, ".rcall.slang")) return RHI_ShaderStage::RayCallable;
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

    bool EnsureSlangInitialized() {
        std::lock_guard<std::mutex> lock(g_SlangMutex);
        std::string err;
        return EnsureGlobalSessionLocked(err);
    }

    void ShutdownSlang() {
        std::lock_guard<std::mutex> lock(g_SlangMutex);
        g_GlobalSession.setNull();
        slang::shutdown();
    }

    bool CompileShader(const RHI_ShaderDesc& desc, const RHI_ShaderCompileOptions& options, RHI_ShaderCompilationOutput& out) {
        out = RHI_ShaderCompilationOutput{};
        out.m_TargetApi = options.m_TargetApi;

        RHI_ShaderStage stage = desc.m_Stage;
        if (stage == RHI_ShaderStage::Unknown) {
            stage = ShaderStageFromFileExtension(desc.m_FilePath);
        }

        out.m_Stage = stage;
        if (stage == RHI_ShaderStage::Unknown) {
            out.m_Log = "Cannot infer shader stage from file name (expected e.g. *.vert.slang): " + desc.m_FilePath.string();
            return false;
        }

        const SlangStage slangStage = ToSlangStage(stage);
        if (slangStage == SLANG_STAGE_NONE) {
            out.m_Log = "Invalid shader stage";
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

        std::lock_guard<std::mutex> lock(g_SlangMutex);

        std::string err;
        if (!EnsureGlobalSessionLocked(err)) {
            out.m_Log = err;
            return false;
        }

        slang::IGlobalSession* const globalSession = g_GlobalSession.get();

        Slang::ComPtr<slang::ISession> session;

        slang::TargetDesc targetDesc{};
        targetDesc.structureSize = sizeof(targetDesc);
        targetDesc.format = SLANG_SPIRV;
        targetDesc.profile = globalSession->findProfile("glsl_450");
        if (targetDesc.profile == SLANG_PROFILE_UNKNOWN) {
            targetDesc.profile = globalSession->findProfile("spirv_1_5");
        }

        std::vector<std::string> searchPathStorage;
        {
            const std::filesystem::path parent =
                desc.m_FilePath.has_parent_path() ? desc.m_FilePath.parent_path() : std::filesystem::current_path();
            searchPathStorage.push_back(PathStringSlang(parent));
            for (const auto& inc : options.m_IncludeDirs) {
                searchPathStorage.push_back(PathStringSlang(inc));
            }
        }

        std::vector<const char*> searchPathPtrs;
        searchPathPtrs.reserve(searchPathStorage.size());
        for (const auto& s : searchPathStorage) {
            searchPathPtrs.push_back(s.c_str());
        }

        std::vector<slang::PreprocessorMacroDesc> macroStorage;
        macroStorage.reserve(options.m_Definitions.size());
        for (const auto& def : options.m_Definitions) {
            slang::PreprocessorMacroDesc m{};
            m.name = def.first.c_str();
            m.value = def.second.empty() ? "1" : def.second.c_str();
            macroStorage.push_back(m);
        }

        slang::SessionDesc sessionDesc{};
        sessionDesc.structureSize = sizeof(sessionDesc);
        sessionDesc.targets = &targetDesc;
        sessionDesc.targetCount = 1;
        sessionDesc.searchPaths = searchPathPtrs.empty() ? nullptr : searchPathPtrs.data();
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPtrs.size());
        sessionDesc.preprocessorMacros = macroStorage.empty() ? nullptr : macroStorage.data();
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macroStorage.size());
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        const SlangResult sessionRes = globalSession->createSession(sessionDesc, session.writeRef());
        if (SLANG_FAILED(sessionRes)) {
            out.m_Log = "slang ISession::createSession failed";
            return false;
        }

        const std::string moduleName = desc.m_FilePath.stem().string();

        Slang::ComPtr<ISlangBlob> diagLoad;
        slang::IModule* const rawModule = session->loadModule(moduleName.c_str(), diagLoad.writeRef());
        std::string log = BlobToString(diagLoad.get());

        if (!rawModule) {
            out.m_Log = log.empty() ? ("loadModule failed for: " + moduleName) : log;
            return false;
        }
        Slang::ComPtr<slang::IModule> module(rawModule);

        Slang::ComPtr<ISlangBlob> diagEp;
        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        const SlangResult epRes = module->findAndCheckEntryPoint(
            desc.m_EntryPoint.c_str(),
            slangStage,
            entryPoint.writeRef(),
            diagEp.writeRef());
        log += BlobToString(diagEp.get());
        if (SLANG_FAILED(epRes) || !entryPoint) {
            out.m_Log = log.empty() ? "findAndCheckEntryPoint failed" : log;
            return false;
        }

        slang::IComponentType* components[] = { module.get(), entryPoint.get() };
        Slang::ComPtr<slang::IComponentType> program;
        Slang::ComPtr<ISlangBlob> diagCompose;
        const SlangResult compRes =
            session->createCompositeComponentType(components, 2, program.writeRef(), diagCompose.writeRef());
        log += BlobToString(diagCompose.get());
        if (SLANG_FAILED(compRes) || !program) {
            out.m_Log = log.empty() ? "createCompositeComponentType failed" : log;
            return false;
        }

        std::vector<slang::CompilerOptionEntry> linkOpts;
        {
            slang::CompilerOptionEntry optDbg{};
            optDbg.name = slang::CompilerOptionName::DebugInformation;
            optDbg.value.kind = slang::CompilerOptionValueKind::Int;
            optDbg.value.intValue0 =
                options.m_DebugInfo ? SLANG_DEBUG_INFO_LEVEL_STANDARD : SLANG_DEBUG_INFO_LEVEL_NONE;
            linkOpts.push_back(optDbg);
        }
        {
            slang::CompilerOptionEntry optOpt{};
            optOpt.name = slang::CompilerOptionName::Optimization;
            optOpt.value.kind = slang::CompilerOptionValueKind::Int;
            optOpt.value.intValue0 =
                options.m_Optimize ? SLANG_OPTIMIZATION_LEVEL_HIGH : SLANG_OPTIMIZATION_LEVEL_NONE;
            linkOpts.push_back(optOpt);
        }

        Slang::ComPtr<slang::IComponentType> linked;
        Slang::ComPtr<ISlangBlob> diagLink;
        const SlangResult linkRes = program->linkWithOptions(
            linked.writeRef(),
            static_cast<uint32_t>(linkOpts.size()),
            linkOpts.data(),
            diagLink.writeRef());
        log += BlobToString(diagLink.get());
        if (SLANG_FAILED(linkRes) || !linked) {
            out.m_Log = log.empty() ? "linkWithOptions failed" : log;
            return false;
        }

        Slang::ComPtr<ISlangBlob> codeBlob;
        Slang::ComPtr<ISlangBlob> diagCode;
        const SlangResult codeRes = linked->getEntryPointCode(0, 0, codeBlob.writeRef(), diagCode.writeRef());
        log += BlobToString(diagCode.get());

        if (SLANG_FAILED(codeRes) || !codeBlob) {
            out.m_Log = log.empty() ? "getEntryPointCode failed" : log;
            return false;
        }

        const uint8_t* spirvBytes = static_cast<const uint8_t*>(codeBlob->getBufferPointer());
        const size_t spirvSize = codeBlob->getBufferSize();
        if (!spirvBytes || spirvSize < 4 || (spirvSize % 4) != 0) {
            out.m_Log = log + "\nInvalid SPIR-V blob size\n";
            return false;
        }

        out.m_Spirv.resize(spirvSize / sizeof(uint32_t));
        std::memcpy(out.m_Spirv.data(), spirvBytes, spirvSize);

        out.m_Source = std::move(source);
        out.m_Log = log;
        out.m_Success = true;
        return true;
    }

} // namespace Nova::Core::Renderer::RHI
