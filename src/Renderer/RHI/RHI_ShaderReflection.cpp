#include "Renderer/RHI/RHI_ShaderReflection.h"

#include <algorithm>

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

} // namespace Nova::Core::Renderer::RHI
