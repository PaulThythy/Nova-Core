#include "Renderer/RHI/RHI_RenderGraph.h"

#include "Renderer/RHI/RHI_Shaders.h"
#include "Renderer/Backends/Vulkan/VK_RenderGraph.h"
#include "Core/Log.h"

namespace Nova::Core::Renderer::RHI {

    IRenderGraph::IRenderGraph(std::vector<RHI_RenderPassDesc> passes) : m_Passes(std::move(passes)) {}

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
            const auto& p = m_Passes[i];
            const std::string* passName = nullptr;
            switch (p.m_Type) {
            case RHI_RenderPassType::Fullscreen: passName = &p.m_Fullscreen.m_Name; break;
            case RHI_RenderPassType::Geometry:   passName = &p.m_Geometry.m_Name; break;
            case RHI_RenderPassType::ImGui:      passName = &p.m_ImGui.m_Name; break;
            }
            if (passName && *passName == name)
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