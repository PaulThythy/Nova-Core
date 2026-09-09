#ifndef RHI_RENDERGRAPHCOMPILER_H
#define RHI_RENDERGRAPHCOMPILER_H

#include "Api.h"
#include "Renderer/RHI/RHI_RenderGraphTypes.h"

namespace Nova::Core::Renderer::RHI {

    /**
     * Compiles declared render-graph data into an execution-ready form:
     * validation + topological sort of pass dependencies.
     */
    class NV_API RHI_RenderGraphCompiler {
    public:
        /**
         * Consume declared data and produce a compiled graph.
         * On cycle detection, falls back to declaration order and sets m_Valid = false.
         */
        static RHI_CompiledRenderGraph Compile(RHI_RenderGraphData data);

    private:
        static bool SortPassesTopologically(
            const std::vector<RHI_RenderGraphPassDesc>& passes,
            std::vector<size_t>& outOrder);

        static bool Validate(const RHI_RenderGraphData& data);
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERGRAPHCOMPILER_H