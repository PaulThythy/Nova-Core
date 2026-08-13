#ifndef RHI_RENDERGRAPH_H
#define RHI_RENDERGRAPH_H

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "Api.h"
#include "Asset/Assets/ShaderAsset.h"
#include "Core/GraphicsAPI.h"
#include "Renderer/RHI/RHI_ShaderUniforms.h"

namespace Nova::Core::Renderer::RHI {

    class IShaders;
    struct RHI_DrawCommand;
    struct RHI_DrawIndexedCommand;

    // -------------------------------------------------------------------------
    // Resource descriptors
    // -------------------------------------------------------------------------

    enum class RHI_TextureFormat : uint8_t {
        Unknown,
        RGBA8,
        RGBA16F,
        RGBA32F,
        Depth32,
        Depth24Stencil8,
    };

    enum class RHI_TextureUsage : uint32_t {
        None = 0,
        ColorAttachment = 1u << 0,
        DepthAttachment = 1u << 1,
        Sampled           = 1u << 2,
        Storage           = 1u << 3,
    };

    inline RHI_TextureUsage operator|(RHI_TextureUsage a, RHI_TextureUsage b) {
        return static_cast<RHI_TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline RHI_TextureUsage operator&(RHI_TextureUsage a, RHI_TextureUsage b) {
        return static_cast<RHI_TextureUsage>(static_cast<uint32_t>(a) & static_cast<uint32_t>(b));
    }

    inline bool HasTextureUsage(RHI_TextureUsage flags, RHI_TextureUsage test) {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
    }

    struct NV_API RHI_TextureDesc {
        uint32_t m_Width = 0;
        uint32_t m_Height = 0;
        RHI_TextureFormat m_Format = RHI_TextureFormat::Unknown;
        RHI_TextureUsage m_Usage = RHI_TextureUsage::Sampled;
        /** 1 = Texture2D; >1 = Texture2DArray (e.g. shadow map layers). */
        uint32_t m_Layers = 1;
        /** When false, viewport resize leaves width/height unchanged (fixed-size shadow maps). */
        bool m_ResizeWithViewport = true;
        /** When true, create a comparison sampler suitable for shadow SampleCmp. */
        bool m_ComparisonSampler = false;
    };

    enum class RHI_BufferFormat : uint8_t {
        Unknown,
        Raw,
        Uniform,
        Storage,
        Vertex,
        Index,
    };

    enum class RHI_BufferUsage : uint32_t {
        None = 0,
        Uniform  = 1u << 0,
        Storage  = 1u << 1,
        Vertex   = 1u << 2,
        Index    = 1u << 3,
        Transfer = 1u << 4,
    };

    inline RHI_BufferUsage operator|(RHI_BufferUsage a, RHI_BufferUsage b) {
        return static_cast<RHI_BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }

    inline bool HasBufferUsage(RHI_BufferUsage flags, RHI_BufferUsage test) {
        return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(test)) != 0;
    }

    struct NV_API RHI_BufferDesc {
        uint64_t m_Size = 0;
        RHI_BufferFormat m_Format = RHI_BufferFormat::Raw;
        RHI_BufferUsage m_Usage = RHI_BufferUsage::None;
    };

    enum class RHI_ResourceState : uint8_t {
        Undefined,
        RenderTarget,
        DepthWrite,
        ShaderRead,
        Present,
    };

    enum class RHI_LoadOp : uint8_t {
        Clear,
        Load,
        DontCare,
    };

    // -------------------------------------------------------------------------
    // Handles — every resource is an index into the graph's arrays
    // -------------------------------------------------------------------------

    struct NV_API RHI_TextureHandle {
        uint32_t m_Index = UINT32_MAX;
        bool IsValid() const { return m_Index != UINT32_MAX; }
        static RHI_TextureHandle Invalid() { return {}; }
        friend bool operator==(const RHI_TextureHandle& a, const RHI_TextureHandle& b) { return a.m_Index == b.m_Index; }
        friend bool operator!=(const RHI_TextureHandle& a, const RHI_TextureHandle& b) { return !(a == b); }
    };

    struct NV_API RHI_BufferHandle {
        uint32_t m_Index = UINT32_MAX;
        bool IsValid() const { return m_Index != UINT32_MAX; }
        static RHI_BufferHandle Invalid() { return {}; }
        friend bool operator==(const RHI_BufferHandle& a, const RHI_BufferHandle& b) { return a.m_Index == b.m_Index; }
        friend bool operator!=(const RHI_BufferHandle& a, const RHI_BufferHandle& b) { return !(a == b); }
    };

    struct NV_API RHI_ShaderHandle {
        uint32_t m_Index = UINT32_MAX;
        bool IsValid() const { return m_Index != UINT32_MAX; }
        static RHI_ShaderHandle Invalid() { return {}; }
        friend bool operator==(const RHI_ShaderHandle& a, const RHI_ShaderHandle& b) { return a.m_Index == b.m_Index; }
        friend bool operator!=(const RHI_ShaderHandle& a, const RHI_ShaderHandle& b) { return !(a == b); }
    };

    // -------------------------------------------------------------------------
    // Shader description — graphics today, compute by filling m_Compute
    // -------------------------------------------------------------------------

    enum class RHI_VertexLayout : uint8_t {
        None,
        FullscreenQuad,
        Mesh,
    };

    enum class RHI_CullMode : uint8_t {
        None = 0,
        Front,
        Back,
    };

    struct NV_API RHI_ShaderDesc {
        std::string m_Name;

        /** Compiled stage assets acquired via AssetManager (not raw filesystem paths). */
        std::shared_ptr<Core::Asset::Assets::ShaderAsset> m_Vertex;
        std::shared_ptr<Core::Asset::Assets::ShaderAsset> m_Fragment;
        std::shared_ptr<Core::Asset::Assets::ShaderAsset> m_Compute;

        std::string m_EntryPoint = "main";

        RHI_VertexLayout m_VertexLayout = RHI_VertexLayout::Mesh;
        bool m_AlphaBlend = false;
        bool m_DepthTest = true;
        bool m_DepthWrite = true;
        /** Depth-only pipeline (no color attachments, fragment stage optional). */
        bool m_DepthOnly = false;
        RHI_CullMode m_CullMode = RHI_CullMode::Back;
        float m_DepthBiasConstant = 0.0f;
        float m_DepthBiasSlope = 0.0f;

        bool IsCompute() const { return static_cast<bool>(m_Compute); }
    };

    // -------------------------------------------------------------------------
    // Pass execution context — abstract interface passed to each pass callback
    // -------------------------------------------------------------------------

    class NV_API IPassContext {
    public:
        virtual ~IPassContext() = default;

        virtual IShaders* GetShader(RHI_ShaderHandle shader) = 0;
        virtual void DrawFullscreen(RHI_ShaderHandle shader) = 0;
        virtual void BindShader(RHI_ShaderHandle shader) = 0;
        virtual void Draw(const RHI_DrawCommand& cmd) = 0;
        virtual void DrawIndexed(const RHI_DrawIndexedCommand& cmd) = 0;
        virtual uint32_t GetRenderWidth() const = 0;
        virtual uint32_t GetRenderHeight() const = 0;

        /** Runtime depth bias (shadow maps). No-op if the bound pipeline has no depth bias enabled. */
        virtual void SetDepthBias(float constantFactor, float slopeFactor, float clamp = 0.0f) {
            (void)constantFactor; (void)slopeFactor; (void)clamp;
        }

        /**
         * Depth-only multi-layer shadow rendering: begin/end a render pass targeting a single
         * array layer. Used when the graph pass writes a depth Texture2DArray (no color).
         */
        virtual void BeginDepthLayer(RHI_TextureHandle depth, uint32_t layer, bool clear) {
            (void)depth; (void)layer; (void)clear;
        }
        virtual void EndDepthLayer() {}
    };

    // -------------------------------------------------------------------------
    // Declared graph data — filled by the builder, consumed by the backend
    // -------------------------------------------------------------------------

    static constexpr size_t RHI_InvalidPassIndex = static_cast<size_t>(-1);

    struct NV_API RHI_RenderGraphPassDesc {
        std::string m_Name;

        std::vector<RHI_TextureHandle> m_ReadTextures;
        std::vector<RHI_TextureHandle> m_WriteTextures;
        std::vector<RHI_TextureHandle> m_ReadWriteTextures;
        std::vector<RHI_BufferHandle> m_ReadBuffers;
        std::vector<RHI_BufferHandle> m_WriteBuffers;

        /** Passes this one depends on, derived from resource versioning during setup. */
        std::vector<size_t> m_DependsOn;

        /** When true, this pass is deferred to ExecutePresentPasses (e.g. swapchain + ImGui). */
        bool m_PresentOnly = false;

        std::function<void(IPassContext&)> m_Execute;
    };

    struct NV_API RHI_RenderGraphTextureResource {
        RHI_TextureDesc m_Desc;
        bool m_Imported = false;
        bool m_IsSwapchain = false;
        RHI_ResourceState m_InitialState = RHI_ResourceState::Undefined;
    };

    struct NV_API RHI_RenderGraphBufferResource {
        RHI_BufferDesc m_Desc;
        bool m_Imported = false;
    };

    struct NV_API RHI_RenderGraphData {
        std::vector<RHI_RenderGraphTextureResource> m_Textures;
        std::vector<RHI_RenderGraphBufferResource> m_Buffers;
        std::vector<RHI_ShaderDesc> m_Shaders;
        std::vector<RHI_RenderGraphPassDesc> m_Passes;
    };

    /**
     * Abstract render graph: compiled pass order and backend execution.
     */
    class NV_API IRenderGraph {
    public:
        virtual ~IRenderGraph() = default;

        static std::unique_ptr<IRenderGraph> Create(Core::GraphicsAPI api, RHI_RenderGraphData data);

        virtual void OnBeginFrame() = 0;

        /** Execute passes that do not target the swapchain (scene rendering). */
        virtual void ExecuteScenePasses() = 0;

        /** Execute passes that write to the imported swapchain texture (presentation). */
        virtual void ExecutePresentPasses() = 0;

        virtual void OnEndFrame() = 0;
        virtual bool ReloadChangedShaders() = 0;

        /** Resolve a declared shader to its backend pipeline, building it on first use. */
        virtual IShaders* GetShader(RHI_ShaderHandle shader) = 0;

        virtual void* GetTextureImGuiID(RHI_TextureHandle handle) const = 0;
        virtual bool Resize(uint32_t width, uint32_t height) = 0;

        /** Engine `ParameterBlock<NovaEngine>` buffers (frame/mvp/material/lights). */
        virtual const RHI_EngineParameterBlock* GetEngineParameterBlock() const { return nullptr; }

        /**
         * Bind a depth Texture2DArray (+ comparison sampler) to `nova.shadowMaps` /
         * `nova.shadowSampler` on every built pipeline that reflects those names.
         */
        virtual bool BindEngineShadowMaps(RHI_TextureHandle shadowMaps) { (void)shadowMaps; return false; }

        const std::vector<RHI_RenderGraphPassDesc>& GetPasses() const { return m_Passes; }
        const std::vector<size_t>& GetExecutionOrder() const { return m_ExecutionOrder; }
        bool IsCompiled() const { return m_Compiled; }

    protected:
        explicit IRenderGraph(RHI_RenderGraphData data);

        bool SortPassesTopologically();
        bool PassWritesSwapchain(const RHI_RenderGraphPassDesc& pass) const;

        RHI_RenderGraphData m_Data;
        std::vector<RHI_RenderGraphPassDesc> m_Passes;
        std::vector<size_t> m_ExecutionOrder;
        bool m_Compiled = false;
    };

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
     *
     * Example:
     *   auto vert = AssetManager::Get().Acquire<ShaderAsset>("Engine://Shaders/Scene.vert.slang").GetAssetRef();
     *   auto frag = AssetManager::Get().Acquire<ShaderAsset>("Engine://Shaders/Scene.frag.slang").GetAssetRef();
     *   vert->Compile(); frag->Compile();
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

#endif // RHI_RENDERGRAPH_H