#ifndef RHI_RENDERGRAPH_H
#define RHI_RENDERGRAPH_H

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Api.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_ShaderCompiler.h"

namespace Nova::Core::Renderer::RHI {

    class RHI_Shaders;
    struct RHI_DrawCommand;
    struct RHI_DrawIndexedCommand;

    enum class RHI_RenderPassType : uint8_t {
        Fullscreen = 0,
        Geometry,
        ImGui,
    };

    /** Where a pass writes its color/depth attachments. */
    enum class RHI_RenderTarget : uint8_t {
        Viewport,
        BackBuffer,
    };

    enum class RHI_LoadOp : uint8_t {
        Clear,
        Load,
        DontCare,
    };

    struct NV_API RHI_GraphicsShaderDesc {
        std::filesystem::path m_VertexShader;
        std::filesystem::path m_FragmentShader;
        std::string m_EntryPoint = "main";
        /** Extra Slang include search paths (engine/editor shader roots are added automatically). */
        std::vector<std::filesystem::path> m_IncludeDirs;
    };

    struct NV_API RHI_FullscreenPassDesc {
        std::string m_Name;
        RHI_GraphicsShaderDesc m_Shaders;
        RHI_RenderTarget m_Target = RHI_RenderTarget::Viewport;
        bool m_ClearColor = true;
        bool m_ClearDepth = true;
        bool m_DepthTest = true;
        bool m_DepthWrite = true;
        bool m_AlphaBlend = true;
    };

    struct NV_API RHI_GeometryPassDesc {
        std::string m_Name;
        RHI_GraphicsShaderDesc m_Shaders;
        RHI_RenderTarget m_Target = RHI_RenderTarget::Viewport;
        RHI_LoadOp m_ColorLoadOp = RHI_LoadOp::Load;
        bool m_ClearDepth = true;
        bool m_DepthTest = true;
        bool m_DepthWrite = true;
    };

    struct NV_API RHI_ImGuiPassDesc {
        std::string m_Name;
        RHI_RenderTarget m_Target = RHI_RenderTarget::BackBuffer;
        bool m_ClearColor = true;
    };

    struct NV_API RHI_RenderPassDesc {
        RHI_RenderPassType m_Type = RHI_RenderPassType::Fullscreen;
        RHI_FullscreenPassDesc m_Fullscreen{};
        RHI_GeometryPassDesc m_Geometry{};
        RHI_ImGuiPassDesc m_ImGui{};
    };

    /**
     * Abstract render graph: ordered pass descriptions plus backend-specific execution.
     * Concrete implementations are created via RHI_RenderGraphBuilder::Build() or IRenderGraph::Create().
     */
    class NV_API IRenderGraph {
    public:
        virtual ~IRenderGraph() = default;

        static std::unique_ptr<IRenderGraph> Create(
            Core::GraphicsAPI api,
            std::vector<RHI_RenderPassDesc> passes);

        virtual void OnBeginFrame() = 0;
        virtual void OnEndFrame() = 0;
        virtual void OnDraw(const RHI_DrawCommand& cmd) = 0;
        virtual void OnDrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;
        virtual void OnTransitionToImGuiPass() = 0;
        virtual bool ReloadChangedShaders() = 0;

        const std::vector<RHI_RenderPassDesc>& GetPasses() const { return m_Passes; }
        size_t GetPassCount() const { return m_Passes.size(); }

        int FindPassIndex(const std::string& name) const;
        int FindPassIndexByType(RHI_RenderPassType type) const;

        RHI_Shaders* GetPassShader(size_t passIndex) const;
        RHI_Shaders* GetPassShader(const std::string& name) const;

        bool IsCompiled() const { return m_Compiled; }

    protected:
        explicit IRenderGraph(std::vector<RHI_RenderPassDesc> passes);

        void SetPassShader(size_t passIndex, RHI_Shaders* shader);
        void SetCompiled(bool compiled) { m_Compiled = compiled; }
        void ClearPassShaders();

        std::vector<RHI_RenderPassDesc> m_Passes;
        std::vector<RHI_Shaders*> m_PassShaders;
        bool m_Compiled = false;
    };

    class NV_API RHI_RenderGraphBuilder {
    public:
        RHI_RenderGraphBuilder& AddPass(RHI_RenderPassDesc pass);

        std::unique_ptr<IRenderGraph> Build(Core::GraphicsAPI api);

    private:
        std::vector<RHI_RenderPassDesc> m_Passes;
    };

} // namespace Nova::Core::Renderer::RHI

#endif // RHI_RENDERGRAPH_H
