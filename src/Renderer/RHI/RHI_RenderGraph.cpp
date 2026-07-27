#include "Renderer/RHI/RHI_RenderGraph.h"

#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"
#include "Core/Log.h"

#include <algorithm>
#include <numeric>
#include <unordered_set>

namespace Nova::Core::Renderer::RHI {

    // -------------------------------------------------------------------------
    // IRenderGraph
    // -------------------------------------------------------------------------

    IRenderGraph::IRenderGraph(RHI_RenderGraphData data)
        : m_Data(std::move(data))
        , m_Passes(m_Data.m_Passes)
    {
        if (!SortPassesTopologically()) {
            NV_LOG_ERROR("IRenderGraph: topological sort failed, falling back to declaration order");
            m_ExecutionOrder.resize(m_Passes.size());
            std::iota(m_ExecutionOrder.begin(), m_ExecutionOrder.end(), size_t{ 0 });
        }
    }

    bool IRenderGraph::PassWritesSwapchain(const RHI_RenderGraphPassDesc& pass) const {
        for (RHI_TextureHandle handle : pass.m_WriteTextures) {
            if (!handle.IsValid() || handle.m_Index >= m_Data.m_Textures.size())
                continue;
            if (m_Data.m_Textures[handle.m_Index].m_IsSwapchain)
                return true;
        }
        return false;
    }

    bool IRenderGraph::SortPassesTopologically() {
        const size_t passCount = m_Passes.size();
        m_ExecutionOrder.clear();

        if (passCount == 0)
            return true;

        // Deduplicate the edges produced by resource versioning into an adjacency list.
        std::vector<std::vector<size_t>> adjacency(passCount);
        std::vector<size_t> inDegree(passCount, 0);

        for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            std::unordered_set<size_t> seen;
            for (size_t dependency : m_Passes[passIndex].m_DependsOn) {
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

        m_ExecutionOrder.reserve(passCount);

        // Always pick the lowest ready index so independent passes keep declaration order.
        while (!ready.empty()) {
            std::sort(ready.begin(), ready.end());
            const size_t current = ready.front();
            ready.erase(ready.begin());

            m_ExecutionOrder.push_back(current);

            for (size_t dependent : adjacency[current]) {
                if (--inDegree[dependent] == 0)
                    ready.push_back(dependent);
            }
        }

        if (m_ExecutionOrder.size() != passCount) {
            NV_LOG_ERROR("IRenderGraph: cycle detected in pass dependencies");
            m_ExecutionOrder.clear();
            return false;
        }
        return true;
    }

    std::unique_ptr<IRenderGraph> IRenderGraph::Create(Core::GraphicsAPI api, RHI_RenderGraphData data) {
        switch (api) {
            case Core::GraphicsAPI::Vulkan:
                return std::make_unique<Backends::Vulkan::VK_RenderGraph>(std::move(data));

            default:
                NV_LOG_ERROR("IRenderGraph::Create - unsupported graphics API");
                return nullptr;
        }
    }

    // -------------------------------------------------------------------------
    // RHI_PassBuilder
    // -------------------------------------------------------------------------

    void RHI_PassBuilder::Read(RHI_TextureHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Graph.m_TextureVersions.size())
            return;
        m_Graph.RecordRead(m_PassIndex, m_Graph.m_TextureVersions[handle.m_Index]);
        m_Graph.m_Data.m_Passes[m_PassIndex].m_ReadTextures.push_back(handle);
    }

    void RHI_PassBuilder::Write(RHI_TextureHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Graph.m_TextureVersions.size())
            return;
        m_Graph.RecordWrite(m_PassIndex, m_Graph.m_TextureVersions[handle.m_Index]);
        m_Graph.m_Data.m_Passes[m_PassIndex].m_WriteTextures.push_back(handle);
    }

    void RHI_PassBuilder::ReadWrite(RHI_TextureHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Graph.m_TextureVersions.size())
            return;
        m_Graph.RecordReadWrite(m_PassIndex, m_Graph.m_TextureVersions[handle.m_Index]);

        auto& pass = m_Graph.m_Data.m_Passes[m_PassIndex];
        pass.m_ReadTextures.push_back(handle);
        pass.m_WriteTextures.push_back(handle);
        pass.m_ReadWriteTextures.push_back(handle);
    }

    void RHI_PassBuilder::Read(RHI_BufferHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Graph.m_BufferVersions.size())
            return;
        m_Graph.RecordRead(m_PassIndex, m_Graph.m_BufferVersions[handle.m_Index]);
        m_Graph.m_Data.m_Passes[m_PassIndex].m_ReadBuffers.push_back(handle);
    }

    void RHI_PassBuilder::Write(RHI_BufferHandle handle) {
        if (!handle.IsValid() || handle.m_Index >= m_Graph.m_BufferVersions.size())
            return;
        m_Graph.RecordWrite(m_PassIndex, m_Graph.m_BufferVersions[handle.m_Index]);
        m_Graph.m_Data.m_Passes[m_PassIndex].m_WriteBuffers.push_back(handle);
    }

    void RHI_PassBuilder::PresentOnly() {
        m_Graph.m_Data.m_Passes[m_PassIndex].m_PresentOnly = true;
    }

    // -------------------------------------------------------------------------
    // RHI_RenderGraphBuilder — resource versioning
    // -------------------------------------------------------------------------

    void RHI_RenderGraphBuilder::RecordRead(size_t passIndex, VersionHistory& history) {
        ResourceVersion& version = history.back();

        // RAW: whoever wrote the current version must run first.
        if (version.HasWriter())
            m_Data.m_Passes[passIndex].m_DependsOn.push_back(version.m_WriterPass);

        version.m_ReaderPasses.push_back(passIndex);
    }

    void RHI_RenderGraphBuilder::RecordWrite(size_t passIndex, VersionHistory& history) {
        ResourceVersion& version = history.back();
        auto& dependsOn = m_Data.m_Passes[passIndex].m_DependsOn;

        // WAW: the previous writer must finish before we overwrite.
        if (version.HasWriter())
            dependsOn.push_back(version.m_WriterPass);

        // WAR: every reader of the current version must finish before we overwrite.
        for (size_t reader : version.m_ReaderPasses)
            dependsOn.push_back(reader);

        history.push_back({});
        history.back().m_WriterPass = passIndex;
    }

    void RHI_RenderGraphBuilder::RecordReadWrite(size_t passIndex, VersionHistory& history) {
        RecordRead(passIndex, history);
        RecordWrite(passIndex, history);
    }

    // -------------------------------------------------------------------------
    // RHI_RenderGraphBuilder — declaration
    // -------------------------------------------------------------------------

    RHI_TextureHandle RHI_RenderGraphBuilder::CreateTexture(const RHI_TextureDesc& desc) {
        const uint32_t index = static_cast<uint32_t>(m_Data.m_Textures.size());
        m_Data.m_Textures.push_back({ desc, false, false, RHI_ResourceState::Undefined });
        m_TextureVersions.emplace_back(1);
        return RHI_TextureHandle{ index };
    }

    RHI_TextureHandle RHI_RenderGraphBuilder::ImportTexture(const RHI_TextureDesc& desc, RHI_ResourceState initialState) {
        const uint32_t index = static_cast<uint32_t>(m_Data.m_Textures.size());
        const bool isSwapchain = initialState == RHI_ResourceState::Present;
        m_Data.m_Textures.push_back({ desc, true, isSwapchain, initialState });
        m_TextureVersions.emplace_back(1);
        return RHI_TextureHandle{ index };
    }

    RHI_BufferHandle RHI_RenderGraphBuilder::CreateBuffer(const RHI_BufferDesc& desc) {
        const uint32_t index = static_cast<uint32_t>(m_Data.m_Buffers.size());
        m_Data.m_Buffers.push_back({ desc, false });
        m_BufferVersions.emplace_back(1);
        return RHI_BufferHandle{ index };
    }

    RHI_BufferHandle RHI_RenderGraphBuilder::ImportBuffer(const RHI_BufferDesc& desc) {
        const uint32_t index = static_cast<uint32_t>(m_Data.m_Buffers.size());
        m_Data.m_Buffers.push_back({ desc, true });
        m_BufferVersions.emplace_back(1);
        return RHI_BufferHandle{ index };
    }

    RHI_ShaderHandle RHI_RenderGraphBuilder::RegisterShader(RHI_ShaderDesc desc) {
        const uint32_t index = static_cast<uint32_t>(m_Data.m_Shaders.size());
        m_Data.m_Shaders.push_back(std::move(desc));
        return RHI_ShaderHandle{ index };
    }

    std::unique_ptr<IRenderGraph> RHI_RenderGraphBuilder::Build(Core::GraphicsAPI api) {
        return IRenderGraph::Create(api, std::move(m_Data));
    }

} // namespace Nova::Core::Renderer::RHI