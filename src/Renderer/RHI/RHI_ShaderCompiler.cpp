#include "Renderer/RHI/RHI_ShaderCompiler.h"

#include <algorithm>
#include <cctype>
#include <functional>
#include <cstring>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_map>

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

namespace Nova::Core::Renderer::RHI {

    std::mutex g_Mutex;
    std::unordered_map<std::string, RHI_ShaderCompileResult> g_MemoryCache;
    Slang::ComPtr<slang::IGlobalSession> g_GlobalSession;

    std::string ToSlangPathString(const std::filesystem::path& path) {
        return path.generic_string();
    }

    void AppendBlobDiagnostics(std::string& log, ISlangBlob* blob) {
        if (!blob) {
            return;
        }
        const char* ptr = static_cast<const char*>(blob->getBufferPointer());
        const size_t size = blob->getBufferSize();
        if (ptr && size > 0) {
            log.append(ptr, ptr + size);
        }
    }

    bool EndsWithIgnoreCase(std::string_view s, std::string_view suffix) {
        if (suffix.size() > s.size()) {
            return false;
        }
        for (size_t i = 0; i < suffix.size(); ++i) {
            const unsigned char ca = static_cast<unsigned char>(s[s.size() - suffix.size() + i]);
            const unsigned char cb = static_cast<unsigned char>(suffix[i]);
            if (std::tolower(ca) != std::tolower(cb)) {
                return false;
            }
        }
        return true;
    }

    SlangStage RhiStageToSlangStage(RHI_ShaderStage stage) {
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

    bool EnsureGlobalSessionUnlocked(std::string& outErr) {
        if (g_GlobalSession) {
            return true;
        }
        SlangGlobalSessionDesc desc{};
        desc.structureSize = sizeof(desc);
        desc.enableGLSL = true;

        const SlangResult hr = slang::createGlobalSession(&desc, g_GlobalSession.writeRef());
        if (SLANG_FAILED(hr)) {
            outErr = "slang::createGlobalSession failed";
            return false;
        }
        return true;
    }

    SlangProfileID ResolveSpirvProfile(slang::IGlobalSession* global) {
        SlangProfileID profile = global->findProfile("glsl_450");
        if (profile == SLANG_PROFILE_UNKNOWN) {
            profile = global->findProfile("spirv_1_5");
        }
        return profile;
    }

    void FillLinkOptions(const RHI_ShaderCompileInput& input, std::vector<slang::CompilerOptionEntry>& outOpts) {
        outOpts.clear();
        outOpts.reserve(2);

        slang::CompilerOptionEntry dbg{};
        dbg.name = slang::CompilerOptionName::DebugInformation;
        dbg.value.kind = slang::CompilerOptionValueKind::Int;
        dbg.value.intValue0 =
            input.m_Debug ? SLANG_DEBUG_INFO_LEVEL_STANDARD : SLANG_DEBUG_INFO_LEVEL_NONE;
        outOpts.push_back(dbg);

        slang::CompilerOptionEntry opt{};
        opt.name = slang::CompilerOptionName::Optimization;
        opt.value.kind = slang::CompilerOptionValueKind::Int;
        opt.value.intValue0 =
            input.m_Optimize ? SLANG_OPTIMIZATION_LEVEL_HIGH : SLANG_OPTIMIZATION_LEVEL_NONE;
        outOpts.push_back(opt);
    }

    bool GetLinkedSpirv(
        slang::IComponentType* linked,
        std::string& log,
        std::vector<uint32_t>& outSpirv,
        std::string& outFailureMessage) {
        Slang::ComPtr<ISlangBlob> codeBlob;
        Slang::ComPtr<ISlangBlob> diagBlob;
        const SlangResult hr = linked->getEntryPointCode(0, 0, codeBlob.writeRef(), diagBlob.writeRef());
        AppendBlobDiagnostics(log, diagBlob.get());

        if (SLANG_FAILED(hr) || !codeBlob) {
            outFailureMessage = log.empty() ? "getEntryPointCode failed" : log;
            return false;
        }

        const uint8_t* bytes = static_cast<const uint8_t*>(codeBlob->getBufferPointer());
        const size_t byteSize = codeBlob->getBufferSize();
        if (!bytes || byteSize < 4 || (byteSize % 4) != 0) {
            outFailureMessage = log + "\nInvalid SPIR-V blob size\n";
            return false;
        }

        outSpirv.resize(byteSize / sizeof(uint32_t));
        std::memcpy(outSpirv.data(), bytes, byteSize);
        return true;
    }

    bool CompileSlangFileToSpirv(
        slang::IGlobalSession* global,
        const RHI_ShaderCompileInput& input,
        std::string&& fileContents,
        RHI_ShaderCompileResult& out) {
        const SlangStage slangStage = RhiStageToSlangStage(input.m_Stage);
        if (slangStage == SLANG_STAGE_NONE) {
            out.m_Log = "Invalid shader stage";
            return false;
        }

        slang::TargetDesc target{};
        target.structureSize = sizeof(target);
        target.format = SLANG_SPIRV;
        target.profile = ResolveSpirvProfile(global);

        std::vector<std::string> searchPathStrings;
        {
            const std::filesystem::path parent =
                input.m_File.has_parent_path() ? input.m_File.parent_path() : std::filesystem::current_path();
            searchPathStrings.push_back(ToSlangPathString(parent));
            for (const auto& inc : input.m_IncludeDirs) {
                searchPathStrings.push_back(ToSlangPathString(inc));
            }
        }
        std::vector<const char*> searchPathPtrs;
        searchPathPtrs.reserve(searchPathStrings.size());
        for (const auto& s : searchPathStrings) {
            searchPathPtrs.push_back(s.c_str());
        }

        std::vector<slang::PreprocessorMacroDesc> macros;
        macros.reserve(input.m_Defines.size());
        for (const auto& def : input.m_Defines) {
            slang::PreprocessorMacroDesc m{};
            m.name = def.first.c_str();
            m.value = def.second.empty() ? "1" : def.second.c_str();
            macros.push_back(m);
        }

        slang::SessionDesc sessionDesc{};
        sessionDesc.structureSize = sizeof(sessionDesc);
        sessionDesc.targets = &target;
        sessionDesc.targetCount = 1;
        sessionDesc.searchPaths = searchPathPtrs.empty() ? nullptr : searchPathPtrs.data();
        sessionDesc.searchPathCount = static_cast<SlangInt>(searchPathPtrs.size());
        sessionDesc.preprocessorMacros = macros.empty() ? nullptr : macros.data();
        sessionDesc.preprocessorMacroCount = static_cast<SlangInt>(macros.size());
        sessionDesc.defaultMatrixLayoutMode = SLANG_MATRIX_LAYOUT_COLUMN_MAJOR;

        Slang::ComPtr<slang::ISession> session;
        if (SLANG_FAILED(global->createSession(sessionDesc, session.writeRef())) || !session) {
            out.m_Log = "slang::IGlobalSession::createSession failed";
            return false;
        }

        const std::string moduleName = input.m_File.stem().string();
        std::string log;

        Slang::ComPtr<ISlangBlob> diagLoad;
        slang::IModule* rawModule = session->loadModule(moduleName.c_str(), diagLoad.writeRef());
        AppendBlobDiagnostics(log, diagLoad.get());
        if (!rawModule) {
            out.m_Log = log.empty() ? ("loadModule failed for: " + moduleName) : log;
            return false;
        }
        Slang::ComPtr<slang::IModule> module(rawModule);

        Slang::ComPtr<slang::IEntryPoint> entryPoint;
        Slang::ComPtr<ISlangBlob> diagEp;
        const SlangResult epHr = module->findAndCheckEntryPoint(
            input.m_EntryPoint.c_str(),
            slangStage,
            entryPoint.writeRef(),
            diagEp.writeRef());
        AppendBlobDiagnostics(log, diagEp.get());
        if (SLANG_FAILED(epHr) || !entryPoint) {
            out.m_Log = log.empty() ? "findAndCheckEntryPoint failed" : log;
            return false;
        }

        slang::IComponentType* parts[] = { module.get(), entryPoint.get() };
        Slang::ComPtr<slang::IComponentType> program;
        Slang::ComPtr<ISlangBlob> diagCompose;
        const SlangResult composeHr =
            session->createCompositeComponentType(parts, 2, program.writeRef(), diagCompose.writeRef());
        AppendBlobDiagnostics(log, diagCompose.get());
        if (SLANG_FAILED(composeHr) || !program) {
            out.m_Log = log.empty() ? "createCompositeComponentType failed" : log;
            return false;
        }

        std::vector<slang::CompilerOptionEntry> linkOpts;
        FillLinkOptions(input, linkOpts);

        Slang::ComPtr<slang::IComponentType> linked;
        Slang::ComPtr<ISlangBlob> diagLink;
        const SlangResult linkHr = program->linkWithOptions(
            linked.writeRef(),
            static_cast<uint32_t>(linkOpts.size()),
            linkOpts.data(),
            diagLink.writeRef());
        AppendBlobDiagnostics(log, diagLink.get());
        if (SLANG_FAILED(linkHr) || !linked) {
            out.m_Log = log.empty() ? "linkWithOptions failed" : log;
            return false;
        }

        std::string failMsg;
        if (!GetLinkedSpirv(linked.get(), log, out.m_Spirv, failMsg)) {
            out.m_Log = std::move(failMsg);
            return false;
        }

        out.m_Source = std::move(fileContents);
        out.m_Log = std::move(log);
        out.m_Success = true;
        return true;
    }

    // -----------------------------------------------------------------------------

    std::filesystem::path RHI_ShaderCompiler::GetCacheDirectory() {
        auto dir = std::filesystem::current_path() / "Cache" / "Shaders";
        std::filesystem::create_directories(dir);
        return dir;
    }

    std::string RHI_ShaderCompiler::ComputeHash(const RHI_ShaderCompileInput& input) {
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

    bool RHI_ShaderCompiler::NeedsRecompile(const RHI_ShaderCompileInput& input, const std::string& hash) {
        (void)input;
        const auto path = GetCacheDirectory() / (hash + ".spv");
        return !std::filesystem::exists(path);
    }

    bool RHI_ShaderCompiler::LoadCache(const std::string& hash, RHI_ShaderCompileResult& out) {
        const auto path = GetCacheDirectory() / (hash + ".spv");
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

        out.m_Spirv.resize(size / sizeof(uint32_t));
        file.read(reinterpret_cast<char*>(out.m_Spirv.data()), static_cast<std::streamsize>(size));

        out.m_Success = true;
        return true;
    }

    void RHI_ShaderCompiler::SaveCache(const std::string& hash, const RHI_ShaderCompileResult& result) {
        const auto path = GetCacheDirectory() / (hash + ".spv");
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        file.write(
            reinterpret_cast<const char*>(result.m_Spirv.data()),
            static_cast<std::streamsize>(result.m_Spirv.size() * sizeof(uint32_t)));
    }

    RHI_ShaderCompileResult RHI_ShaderCompiler::Compile(const RHI_ShaderCompileInput& input) {
        RHI_ShaderCompileInput in = input;
        if (in.m_Stage == RHI_ShaderStage::Unknown) {
            in.m_Stage = ShaderStageFromFileExtension(in.m_File);
        }

        RHI_ShaderCompileResult failure{};
        failure.m_TargetApi = in.m_TargetApi;

        if (in.m_Stage == RHI_ShaderStage::Unknown) {
            failure.m_Log =
                "Cannot infer shader stage from file name (expected e.g. *.vert.slang): " + in.m_File.string();
            return failure;
        }

        std::lock_guard<std::mutex> lock(g_Mutex);

        const std::string hash = ComputeHash(in);

        if (!in.m_SkipCache) {
            if (const auto it = g_MemoryCache.find(hash); it != g_MemoryCache.end()) {
                return it->second;
            }

            if (!NeedsRecompile(in, hash)) {
                RHI_ShaderCompileResult disk{};
                if (LoadCache(hash, disk)) {
                    disk.m_Stage = in.m_Stage;
                    disk.m_TargetApi = in.m_TargetApi;
                    std::string readErr;
                    ReadTextFile(in.m_File, disk.m_Source, readErr);
                    std::error_code ec;
                    disk.m_LastWriteTime = std::filesystem::last_write_time(in.m_File, ec);
                    disk.m_Success = true;
                    g_MemoryCache[hash] = disk;
                    return disk;
                }
            }
        }

        std::string sessionErr;
        if (!EnsureGlobalSessionUnlocked(sessionErr)) {
            failure.m_Log = std::move(sessionErr);
            return failure;
        }

        std::string source;
        std::string readErr;
        if (!ReadTextFile(in.m_File, source, readErr)) {
            failure.m_Log = std::move(readErr);
            return failure;
        }

        RHI_ShaderCompileResult out{};
        out.m_Stage = in.m_Stage;
        out.m_TargetApi = in.m_TargetApi;

        if (!CompileSlangFileToSpirv(g_GlobalSession.get(), in, std::move(source), out)) {
            return out;
        }

        std::error_code ec;
        out.m_LastWriteTime = std::filesystem::last_write_time(in.m_File, ec);

        if (!in.m_SkipCache) {
            SaveCache(hash, out);
            g_MemoryCache[hash] = out;
        }

        return out;
    }

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
            case RHI_ShaderStage::Vertex:           return "Vertex";
            case RHI_ShaderStage::Fragment:         return "Fragment";
            case RHI_ShaderStage::Geometry:         return "Geometry";
            case RHI_ShaderStage::TessControl:      return "Tessellation Control";
            case RHI_ShaderStage::TessEvaluation:   return "Tessellation Evaluation";
            case RHI_ShaderStage::Compute:          return "Compute";
            case RHI_ShaderStage::RayGen:           return "Ray Generation";
            case RHI_ShaderStage::RayMiss:          return "Ray Miss";
            case RHI_ShaderStage::RayClosestHit:    return "Ray Closest Hit";
            case RHI_ShaderStage::RayAnyHit:        return "Ray Any Hit";
            case RHI_ShaderStage::RayIntersection:  return "Ray Intersection";
            case RHI_ShaderStage::RayCallable:      return "Ray Callable";
            default:                                return "Unknown";
        }
    }

    bool EnsureSlangInitialized() {
        std::lock_guard<std::mutex> lock(g_Mutex);
        std::string err;
        return EnsureGlobalSessionUnlocked(err);
    }

    void ShutdownSlang() {
        std::lock_guard<std::mutex> lock(g_Mutex);
        g_MemoryCache.clear();
        g_GlobalSession.setNull();
        slang::shutdown();
    }

} // namespace Nova::Core::Renderer::RHI
