#include "Renderer/RHI/RHI_ShaderReflection.h"

#include <algorithm>

#include "Renderer/RHI/RHI_ShaderCompiler.h"

namespace Nova::Core::Renderer::RHI {

    const RHI_DescriptorSetLayoutInfo* RHI_ProgramReflection::FindSet(uint32_t setIndex) const {
        for (const auto& s : sets) {
            if (s.set == setIndex) return &s;
        }
        return nullptr;
    }

    const RHI_BindingInfo* RHI_ProgramReflection::FindBinding(uint32_t setIndex, uint32_t binding) const {
        if (const auto* s = FindSet(setIndex)) {
            for (const auto& b : s->bindings) {
                if (b.key.binding == binding) return &b;
            }
        }
        return nullptr;
    }

    static void SortAndDedupe(RHI_ProgramReflection& refl) {
        std::sort(refl.sets.begin(), refl.sets.end(),
            [](const auto& a, const auto& b) { return a.set < b.set; });

        for (auto& set : refl.sets) {
            std::sort(set.bindings.begin(), set.bindings.end(),
                [](const auto& a, const auto& b) { return a.key.binding < b.key.binding; });

            // Dedupe by binding index (keep first; callers should have merged fields already).
            set.bindings.erase(
                std::unique(set.bindings.begin(), set.bindings.end(),
                    [](const auto& a, const auto& b) { return a.key.binding == b.key.binding; }),
                set.bindings.end());
        }
    }

    RHI_ShaderStageMask ToStageMask(RHI_ShaderStage stage) {
        switch (stage) {
            case RHI_ShaderStage::Vertex:          return RHI_ShaderStageMask::Vertex;
            case RHI_ShaderStage::Fragment:        return RHI_ShaderStageMask::Fragment;
            case RHI_ShaderStage::Geometry:        return RHI_ShaderStageMask::Geometry;
            case RHI_ShaderStage::TessControl:     return RHI_ShaderStageMask::TessCtrl;
            case RHI_ShaderStage::TessEvaluation:  return RHI_ShaderStageMask::TessEval;
            case RHI_ShaderStage::Compute:         return RHI_ShaderStageMask::Compute;
            case RHI_ShaderStage::RayGen:          return RHI_ShaderStageMask::RayGen;
            case RHI_ShaderStage::RayMiss:         return RHI_ShaderStageMask::RayMiss;
            case RHI_ShaderStage::RayClosestHit:   return RHI_ShaderStageMask::RayCHit;
            case RHI_ShaderStage::RayAnyHit:       return RHI_ShaderStageMask::RayAHit;
            case RHI_ShaderStage::RayIntersection: return RHI_ShaderStageMask::RayISect;
            case RHI_ShaderStage::RayCallable:     return RHI_ShaderStageMask::RayCall;
            default:                               return RHI_ShaderStageMask::None;
        }
    }

    RHI_ProgramReflection MergeProgramReflections(const std::vector<RHI_ProgramReflection>& perStage) {
        RHI_ProgramReflection out{};

        // Merge push constants (union size, OR stages). Size mismatches are resolved by taking max.
        for (const auto& r : perStage) {
            if (!r.pushConstants) continue;
            if (!out.pushConstants) {
                out.pushConstants = *r.pushConstants;
            } else {
                out.pushConstants->sizeBytes = std::max(out.pushConstants->sizeBytes, r.pushConstants->sizeBytes);
                out.pushConstants->stages |= r.pushConstants->stages;
            }
        }

        // Merge descriptor sets by set index; then bindings by binding index.
        for (const auto& r : perStage) {
            for (const auto& set : r.sets) {
                auto itSet = std::find_if(out.sets.begin(), out.sets.end(),
                    [&](const auto& s) { return s.set == set.set; });
                if (itSet == out.sets.end()) {
                    out.sets.push_back(set);
                } else {
                    // Merge bindings into existing set.
                    for (const auto& b : set.bindings) {
                        auto itB = std::find_if(itSet->bindings.begin(), itSet->bindings.end(),
                            [&](const auto& existing) { return existing.key.binding == b.key.binding; });
                        if (itB == itSet->bindings.end()) {
                            itSet->bindings.push_back(b);
                        } else {
                            // Merge stage flags; prefer known metadata.
                            itB->stages |= b.stages;
                            if (itB->kind == RHI_ResourceKind::Unknown) itB->kind = b.kind;
                            if (itB->arrayCount == 1 && b.arrayCount != 1) itB->arrayCount = b.arrayCount;
                            if (itB->byteSizeIfKnown == 0 && b.byteSizeIfKnown != 0) itB->byteSizeIfKnown = b.byteSizeIfKnown;
                            if (itB->fullName.empty() && !b.fullName.empty()) itB->fullName = b.fullName;
                            itB->isDynamicUniformBuffer = itB->isDynamicUniformBuffer || b.isDynamicUniformBuffer;
                        }
                    }
                }
            }

            // Merge nameToBinding (first wins on conflict).
            for (const auto& [name, key] : r.nameToBinding) {
                out.nameToBinding.emplace(name, key);
            }
        }

        SortAndDedupe(out);
        return out;
    }

} // namespace Nova::Core::Renderer::RHI