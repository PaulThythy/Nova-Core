#include "Renderer/RHI/RHI_RenderGraph.h"

#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"

#include "Core/Log.h"

#include <algorithm>
#include <numeric>
#include <unordered_map>

namespace Nova::Core::Renderer::RHI {

    std::string GetRenderPassName(const RHI_RenderPassDesc& pass) {
        switch (pass.m_Type) {
            case RHI_RenderPassType::Fullscreen: return pass.m_Fullscreen.m_Name;
            case RHI_RenderPassType::Geometry:   return pass.m_Geometry.m_Name;
            case RHI_RenderPassType::ImGui:      return pass.m_ImGui.m_Name;
        }
        return {};
    }

    bool PassReadsResource(const RHI_RenderPassDesc& pass, RHI_RenderGraphResource resource) {
        return std::find(pass.m_Reads.begin(), pass.m_Reads.end(), resource) != pass.m_Reads.end();
    }

    bool PassWritesResource(const RHI_RenderPassDesc& pass, RHI_RenderGraphResource resource) {
        return std::find(pass.m_Writes.begin(), pass.m_Writes.end(), resource) != pass.m_Writes.end();
    }

    bool PassWritesToViewport(const RHI_RenderPassDesc& pass) {
        return PassWritesResource(pass, RHI_RenderGraphResource::ViewportColor) || PassWritesResource(pass, RHI_RenderGraphResource::ViewportDepth);
    }

    bool PassWritesToBackBuffer(const RHI_RenderPassDesc& pass) {
        return PassWritesResource(pass, RHI_RenderGraphResource::BackBufferColor) || PassWritesResource(pass, RHI_RenderGraphResource::BackBufferDepth);
    }

    void AddEdge(size_t from, size_t to, std::vector<std::vector<size_t>>& adjacency, std::vector<size_t>& inDegree) {
        if (from == to)
            return;

        auto& edges = adjacency[from];
        if (std::find(edges.begin(), edges.end(), to) != edges.end())
            return;
        edges.push_back(to);
        inDegree[to]++;
    }

    void AddResourceDependency(size_t from, size_t to, std::vector<std::vector<size_t>>& adjacency, std::vector<size_t>& inDegree) {
        if (from == to)
            return;
        AddEdge(from, to, adjacency, inDegree);
    }

    IRenderGraph::IRenderGraph(std::vector<RHI_RenderPassDesc> passes) : m_Passes(std::move(passes)) {
        if (!SortPassesTopologically()) {
            NV_LOG_ERROR("IRenderGraph: topological sort failed, falling back to declaration order");
            m_ExecutionOrder.resize(m_Passes.size());
            std::iota(m_ExecutionOrder.begin(), m_ExecutionOrder.end(), size_t{0});
        }
    }

    bool IRenderGraph::SortPassesTopologically() {
        const size_t passCount = m_Passes.size();
        m_ExecutionOrder.clear();

        if (passCount == 0)
            return true;

        std::vector<std::vector<size_t>> adjacency(passCount);
        std::vector<size_t> inDegree(passCount, 0);

        // Track the latest writer for each resource to infer ordering from reads/writes.
        std::unordered_map<RHI_RenderGraphResource, size_t> lastWriter;

        for (size_t passIndex = 0; passIndex < passCount; ++passIndex) {
            const auto& pass = m_Passes[passIndex];

            for (const std::string& dependencyName : pass.m_Dependencies) {
                const int dependencyIndex = FindPassIndex(dependencyName);

                if (dependencyIndex < 0) {
                    NV_LOG_ERROR(("IRenderGraph: pass '" + GetRenderPassName(pass) + "' depends on unknown pass '" + dependencyName + "'").c_str());
                    return false;
                }
                AddEdge(static_cast<size_t>(dependencyIndex), passIndex, adjacency, inDegree);
            }

            for (RHI_RenderGraphResource resource : pass.m_Reads) {
                const auto writerIt = lastWriter.find(resource);
                if (writerIt != lastWriter.end())
                    AddResourceDependency(writerIt->second, passIndex, adjacency, inDegree);
            }

            for (RHI_RenderGraphResource resource : pass.m_Writes) {
                const auto writerIt = lastWriter.find(resource);

                if (writerIt != lastWriter.end())
                    AddResourceDependency(writerIt->second, passIndex, adjacency, inDegree);
                lastWriter[resource] = passIndex;
            }
        }

        // Kahn's algorithm with stable tie-breaking: preserve declaration order among ready passes.
        std::vector<size_t> ready;

        ready.reserve(passCount);

        for (size_t i = 0; i < passCount; ++i) {
            if (inDegree[i] == 0)
                ready.push_back(i);
        }

        m_ExecutionOrder.reserve(passCount);

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
            NV_LOG_ERROR("IRenderGraph: cycle detected in render pass dependencies");
            m_ExecutionOrder.clear();
            return false;
        }
        return true;
    }

    std::unique_ptr<IRenderGraph> IRenderGraph::Create(Core::GraphicsAPI api, std::vector<RHI_RenderPassDesc> passes) {
        switch (api) {
            case Core::GraphicsAPI::Vulkan:
                return std::make_unique<Backends::Vulkan::VK_RenderGraph>(std::move(passes));

            default:
                NV_LOG_ERROR("IRenderGraph::Create - unsupported graphics API");

            return nullptr;
        }
    }

    int IRenderGraph::FindPassIndex(const std::string& name) const {
        for (size_t i = 0; i < m_Passes.size(); ++i) {
            if (GetRenderPassName(m_Passes[i]) == name)
                return static_cast<int>(i);
        }
        return -1;
    }

    int IRenderGraph::FindPassIndexByType(RHI_RenderPassType type) const {
        for (size_t i = 0; i < m_Passes.size(); ++i) {
            if (m_Passes[i].m_Type == type)
                return static_cast<int>(i);
        }
        return -1;
    }

    void IRenderGraph::SetPassShader(size_t passIndex, RHI_Shaders* shader) {
        if (passIndex >= m_PassShaders.size())
            m_PassShaders.resize(passIndex + 1, nullptr);
        m_PassShaders[passIndex] = shader;
    }

    void IRenderGraph::ClearPassShaders() {
        m_PassShaders.clear();
        m_Compiled = false;
    }

    RHI_Shaders* IRenderGraph::GetPassShader(size_t passIndex) const {
        if (!m_Compiled || passIndex >= m_PassShaders.size())
            return nullptr;
        return m_PassShaders[passIndex];
    }

    RHI_Shaders* IRenderGraph::GetPassShader(const std::string& name) const {
        const int idx = FindPassIndex(name);
        if (idx < 0) return nullptr;
        return GetPassShader(static_cast<size_t>(idx));
    }

    RHI_RenderGraphBuilder& RHI_RenderGraphBuilder::AddPass(RHI_RenderPassDesc pass) {
        m_Passes.push_back(std::move(pass));
        return *this;
    }

    std::unique_ptr<IRenderGraph> RHI_RenderGraphBuilder::Build(Core::GraphicsAPI api) {
        return IRenderGraph::Create(api, std::move(m_Passes));
    }

} // namespace Nova::Core::Renderer::RHI