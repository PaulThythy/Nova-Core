#include "Renderer/RHI/RHI_RenderGraphCompiler.h"

#include "Core/Log.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace Nova::Core::Renderer::RHI {

    bool RHI_RenderGraphCompiler::Validate(const RHI_RenderGraphData& data) {
        if (data.m_Passes.empty()) {
            NV_LOG_ERROR("RHI_RenderGraphCompiler: no passes declared");
            return false;
        }

        const size_t passCount = data.m_Passes.size();
        const size_t textureCount = data.m_Textures.size();
        const size_t bufferCount = data.m_Buffers.size();
        const size_t shaderCount = data.m_Shaders.size();

        auto checkTexture = [&](RHI_TextureHandle handle, const char* passName) {
            if (!handle.IsValid() || handle.m_Index >= textureCount) {
                NV_LOG_ERROR(("RHI_RenderGraphCompiler: invalid texture handle in pass '" +
                    std::string(passName) + "'").c_str());
                return false;
            }
            return true;
        };

        auto checkBuffer = [&](RHI_BufferHandle handle, const char* passName) {
            if (!handle.IsValid() || handle.m_Index >= bufferCount) {
                NV_LOG_ERROR(("RHI_RenderGraphCompiler: invalid buffer handle in pass '" +
                    std::string(passName) + "'").c_str());
                return false;
            }
            return true;
        };

        for (const auto& pass : data.m_Passes) {
            for (RHI_TextureHandle h : pass.m_ReadTextures)
                if (!checkTexture(h, pass.m_Name.c_str())) return false;
            for (RHI_TextureHandle h : pass.m_WriteTextures)
                if (!checkTexture(h, pass.m_Name.c_str())) return false;
            for (RHI_TextureHandle h : pass.m_ReadWriteTextures)
                if (!checkTexture(h, pass.m_Name.c_str())) return false;
            for (RHI_BufferHandle h : pass.m_ReadBuffers)
                if (!checkBuffer(h, pass.m_Name.c_str())) return false;
            for (RHI_BufferHandle h : pass.m_WriteBuffers)
                if (!checkBuffer(h, pass.m_Name.c_str())) return false;

            for (size_t dep : pass.m_DependsOn) {
                if (dep >= passCount) {
                    NV_LOG_ERROR(("RHI_RenderGraphCompiler: invalid dependency index in pass '" +
                        pass.m_Name + "'").c_str());
                    return false;
                }
            }
        }

        (void)shaderCount;
        return true;
    }

    bool RHI_RenderGraphCompiler::SortPassesTopologically(
        const std::vector<RHI_RenderGraphPassDesc>& passes,
        std::vector<size_t>& outOrder)
    {
        const size_t passCount = passes.size();
        outOrder.clear();

        if (passCount == 0)
            return true;

        // Deduplicate the edges produced by resource versioning into an adjacency list.
        std::vector<std::vector<size_t>> adjacency(passCount);
        std::vector<size_t> inDegree(passCount, 0);

        for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            std::unordered_set<size_t> seen;
            for (size_t dependency : passes[passIndex].m_DependsOn) {
                if (dependency >= passCount || dependency == passIndex)
                    continue;
                if (!seen.insert(dependency).second)
                    continue;
                adjacency[dependency].push_back(passIndex);
                inDegree[passIndex]++;
            }
        }

        std::vector<size_t> ready;
        ready.reserve(passCount);

        for (size_t i = 0; i < passCount; ++i) {
            if (inDegree[i] == 0)
                ready.push_back(i);
        }

        outOrder.reserve(passCount);

        // Always pick the lowest ready index so independent passes keep declaration order.
        while (!ready.empty()) {
            std::sort(ready.begin(), ready.end());
            const size_t current = ready.front();
            ready.erase(ready.begin());

            outOrder.push_back(current);

            for (size_t dependent : adjacency[current]) {
                if (--inDegree[dependent] == 0)
                    ready.push_back(dependent);
            }
        }

        if (outOrder.size() != passCount) {
            NV_LOG_ERROR("RHI_RenderGraphCompiler: cycle detected in pass dependencies");
            outOrder.clear();
            return false;
        }
        return true;
    }

    RHI_CompiledRenderGraph RHI_RenderGraphCompiler::Compile(RHI_RenderGraphData data) {
        RHI_CompiledRenderGraph compiled{};
        compiled.m_Data = std::move(data);

        if (!Validate(compiled.m_Data)) {
            compiled.m_ExecutionOrder.resize(compiled.m_Data.m_Passes.size());
            std::iota(compiled.m_ExecutionOrder.begin(), compiled.m_ExecutionOrder.end(), size_t{ 0 });
            compiled.m_Valid = false;
            return compiled;
        }

        if (!SortPassesTopologically(compiled.m_Data.m_Passes, compiled.m_ExecutionOrder)) {
            NV_LOG_ERROR("RHI_RenderGraphCompiler: topological sort failed, falling back to declaration order");
            compiled.m_ExecutionOrder.resize(compiled.m_Data.m_Passes.size());
            std::iota(compiled.m_ExecutionOrder.begin(), compiled.m_ExecutionOrder.end(), size_t{ 0 });
            compiled.m_Valid = false;
            return compiled;
        }

        compiled.m_Valid = true;
        return compiled;
    }

} // namespace Nova::Core::Renderer::RHI