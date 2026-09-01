#include "Renderer/RHI/RHI_ShaderReflection.h"

#include <algorithm>
#include <optional>

#include <slang-com-helper.h>
#include <slang-com-ptr.h>
#include <slang.h>

#include "Renderer/RHI/RHI_Renderer.h"
#include "Renderer/RHI/RHI_Shaders.h"

namespace Nova::Core::Renderer::RHI {

    const RHI_DescriptorSetLayoutInfo* RHI_ProgramReflection::FindSet(uint32_t setIndex) const {
        for (const auto& s : m_Sets) {
            if (s.m_Set == setIndex) return &s;
        }
        return nullptr;
    }

    const RHI_BindingInfo* RHI_ProgramReflection::FindBinding(uint32_t setIndex, uint32_t binding) const {
        if (const auto* s = FindSet(setIndex)) {
            for (const auto& b : s->m_Bindings) {
                if (b.m_Key.m_Binding == binding) return &b;
            }
        }
        return nullptr;
    }

    const RHI_BindingKey* RHI_ProgramReflection::FindBindingKeyByName(const std::string& name) const {
        auto it = m_NameToBinding.find(name);
        if (it == m_NameToBinding.end()) return nullptr;
        return &it->second;
    }

    const RHI_BindingInfo* RHI_ProgramReflection::FindBindingByName(const std::string& name) const {
        const RHI_BindingKey* key = FindBindingKeyByName(name);
        if (!key) return nullptr;
        return FindBinding(key->m_Set, key->m_Binding);
    }

    static void SortAndDedupe(RHI_ProgramReflection& refl) {
        std::sort(refl.m_Sets.begin(), refl.m_Sets.end(),
            [](const auto& a, const auto& b) { return a.m_Set < b.m_Set; });

        for (auto& set : refl.m_Sets) {
            std::sort(set.m_Bindings.begin(), set.m_Bindings.end(),
                [](const auto& a, const auto& b) { return a.m_Key.m_Binding < b.m_Key.m_Binding; });

            set.m_Bindings.erase(
                std::unique(set.m_Bindings.begin(), set.m_Bindings.end(),
                    [](const auto& a, const auto& b) { return a.m_Key.m_Binding == b.m_Key.m_Binding; }),
                set.m_Bindings.end());
        }
    }

    RHI_ProgramReflection MergeProgramReflections(const std::vector<RHI_ProgramReflection>& perStage) {
        RHI_ProgramReflection out{};

        for (const auto& r : perStage) {
            if (!r.m_PushConstants) continue;
            if (!out.m_PushConstants) {
                out.m_PushConstants = *r.m_PushConstants;
            } else {
                out.m_PushConstants->m_SizeBytes = std::max(out.m_PushConstants->m_SizeBytes, r.m_PushConstants->m_SizeBytes);
                out.m_PushConstants->m_Stages |= r.m_PushConstants->m_Stages;
            }
        }

        for (const auto& r : perStage) {
            for (const auto& set : r.m_Sets) {
                auto itSet = std::find_if(out.m_Sets.begin(), out.m_Sets.end(),
                    [&](const auto& s) { return s.m_Set == set.m_Set; });
                if (itSet == out.m_Sets.end()) {
                    out.m_Sets.push_back(set);
                } else {
                    for (const auto& b : set.m_Bindings) {
                        auto itB = std::find_if(itSet->m_Bindings.begin(), itSet->m_Bindings.end(),
                            [&](const auto& existing) { return existing.m_Key.m_Binding == b.m_Key.m_Binding; });
                        if (itB == itSet->m_Bindings.end()) {
                            itSet->m_Bindings.push_back(b);
                        } else {
                            itB->m_Stages |= b.m_Stages;
                            if (itB->m_Kind == RHI_ResourceKind::Unknown) itB->m_Kind = b.m_Kind;
                            if (itB->m_ArrayCount == 1 && b.m_ArrayCount != 1) itB->m_ArrayCount = b.m_ArrayCount;
                            if (itB->m_ByteSizeIfKnown == 0 && b.m_ByteSizeIfKnown != 0) itB->m_ByteSizeIfKnown = b.m_ByteSizeIfKnown;
                            if (itB->m_FullName.empty() && !b.m_FullName.empty()) itB->m_FullName = b.m_FullName;
                            itB->m_IsDynamicUniformBuffer = itB->m_IsDynamicUniformBuffer || b.m_IsDynamicUniformBuffer;
                        }
                    }
                }
            }

            for (const auto& [name, key] : r.m_NameToBinding) {
                out.m_NameToBinding.emplace(name, key);
            }
        }

        SortAndDedupe(out);
        return out;
    }

    RHI_ResourceKind SlangTypeKindToResourceKind(slang::TypeReflection::Kind kind) {
        switch (kind) {
            case slang::TypeReflection::Kind::ConstantBuffer: return RHI_ResourceKind::ConstantBuffer;
            case slang::TypeReflection::Kind::Resource:       return RHI_ResourceKind::Texture;
            case slang::TypeReflection::Kind::SamplerState:   return RHI_ResourceKind::Sampler;
            default:                                          return RHI_ResourceKind::Unknown;
        }
    }

    void EnsureSet(RHI_ProgramReflection& out, uint32_t setIndex) {
        if (out.FindSet(setIndex)) return;
        out.m_Sets.push_back(RHI_DescriptorSetLayoutInfo{ setIndex, {} });
    }

    void AddOrMergeBinding(RHI_ProgramReflection& out, const RHI_BindingInfo& b) {
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

    bool TryGetParameterBlockSpace(slang::VariableLayoutReflection* field, uint32_t& outSpace) {
        if (!field) return false;

        const slang::ParameterCategory spaceCats[] = {
            slang::ParameterCategory::SubElementRegisterSpace,
            slang::ParameterCategory::RegisterSpace,
        };
        for (auto cat : spaceCats) {
            const size_t off = field->getOffset(cat);
            if (off != SLANG_UNKNOWN_SIZE && off != SLANG_UNBOUNDED_SIZE) {
                outSpace = static_cast<uint32_t>(off);
                return true;
            }
            const size_t space = field->getBindingSpace(cat);
            if (space != SLANG_UNKNOWN_SIZE && space != SLANG_UNBOUNDED_SIZE && space != 0) {
                outSpace = static_cast<uint32_t>(space);
                return true;
            }
        }
        return false;
    }

    void ExtractBindingsFromTypeLayout(
        slang::TypeLayoutReflection* typeLayout,
        const std::string& prefix,
        RHI_ShaderStageMask stageMask,
        RHI_ProgramReflection& out,
        const std::optional<uint32_t>& setOverride,
        uint32_t& nextAutoSpace)
    {
        if (!typeLayout) return;

        auto tryGetBindingForField = [&](slang::VariableLayoutReflection* field, uint32_t& outSet, uint32_t& outBinding) -> bool {
            if (!field) return false;

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

            if (fieldTypeLayout &&
                fieldTypeLayout->getParameterCategory() == slang::ParameterCategory::PushConstantBuffer)
            {
                size_t size = fieldTypeLayout->getSize(slang::ParameterCategory::Uniform);
                if (size == SLANG_UNKNOWN_SIZE || size == SLANG_UNBOUNDED_SIZE)
                    size = fieldTypeLayout->getSize();
                if ((size == SLANG_UNKNOWN_SIZE || size == SLANG_UNBOUNDED_SIZE) && fieldTypeLayout->getElementTypeLayout())
                    size = fieldTypeLayout->getElementTypeLayout()->getSize(slang::ParameterCategory::Uniform);
                if (size != SLANG_UNKNOWN_SIZE && size != SLANG_UNBOUNDED_SIZE && size > 0) {
                    if (!out.m_PushConstants) {
                        out.m_PushConstants = RHI_PushConstantInfo{ size, stageMask };
                    } else {
                        out.m_PushConstants->m_SizeBytes = std::max(out.m_PushConstants->m_SizeBytes, size);
                        out.m_PushConstants->m_Stages |= stageMask;
                    }
                }
                continue;
            }

            uint32_t binding = 0;
            uint32_t set = 0;
            bool bindingKnown = tryGetBindingForField(field, set, binding);

            if (setOverride) {
                set = *setOverride;
            }

            bool hasAnyBinding = bindingKnown;

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

            if (fieldType && fieldType->getKind() == slang::TypeReflection::Kind::ParameterBlock) {
                uint32_t blockSpace = 0;
                if (!TryGetParameterBlockSpace(field, blockSpace)) {
                    blockSpace = nextAutoSpace;
                }
                if (blockSpace + 1 > nextAutoSpace) nextAutoSpace = blockSpace + 1;

                if (fieldTypeLayout) {
                    if (auto* elem = fieldTypeLayout->getElementTypeLayout()) {
                        ExtractBindingsFromTypeLayout(elem, fullName, stageMask, out, blockSpace, nextAutoSpace);
                    }
                }
                continue;
            }

            if (!hasAnyBinding && fieldTypeLayout && fieldTypeLayout->getFieldCount() > 0) {
                ExtractBindingsFromTypeLayout(fieldTypeLayout, fullName, stageMask, out, setOverride, nextAutoSpace);
                continue;
            }

            if (!hasAnyBinding) {
                continue;
            }

            RHI_BindingInfo bi{};
            bi.m_Key = RHI_BindingKey{ set, binding };
            bi.m_FullName = fullName;
            bi.m_Stages = stageMask;

            if (fieldTypeLayout) {
                const int elemCount = fieldTypeLayout->getElementCount();
                if (elemCount > 0) bi.m_ArrayCount = static_cast<uint32_t>(elemCount);
                else if (elemCount == 0) bi.m_ArrayCount = 0;
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
                        bi.m_Kind = (access == SLANG_RESOURCE_ACCESS_READ_WRITE) ? RHI_ResourceKind::RWStructuredBuffer : RHI_ResourceKind::StructuredBuffer;
                    } else if (baseShape == SLANG_ACCELERATION_STRUCTURE) {
                        bi.m_Kind = RHI_ResourceKind::AccelStruct;
                    } else if (baseShape == SLANG_TEXTURE_1D || baseShape == SLANG_TEXTURE_2D || baseShape == SLANG_TEXTURE_3D ||
                                baseShape == SLANG_TEXTURE_CUBE || baseShape == SLANG_TEXTURE_SUBPASS || baseShape == SLANG_TEXTURE_BUFFER) {
                        bi.m_Kind = (access == SLANG_RESOURCE_ACCESS_READ_WRITE) ? RHI_ResourceKind::RWTexture : RHI_ResourceKind::Texture;
                    }
                }
            }

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

            if (fieldTypeLayout && (bi.m_Kind == RHI_ResourceKind::ConstantBuffer)) {
                const size_t size = static_cast<size_t>(fieldTypeLayout->getSize());
                if (size != SLANG_UNKNOWN_SIZE && size != SLANG_UNBOUNDED_SIZE)
                    bi.m_ByteSizeIfKnown = size;
            }

            AddOrMergeBinding(out, bi);
            out.m_NameToBinding.emplace(fullName, bi.m_Key);
        }
    }

    void ExtractProgramReflection(slang::IComponentType* linked, RHI_ShaderStage stage, RHI_ProgramReflection& out) {
        out = {};
        if (!linked) return;

        slang::ProgramLayout* programLayout = linked->getLayout();
        if (!programLayout) return;

        slang::VariableLayoutReflection* globals = programLayout->getGlobalParamsVarLayout();
        if (!globals) return;

        slang::TypeLayoutReflection* globalsType = globals->getTypeLayout();
        if (!globalsType) return;

        const RHI_ShaderStageMask stageMask = ToStageMask(stage);
        uint32_t nextAutoSpace = 0;
        ExtractBindingsFromTypeLayout(globalsType, "", stageMask, out, std::nullopt, nextAutoSpace);
    }

    void AppendSlangReflectionJsonExcerpt(std::string& log, slang::IComponentType* linked, size_t maxBytes) {
        if (!linked) return;

        Slang::ComPtr<ISlangBlob> jsonBlob;
        if (SLANG_FAILED(spReflection_ToJson((SlangReflection*)linked->getLayout(), nullptr, jsonBlob.writeRef())) || !jsonBlob) {
            return;
        }

        const char* ptr = static_cast<const char*>(jsonBlob->getBufferPointer());
        const size_t size = jsonBlob->getBufferSize();
        if (!ptr || size == 0) return;

        log.append("\n[SlangReflectionExcerpt]\n");
        log.append(ptr, ptr + std::min(size, maxBytes));
        if (size > maxBytes) log.append("\n...[truncated]...\n");
    }

    const RHI_BindingInfo* RHI_ShaderResourceSet::FindBindingInfo(const std::string& name) const {
        if (!m_Reflection) return nullptr;
        auto it = m_Reflection->m_NameToBinding.find(name);
        if (it == m_Reflection->m_NameToBinding.end()) return nullptr;
        return m_Reflection->FindBinding(it->second.m_Set, it->second.m_Binding);
    }

    bool RHI_ShaderResourceSet::SetBuffer(const std::string& name, uint64_t handle, uint64_t offset, uint64_t range) {
        const auto* info = FindBindingInfo(name);
        if (!info) return false;
        if (info->m_Kind != RHI_ResourceKind::ConstantBuffer && info->m_Kind != RHI_ResourceKind::StructuredBuffer &&
            info->m_Kind != RHI_ResourceKind::RWStructuredBuffer)
            return false;
        m_Bindings[name] = RHI_BufferBinding{ handle, offset, range };
        return true;
    }

    bool RHI_ShaderResourceSet::SetBuffer(const std::string& name, RHI_GpuBufferHandle handle, const IRenderer& renderer, uint32_t elementIndex) {
        if (!handle.IsValid()) return false;
        const RHI_BufferBinding binding = renderer.ResolveGpuBufferBinding(handle, elementIndex);
        return SetBuffer(name, binding.m_Handle, binding.m_Offset, binding.m_Range);
    }

    bool RHI_ShaderResourceSet::SetTexture(const std::string& name, uint64_t textureHandle, uint32_t imageLayout) {
        const auto* info = FindBindingInfo(name);
        if (!info) return false;
        if (info->m_Kind != RHI_ResourceKind::Texture && info->m_Kind != RHI_ResourceKind::CombinedTextureSampler &&
            info->m_Kind != RHI_ResourceKind::RWTexture)
            return false;
        m_Bindings[name] = RHI_TextureBinding{ textureHandle, imageLayout };
        return true;
    }

    bool RHI_ShaderResourceSet::SetSampler(const std::string& name, uint64_t samplerHandle) {
        const auto* info = FindBindingInfo(name);
        if (!info) return false;
        if (info->m_Kind != RHI_ResourceKind::Sampler && info->m_Kind != RHI_ResourceKind::CombinedTextureSampler)
            return false;
        m_Bindings[name] = RHI_SamplerBinding{ samplerHandle };
        return true;
    }

    bool RHI_ShaderResourceSet::Apply(void* shader) const {
        if (!m_Reflection || !shader) return false;
        auto* rhiShader = static_cast<IShaders*>(shader);

        for (const auto& [name, value] : m_Bindings) {
            auto itKey = m_Reflection->m_NameToBinding.find(name);
            if (itKey == m_Reflection->m_NameToBinding.end()) continue;

            const auto* info = m_Reflection->FindBinding(itKey->second.m_Set, itKey->second.m_Binding);
            if (!info) continue;

            (void)rhiShader->ApplyResourceBinding(*info, value);
        }

        return true;
    }

} // namespace Nova::Core::Renderer::RHI