#ifndef RHI_RENDERGRAPHBUILDER_H
#define RHI_RENDERGRAPHBUILDER_H

#include <string>
#include <utility>
#include <vector>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_RenderGraphTypes.h"

namespace Nova::Core::Renderer::RHI {

    class IRenderGraph;
    class RHI_RenderGraphBuilder;

    /**
     * Scoped access declaration for a single pass. Bound to the pass being set up,
     * so there is no pass index to pass around.
     */
    class NV_API RHI_PassBuilder {
    public:
        void Read(RHI_TextureHandle handle);
        void Write(RHI_TextureHandle handle);

        /** Read-modify-write access (UAV / compute). */
        void ReadWrite(RHI_TextureHandle handle);

        void Read(RHI_BufferHandle handle);
        void Write(RHI_BufferHandle handle);

        /** Defer this pass to the presentation phase (swapchain + ImGui). */
        void PresentOnly();

    private:
        friend class RHI_RenderGraphBuilder;

        RHI_PassBuilder(RHI_RenderGraphBuilder& graph, size_t passIndex) : m_Graph(graph), m_PassIndex(passIndex) {}

        RHI_RenderGraphBuilder& m_Graph;
        size_t m_PassIndex;
    };

    /**
     * Frame graph declaration: resources, shaders and passes.
     * Does not compile or create a backend — call Build() which runs the compiler.
     *
     * Example:
     *   auto color = fg.CreateTexture({1920, 1080, RGBA8, ColorAttachment | Sampled});
     *   auto shader = fg.RegisterShader({.m_Name = "Scene", .m_Vertex = vert, .m_Fragment = frag});
     *   fg.AddPass("Scene",
     *       [&](RHI_PassBuilder& b) { b.Write(color); },
     *       [&](IPassContext& ctx) { ctx.DrawFullscreen(shader); });
     *   renderer.SetRenderGraph(fg.Build(api));
     */
    class NV_API RHI_RenderGraphBuilder {
    public:
        RHI_TextureHandle CreateTexture(const RHI_TextureDesc& desc);
        RHI_TextureHandle ImportTexture(const RHI_TextureDesc& desc, RHI_ResourceState initialState);

        RHI_BufferHandle CreateBuffer(const RHI_BufferDesc& desc);
        RHI_BufferHandle ImportBuffer(const RHI_BufferDesc& desc);

        /** Register a graphics/compute program built from already-acquired ShaderAssets. */
        RHI_ShaderHandle RegisterShader(RHI_ShaderDesc desc);

        /** Register a pass. Setup runs immediately and wires the DAG; execute is stored for later. */
        template<typename SetupFn, typename ExecFn>
        RHI_RenderGraphBuilder& AddPass(std::string name, SetupFn&& setup, ExecFn&& execute) {
            const size_t passIndex = m_Data.m_Passes.size();

            RHI_RenderGraphPassDesc pass{};
            pass.m_Name = std::move(name);
            pass.m_Execute = std::forward<ExecFn>(execute);
            m_Data.m_Passes.push_back(std::move(pass));

            RHI_PassBuilder builder(*this, passIndex);
            setup(builder);
            return *this;
        }

        /** Compile the declared graph, then create the backend IRenderGraph. */
        std::unique_ptr<IRenderGraph> Build(Core::GraphicsAPI api);

        const RHI_RenderGraphData& GetData() const { return m_Data; }

    private:
        friend class RHI_PassBuilder;

        /** One entry per resource write; readers of a version force WAR edges on the next write. */
        struct ResourceVersion {
            size_t m_WriterPass = RHI_InvalidPassIndex;
            std::vector<size_t> m_ReaderPasses;
            bool HasWriter() const { return m_WriterPass != RHI_InvalidPassIndex; }
        };

        using VersionHistory = std::vector<ResourceVersion>;

        void RecordRead(size_t passIndex, VersionHistory& history);
        void RecordWrite(size_t passIndex, VersionHistory& history);
        void RecordReadWrite(size_t passIndex, VersionHistory& history);

        RHI_RenderGraphData m_Data;
        std::vector<VersionHistory> m_TextureVersions;
        std::vector<VersionHistory> m_BufferVersions;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERGRAPHBUILDER_H