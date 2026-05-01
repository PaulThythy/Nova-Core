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
#include <optional>

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

    // -----------------------------------------------------------------------------
    // Reflection cache (simple binary format)
    // -----------------------------------------------------------------------------

    static constexpr uint32_t kReflectionCacheMagic = 0x4E565245; // 'NVRE'
    static constexpr uint32_t kReflectionCacheVersion = 1;

    static void WriteU32(std::ostream& os, uint32_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    static void WriteU64(std::ostream& os, uint64_t v) { os.write(reinterpret_cast<const char*>(&v), sizeof(v)); }
    static void WriteBool(std::ostream& os, bool v) { uint8_t b = v ? 1u : 0u; os.write(reinterpret_cast<const char*>(&b), sizeof(b)); }
    static void WriteString(std::ostream& os, const std::string& s) {
        WriteU32(os, static_cast<uint32_t>(s.size()));
        if (!s.empty()) os.write(s.data(), static_cast<std::streamsize>(s.size()));
    }

    static bool ReadU32(std::istream& is, uint32_t& out) { return (bool)is.read(reinterpret_cast<char*>(&out), sizeof(out)); }
    static bool ReadU64(std::istream& is, uint64_t& out) { return (bool)is.read(reinterpret_cast<char*>(&out), sizeof(out)); }
    static bool ReadBool(std::istream& is, bool& out) { uint8_t b = 0; if (!is.read(reinterpret_cast<char*>(&b), sizeof(b))) return false; out = (b != 0); return true; }
    static bool ReadString(std::istream& is, std::string& out) {
        uint32_t n = 0;
        if (!ReadU32(is, n)) return false;
        out.resize(n);
        if (n == 0) return true;
        return (bool)is.read(out.data(), static_cast<std::streamsize>(n));
    }

    static std::filesystem::path GetReflectionCachePath(const std::filesystem::path& dir, const std::string& hash) {
        return dir / (hash + ".refl");
    }

    static bool LoadReflectionCache(const std::filesystem::path& dir, const std::string& hash, RHI_ProgramReflection& out) {
        const auto path = GetReflectionCachePath(dir, hash);
        if (!std::filesystem::exists(path)) return false;

        std::ifstream is(path, std::ios::binary);
        if (!is.is_open()) return false;

        uint32_t magic = 0, version = 0;
        if (!ReadU32(is, magic) || magic != kReflectionCacheMagic) return false;
        if (!ReadU32(is, version) || version != kReflectionCacheVersion) return false;

        // Push constants
        bool hasPC = false;
        if (!ReadBool(is, hasPC)) return false;
        if (hasPC) {
            uint64_t size = 0;
            uint32_t stages = 0;
            if (!ReadU64(is, size)) return false;
            if (!ReadU32(is, stages)) return false;
            out.m_PushConstants = RHI_PushConstantInfo{ static_cast<size_t>(size), static_cast<RHI_ShaderStageMask>(stages) };
        }

        // Sets + bindings
        uint32_t setCount = 0;
        if (!ReadU32(is, setCount)) return false;
        out.m_Sets.clear();
        out.m_Sets.reserve(setCount);
        for (uint32_t si = 0; si < setCount; ++si) {
            RHI_DescriptorSetLayoutInfo dsl{};
            if (!ReadU32(is, dsl.m_Set)) return false;
            uint32_t bindingCount = 0;
            if (!ReadU32(is, bindingCount)) return false;
            dsl.m_Bindings.reserve(bindingCount);
            for (uint32_t bi = 0; bi < bindingCount; ++bi) {
                RHI_BindingInfo b{};
                b.m_Key.m_Set = dsl.m_Set;
                if (!ReadU32(is, b.m_Key.m_Binding)) return false;
                uint32_t kind = 0;
                if (!ReadU32(is, kind)) return false;
                b.m_Kind = static_cast<RHI_ResourceKind>(static_cast<uint8_t>(kind));
                if (!ReadU32(is, b.m_ArrayCount)) return false;
                uint64_t byteSize = 0;
                if (!ReadU64(is, byteSize)) return false;
                b.m_ByteSizeIfKnown = static_cast<size_t>(byteSize);
                if (!ReadString(is, b.m_FullName)) return false;
                uint32_t stages = 0;
                if (!ReadU32(is, stages)) return false;
                b.m_Stages = static_cast<RHI_ShaderStageMask>(stages);
                if (!ReadBool(is, b.m_IsDynamicUniformBuffer)) return false;
                dsl.m_Bindings.push_back(std::move(b));
            }
            out.m_Sets.push_back(std::move(dsl));
        }

        // nameToBinding
        uint32_t nameCount = 0;
        if (!ReadU32(is, nameCount)) return false;
        out.m_NameToBinding.clear();
        out.m_NameToBinding.reserve(nameCount);
        for (uint32_t i = 0; i < nameCount; ++i) {
            std::string name;
            RHI_BindingKey key{};
            if (!ReadString(is, name)) return false;
            if (!ReadU32(is, key.m_Set)) return false;
            if (!ReadU32(is, key.m_Binding)) return false;
            out.m_NameToBinding.emplace(std::move(name), key);
        }

        return true;
    }

    static void SaveReflectionCache(const std::filesystem::path& dir, const std::string& hash, const RHI_ProgramReflection& refl) {
        const auto path = GetReflectionCachePath(dir, hash);
        std::ofstream os(path, std::ios::binary);
        if (!os.is_open()) return;

        WriteU32(os, kReflectionCacheMagic);
        WriteU32(os, kReflectionCacheVersion);

        // Push constants
        WriteBool(os, refl.m_PushConstants.has_value());
        if (refl.m_PushConstants) {
            WriteU64(os, static_cast<uint64_t>(refl.m_PushConstants->m_SizeBytes));
            WriteU32(os, static_cast<uint32_t>(refl.m_PushConstants->m_Stages));
        }

        // Sets + bindings
        WriteU32(os, static_cast<uint32_t>(refl.m_Sets.size()));
        for (const auto& dsl : refl.m_Sets) {
            WriteU32(os, dsl.m_Set);
            WriteU32(os, static_cast<uint32_t>(dsl.m_Bindings.size()));
            for (const auto& b : dsl.m_Bindings) {
                WriteU32(os, b.m_Key.m_Binding);
                WriteU32(os, static_cast<uint32_t>(b.m_Kind));
                WriteU32(os, b.m_ArrayCount);
                WriteU64(os, static_cast<uint64_t>(b.m_ByteSizeIfKnown));
                WriteString(os, b.m_FullName);
                WriteU32(os, static_cast<uint32_t>(b.m_Stages));
                WriteBool(os, b.m_IsDynamicUniformBuffer);
            }
        }

        // nameToBinding
        WriteU32(os, static_cast<uint32_t>(refl.m_NameToBinding.size()));
        for (const auto& [name, key] : refl.m_NameToBinding) {
            WriteString(os, name);
            WriteU32(os, key.m_Set);
            WriteU32(os, key.m_Binding);
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

    // -----------------------------------------------------------------------------
    // Slang reflection extraction
    // -----------------------------------------------------------------------------

    static RHI_ResourceKind SlangTypeKindToResourceKind(slang::TypeReflection::Kind kind) {
        switch (kind) {
            case slang::TypeReflection::Kind::ConstantBuffer: return RHI_ResourceKind::ConstantBuffer;
            case slang::TypeReflection::Kind::Resource:       return RHI_ResourceKind::Texture; // refined below when possible
            case slang::TypeReflection::Kind::SamplerState:   return RHI_ResourceKind::Sampler;
            default:                                          return RHI_ResourceKind::Unknown;
        }
    }

    static void EnsureSet(RHI_ProgramReflection& out, uint32_t setIndex) {
        if (out.FindSet(setIndex)) return;
        out.m_Sets.push_back(RHI_DescriptorSetLayoutInfo{ setIndex, {} });
    }

    static void AddOrMergeBinding(
        RHI_ProgramReflection& out,
        const RHI_BindingInfo& b)
    {
        EnsureSet(out, b.m_Key.m_Set);
        auto* dsl = const_cast<RHI_DescriptorSetLayoutInfo*>(out.FindSet(b.m_Key.m_Set));
        auto it = std::find_if(dsl->m_Bindings.begin(), dsl->m_Bindings.end(),
            [&](const auto& x) { return x.m_Key.m_Binding == b.m_Key.m_Binding; });
        if (it == dsl->m_Bindings.end()) {
            dsl->m_Bindings.push_back(b);
        } else {
            it->m_Stages |= b.m_Stages;
            if (it->m_Kind == RHI_ResourceKind::Unknown) it->m_Kind = b.m_Kind;
            if (it->m_FullName.empty()) it->m_FullName = b.m_FullName;
            if (it->m_ByteSizeIfKnown == 0) it->m_ByteSizeIfKnown = b.m_ByteSizeIfKnown;
            if (it->m_ArrayCount == 1 && b.m_ArrayCount != 1) it->m_ArrayCount = b.m_ArrayCount;
            it->m_IsDynamicUniformBuffer = it->m_IsDynamicUniformBuffer || b.m_IsDynamicUniformBuffer;
        }
    }

    static void ExtractBindingsFromTypeLayout(
        slang::TypeLayoutReflection* typeLayout,
        const std::string& prefix,
        RHI_ShaderStageMask stageMask,
        RHI_ProgramReflection& out,
        const std::optional<uint32_t>& setOverride)
    {
        if (!typeLayout) return;

        auto tryGetBindingForField = [&](slang::VariableLayoutReflection* field, uint32_t& outSet, uint32_t& outBinding) -> bool {
            if (!field) return false;

            // Try categories that can carry binding/space data.
            // Note: Slang uses different categories depending on how a resource is lowered.
            const slang::ParameterCategory cats[] = {
                slang::ParameterCategory::DescriptorTableSlot,
                slang::ParameterCategory::ConstantBuffer,
                slang::ParameterCategory::ShaderResource,
                slang::ParameterCategory::UnorderedAccess,
                slang::ParameterCategory::SamplerState,
                slang::ParameterCategory::Uniform,
            };

            for (auto cat : cats) {
                const size_t rawBinding = field->getOffset(cat);
                const size_t rawSet = field->getBindingSpace(cat);

                const bool bindingKnown =
                    rawBinding != SLANG_UNKNOWN_SIZE &&
                    rawBinding != SLANG_UNBOUNDED_SIZE;
                const bool setKnown =
                    rawSet != SLANG_UNKNOWN_SIZE &&
                    rawSet != SLANG_UNBOUNDED_SIZE;

                if (!bindingKnown) continue;

                outBinding = static_cast<uint32_t>(rawBinding);
                if (setKnown) outSet = static_cast<uint32_t>(rawSet);
                else if (setOverride) outSet = *setOverride;
                else outSet = 0;

                return true;
            }

            // Fallback: global binding index/space (may map to D3D style).
            const unsigned rawBinding = field->getBindingIndex();
            const unsigned rawSet = field->getBindingSpace();
            if (rawBinding != static_cast<unsigned>(SLANG_UNKNOWN_SIZE)) {
                outBinding = static_cast<uint32_t>(rawBinding);
                if (rawSet != static_cast<unsigned>(SLANG_UNKNOWN_SIZE)) outSet = static_cast<uint32_t>(rawSet);
                else if (setOverride) outSet = *setOverride;
                else outSet = 0;
                return true;
            }

            return false;
        };

        const int fieldCount = typeLayout->getFieldCount();
        for (int i = 0; i < fieldCount; ++i) {
            slang::VariableLayoutReflection* field = typeLayout->getFieldByIndex(i);
            if (!field) continue;

            const char* nameCStr = field->getName();
            std::string name = nameCStr ? nameCStr : "";
            const std::string fullName = prefix.empty() ? name : (prefix + "." + name);

            auto* fieldTypeLayout = field->getTypeLayout();
            auto* fieldType = field->getType();

            // Slang reflection uses binding index + binding space for register(set)/binding.
            uint32_t binding = 0;
            uint32_t set = 0;
            bool bindingKnown = tryGetBindingForField(field, set, binding);

            // Convention fallback: global engine/user ParameterBlocks.
            // This keeps the system stable even when Slang reflection doesn't expose the space cleanly.
            if (!bindingKnown && prefix.empty() && fieldType &&
                fieldType->getKind() == slang::TypeReflection::Kind::ParameterBlock)
            {
                binding = 0;
                if (name == "nova") { set = 0; bindingKnown = true; }
                else if (name == "user") { set = 1; bindingKnown = true; }
            }

            // When extracting fields *inside* a ParameterBlock element type, always force the set.
            // Slang may report binding space=0 for nested fields, but the descriptor set comes from the block.
            if (setOverride) {
                set = *setOverride;
            }

            // Recurse into nested structs *without* a binding, to reach resources.
            // If a field has a binding, it represents a resource/parameter block.
            // If we don't see a binding on this field, recurse into nested fields.
            bool hasAnyBinding = bindingKnown;

            // Fallback for fields inside a ParameterBlock element type: bindings are assigned in field order.
            // If reflection doesn't report a binding for the field, use the field index with the known set override.
            if (!hasAnyBinding && setOverride && fieldType) {
                const auto k = fieldType->getKind();
                if (k == slang::TypeReflection::Kind::ConstantBuffer ||
                    k == slang::TypeReflection::Kind::Resource ||
                    k == slang::TypeReflection::Kind::SamplerState)
                {
                    set = *setOverride;
                    binding = static_cast<uint32_t>(i);
                    hasAnyBinding = true;
                }
            }

            // ParameterBlock is a container: binding/set applies to the block, while the *element* fields
            // provide binding indices. We must descend into the element layout and force its set.
            if (fieldType && fieldType->getKind() == slang::TypeReflection::Kind::ParameterBlock) {
                if (fieldTypeLayout) {
                    if (auto* elem = fieldTypeLayout->getElementTypeLayout()) {
                        ExtractBindingsFromTypeLayout(elem, fullName, stageMask, out, set);
                    }
                }
                continue;
            }

            if (!hasAnyBinding && fieldTypeLayout && fieldTypeLayout->getFieldCount() > 0) {
                ExtractBindingsFromTypeLayout(fieldTypeLayout, fullName, stageMask, out, setOverride);
                continue;
            }

            // If we got here without a binding, skip (we don't know where to bind it).
            if (!hasAnyBinding) {
                continue;
            }

            RHI_BindingInfo bi{};
            bi.m_Key = RHI_BindingKey{ set, binding };
            bi.m_FullName = fullName;
            bi.m_Stages = stageMask;

            // Arrays: Slang reflects array element count in type layout when known.
            if (fieldTypeLayout) {
                const int elemCount = fieldTypeLayout->getElementCount();
                if (elemCount > 0) bi.m_ArrayCount = static_cast<uint32_t>(elemCount);
                else if (elemCount == 0) bi.m_ArrayCount = 0; // unknown/runtime sized
            }

            if (fieldType) {
                bi.m_Kind = SlangTypeKindToResourceKind(fieldType->getKind());

                if (fieldType->getKind() == slang::TypeReflection::Kind::Resource) {
                    const SlangResourceShape shape = fieldType->getResourceShape();
                    const SlangResourceAccess access = fieldType->getResourceAccess();
                    const SlangResourceShape baseShape = (SlangResourceShape)(shape & SLANG_RESOURCE_BASE_SHAPE_MASK);

                    const bool combined = (shape & SLANG_TEXTURE_COMBINED_FLAG) != 0;
                    if (combined) {
                        bi.m_Kind = RHI_ResourceKind::CombinedTextureSampler;
                    } else if (baseShape == SLANG_STRUCTURED_BUFFER || baseShape == SLANG_BYTE_ADDRESS_BUFFER) {
                        bi.m_Kind = (access == SLANG_RESOURCE_ACCESS_READ_WRITE) ? RHI_ResourceKind::RWBuffer : RHI_ResourceKind::StorageBuffer;
                    } else if (baseShape == SLANG_ACCELERATION_STRUCTURE) {
                        bi.m_Kind = RHI_ResourceKind::AccelStruct;
                    } else if (baseShape == SLANG_TEXTURE_1D || baseShape == SLANG_TEXTURE_2D || baseShape == SLANG_TEXTURE_3D ||
                               baseShape == SLANG_TEXTURE_CUBE || baseShape == SLANG_TEXTURE_SUBPASS || baseShape == SLANG_TEXTURE_BUFFER) {
                        bi.m_Kind = (access == SLANG_RESOURCE_ACCESS_READ_WRITE) ? RHI_ResourceKind::RWTexture : RHI_ResourceKind::Texture;
                    }
                }
            }

            // If type-based classification failed, fall back to the parameter category from the layout.
            if (bi.m_Kind == RHI_ResourceKind::Unknown && fieldTypeLayout) {
                switch (fieldTypeLayout->getParameterCategory()) {
                    case slang::ParameterCategory::ConstantBuffer:
                        bi.m_Kind = RHI_ResourceKind::ConstantBuffer;
                        break;
                    case slang::ParameterCategory::ShaderResource:
                        bi.m_Kind = RHI_ResourceKind::Texture;
                        break;
                    case slang::ParameterCategory::SamplerState:
                        bi.m_Kind = RHI_ResourceKind::Sampler;
                        break;
                    case slang::ParameterCategory::UnorderedAccess:
                        bi.m_Kind = RHI_ResourceKind::RWTexture;
                        break;
                    default:
                        break;
                }
            }

            // Byte size for constant buffers (when reflectable).
            if (fieldTypeLayout && (bi.m_Kind == RHI_ResourceKind::ConstantBuffer)) {
                const size_t size = static_cast<size_t>(fieldTypeLayout->getSize());
                if (size != SLANG_UNKNOWN_SIZE && size != SLANG_UNBOUNDED_SIZE)
                    bi.m_ByteSizeIfKnown = size;
            }

            AddOrMergeBinding(out, bi);
            out.m_NameToBinding.emplace(fullName, bi.m_Key);
        }
    }

    static void ExtractReflectionFromLinked(
        slang::IComponentType* linked,
        RHI_ShaderStage stage,
        RHI_ProgramReflection& out)
    {
        out = {};
        if (!linked) return;

        slang::ProgramLayout* programLayout = linked->getLayout();
        if (!programLayout) return;

        slang::VariableLayoutReflection* globals = programLayout->getGlobalParamsVarLayout();
        if (!globals) return;

        slang::TypeLayoutReflection* globalsType = globals->getTypeLayout();
        if (!globalsType) return;

        const RHI_ShaderStageMask stageMask = ToStageMask(stage);
        ExtractBindingsFromTypeLayout(globalsType, "", stageMask, out, std::nullopt);
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

        // Reflection: best-effort (failure should not fail compilation).
        ExtractReflectionFromLinked(linked.get(), input.m_Stage, out.m_Reflection);
        if (out.m_Reflection.m_Sets.empty()) {
            // Diagnostics: dump a small excerpt of Slang reflection JSON to help map categories/bindings.
            Slang::ComPtr<ISlangBlob> jsonBlob;
            if (SLANG_SUCCEEDED(spReflection_ToJson((SlangReflection*)linked->getLayout(), nullptr, jsonBlob.writeRef())) && jsonBlob) {
                const char* ptr = static_cast<const char*>(jsonBlob->getBufferPointer());
                const size_t size = jsonBlob->getBufferSize();
                if (ptr && size > 0) {
                    const size_t kMax = 4096;
                    out.m_Log.append("\n[SlangReflectionExcerpt]\n");
                    out.m_Log.append(ptr, ptr + std::min(size, kMax));
                    if (size > kMax) out.m_Log.append("\n...[truncated]...\n");
                }
            }
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
        const auto dir = GetCacheDirectory();
        const auto path = dir / (hash + ".spv");
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

        // Reflection is optional, but try to load it.
        (void)LoadReflectionCache(dir, hash, out.m_Reflection);

        out.m_Success = true;
        return true;
    }

    void RHI_ShaderCompiler::SaveCache(const std::string& hash, const RHI_ShaderCompileResult& result) {
        const auto dir = GetCacheDirectory();
        const auto path = dir / (hash + ".spv");
        std::ofstream file(path, std::ios::binary);
        if (!file.is_open()) {
            return;
        }
        file.write(
            reinterpret_cast<const char*>(result.m_Spirv.data()),
            static_cast<std::streamsize>(result.m_Spirv.size() * sizeof(uint32_t)));

        SaveReflectionCache(dir, hash, result.m_Reflection);
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